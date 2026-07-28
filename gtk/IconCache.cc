/*
 * icons.[ch] written by Paolo Bacchilega, who writes:
 * "There is no problem for me, you can license my code
 * under whatever licence you wish :)"
 *
 */

#include "IconCache.h"

#include "GtkCompat.h"

#include <libtransmission-app/torrent-icon.h>

#include <giomm/contenttype.h>
#include <giomm/themedicon.h>

#if GTKMM_CHECK_VERSION(4, 0, 0)
#include <gdkmm/display.h>
#include <gdkmm/graphene_point.h>
#include <gdkmm/snapshot.h>
// Gdk::Paintable_Class lets a custom GType declare that it implements
// GdkPaintable; deriving from Gdk::Paintable alone registers it too late.
#include <gdkmm/private/paintable_p.h>
#include <gtkmm/icontheme.h>
#include <gtkmm/snapshot.h>

#include <glibmm/object.h>
#else
#include <giomm/emblem.h>
#include <giomm/emblemedicon.h>
#endif

#include <algorithm>
#include <functional> // for std::less<>
#include <map>
#include <string>
#include <string_view>
#include <utility> // for std::move()

using namespace std::literals;

using IconCache = std::map<std::string, Glib::RefPtr<Gio::Icon>, std::less<>>;

std::string_view const DirectoryMimeType = "folder"sv;

Glib::RefPtr<Gio::Icon> gtr_get_mime_type_icon(std::string_view mime_type)
{
    static IconCache cache;

    if (auto mime_it = cache.find(mime_type); mime_it != std::end(cache)) {
        return mime_it->second;
    }

    auto mime_type_str = std::string{ mime_type };
    auto icon = Gio::content_type_get_icon(mime_type_str);
    if (icon != nullptr) {
        cache.try_emplace(std::move(mime_type_str), icon);
    }

    return icon;
}

namespace
{

#if GTKMM_CHECK_VERSION(4, 0, 0)

// Adds GdkPaintable to the custom GType. Runs before Glib::Object, which is
// what creates that type.
class PaintableInit : virtual public Glib::ObjectBase
{
protected:
    PaintableInit()
    {
        static auto paintable_class = Gdk::Paintable_Class{};
        add_custom_interface_class(&paintable_class.init());
    }
};

// Composites the folder and mime type icons into one paintable, laid out per
// <libtransmission-app/torrent-icon.h>. A null folder icon draws the mime type
// icon alone, filling the box.
class TorrentIcon
    : public PaintableInit
    , public Glib::Object
    , public Gdk::Paintable
{
public:
    static Glib::RefPtr<TorrentIcon> create(Glib::RefPtr<Gio::Icon> mime_icon, Glib::RefPtr<Gio::Icon> folder_icon)
    {
        // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
        return Glib::make_refptr_for_instance(new TorrentIcon(std::move(mime_icon), std::move(folder_icon)));
    }

protected:
    TorrentIcon(Glib::RefPtr<Gio::Icon> mime_icon, Glib::RefPtr<Gio::Icon> folder_icon)
        : Glib::ObjectBase(typeid(TorrentIcon))
        , mime_icon_{ std::move(mime_icon) }
        , folder_icon_{ std::move(folder_icon) }
    {
    }

    void snapshot_vfunc(Glib::RefPtr<Gdk::Snapshot> const& snapshot, double width, double height) override
    {
        // looked up here, not in the constructor, so that the icons follow both
        // the size being asked for and any icon theme change
        auto const theme = Gtk::IconTheme::get_for_display(Gdk::Display::get_default());
        auto const gtk_snapshot = Glib::wrap_gtk_snapshot(GTK_SNAPSHOT(snapshot->gobj()), true);
        auto const draw = [&](Glib::RefPtr<Gio::Icon> const& icon, double scale, double offset) {
            auto const w = width * scale;
            auto const h = height * scale;

            gtk_snapshot->save();
            gtk_snapshot->translate(
                Gdk::Graphene::Point(static_cast<float>(width * offset), static_cast<float>(height * offset)));
            theme->lookup_icon(icon, std::max(1, static_cast<int>(std::min(w, h))), 1)->snapshot(gtk_snapshot, w, h);
            gtk_snapshot->restore();
        };

        if (folder_icon_ == nullptr) {
            draw(mime_icon_, 1.0, 0.0);
            return;
        }

        draw(folder_icon_, tr::app::FolderIconScale, 0.0);
        draw(mime_icon_, tr::app::MimeIconScale, tr::app::MimeIconOffset);
    }

private:
    Glib::RefPtr<Gio::Icon> mime_icon_;
    Glib::RefPtr<Gio::Icon> folder_icon_;
};

Glib::RefPtr<TorrentIconType> make_torrent_icon(Glib::RefPtr<Gio::Icon> mime_icon, bool is_folder)
{
    if (mime_icon == nullptr) {
        return {};
    }

    return TorrentIcon::create(std::move(mime_icon), is_folder ? gtr_get_mime_type_icon(DirectoryMimeType) : nullptr);
}

#else

Glib::RefPtr<TorrentIconType> make_torrent_icon(Glib::RefPtr<Gio::Icon> mime_icon, bool is_folder)
{
    if (mime_icon == nullptr || !is_folder) {
        return mime_icon;
    }

    auto const folder_icon = gtr_get_mime_type_icon(DirectoryMimeType);
    if (folder_icon == nullptr) {
        return mime_icon;
    }

    // gtk3 composites the emblem when it loads the icon; where it lands is the
    // icon theme's choice, not ours.
    //
    // Built through the C constructor because Gio::EmblemedIcon::create() passes a
    // construct property named "icon" and GEmblemedIcon's is "gicon", so the base
    // icon is dropped and every later lookup of the result fails on a null GIcon.
    return Glib::wrap(G_ICON(g_emblemed_icon_new(mime_icon->gobj(), Gio::Emblem::create(folder_icon)->gobj())));
}

#endif

} // namespace

Glib::RefPtr<TorrentIconType> gtr_get_magnet_icon()
{
    static auto const icon = make_torrent_icon(Gio::ThemedIcon::create("magnet"), false);
    return icon;
}

Glib::RefPtr<TorrentIconType> gtr_get_torrent_icon(std::string_view mime_type, bool is_folder)
{
    using TorrentIconCache = std::map<std::string, Glib::RefPtr<TorrentIconType>, std::less<>>;
    static auto folder_cache = TorrentIconCache{};
    static auto file_cache = TorrentIconCache{};

    auto& cache = is_folder ? folder_cache : file_cache;
    if (auto const it = cache.find(mime_type); it != std::end(cache)) {
        return it->second;
    }

    auto icon = make_torrent_icon(gtr_get_mime_type_icon(mime_type), is_folder);
    if (icon != nullptr) {
        cache.try_emplace(std::string{ mime_type }, icon);
    }

    return icon;
}
