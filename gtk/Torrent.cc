// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include "Torrent.h"

#include "DynamicPropertyStore.h"
#include "IconCache.h"
#include "Utils.h"

#include <libtransmission/transmission.h>
#include <libtransmission/utils.h>
#include <libtransmission/values.h>

#include <glibmm/i18n.h>
#include <glibmm/value.h>

#include <fmt/format.h>

#include <array>
#include <memory>
#include <string_view>
#include <utility>

using namespace std::string_view_literals;

using Shared = tr::app::Torrent;

namespace
{

template<typename T>
Glib::Value<T>& column_value_cast(Glib::ValueBase& value, Gtk::TreeModelColumn<T> const& /*column*/)
{
    return static_cast<Glib::Value<T>&>(value);
}

[[nodiscard]] std::string_view get_activity_direction(tr_torrent_activity activity)
{
    switch (activity) {
    case TR_STATUS_DOWNLOAD:
        return "down"sv;
    case TR_STATUS_SEED:
        return "up"sv;
    default:
        return "idle"sv;
    }
}

// --- the fields each piece of text reads

auto constexpr HasMetadataFields = Shared::metadata_percent_complete_fields();

auto constexpr ShortTransferFields = HasMetadataFields |
    Shared::active_peers_down_fields() |
    Shared::fields<TR_KEY_is_stalled, TR_KEY_peers_getting_from_us, TR_KEY_rate_download, TR_KEY_rate_upload>();

auto constexpr ShortStatusFields = ShortTransferFields |
    Shared::recheck_progress_fields() |
    Shared::fields<TR_KEY_is_finished, TR_KEY_status, TR_KEY_upload_ratio>();

auto constexpr LongProgressFields = Shared::effective_seed_ratio_fields() |
    Shared::have_total_fields() |
    Shared::is_seed_fields() |
    Shared::percent_complete_fields() |
    Shared::percent_done_fields() |
    Shared::fields<TR_KEY_eta,
                   TR_KEY_left_until_done,
                   TR_KEY_size_when_done,
                   TR_KEY_status,
                   TR_KEY_total_size,
                   TR_KEY_upload_ratio,
                   TR_KEY_uploaded_ever>();

auto constexpr LongStatusFields = ShortStatusFields |
    Shared::fields<TR_KEY_error, TR_KEY_error_string, TR_KEY_peers_connected, TR_KEY_peers_sending_to_us>();

} // namespace

Torrent::Columns::Columns()
{
    add(self);
    add(name_collated);
}

class Torrent::Impl
{
public:
    // NOLINTNEXTLINE(performance-enum-size): must be guint to match glib API
    enum class Property : guint {
        ICON = 1,
        NAME,
        PERCENT_DONE,
        PERCENT_DONE_TEXT,
        SHORT_STATUS,
        LONG_PROGRESS,
        LONG_STATUS,
        SENSITIVE,
        CSS_CLASSES,

        N_PROPS
    };

    using PropertyStore = DynamicPropertyStore<Torrent, Property>;

    Impl(Torrent& torrent, Shared const& shared);

    [[nodiscard]] Shared const& shared() const noexcept
    {
        return *shared_;
    }

    [[nodiscard]] Glib::ustring const& name_collated() const noexcept
    {
        return name_collated_;
    }

    [[nodiscard]] std::string const& name_collate_key() const noexcept
    {
        return name_collate_key_;
    }

    void update(Fields const& fields);
    void detach();

    void get_value(int column, Glib::ValueBase& value) const;

    [[nodiscard]] Glib::RefPtr<TorrentIconType> get_icon() const;
    [[nodiscard]] Glib::ustring get_short_status_text() const;
    [[nodiscard]] Glib::ustring get_long_progress_text() const;
    [[nodiscard]] Glib::ustring get_long_status_text() const;
    [[nodiscard]] std::vector<Glib::ustring> get_css_classes() const;

    static void class_init(void* cls, void* user_data);

private:
    void update_names();
    void notify_property_changes(Fields const& fields) const;

    [[nodiscard]] Glib::ustring get_short_transfer_text() const;
    [[nodiscard]] Glib::ustring get_error_text() const;
    [[nodiscard]] Glib::ustring get_activity_text() const;

    Torrent& torrent_;
    Shared const* shared_;
    std::unique_ptr<Shared const> detached_; // set by `detach()`

    Glib::ustring name_collated_;
    std::string name_collate_key_;
};

Torrent::Impl::Impl(Torrent& torrent, Shared const& shared)
    : torrent_{ torrent }
    , shared_{ &shared }
{
    update_names();
}

void Torrent::Impl::update(Fields const& fields)
{
    if ((fields & NameFields).any()) {
        update_names();
    }

    notify_property_changes(fields);
}

void Torrent::Impl::detach()
{
    if (!detached_) {
        detached_ = std::make_unique<Shared const>(*shared_);
        shared_ = detached_.get();
    }
}

void Torrent::Impl::update_names()
{
    name_collated_ = fmt::format("{}\t{}", Glib::ustring{ shared().name() }.lowercase(), shared().hash_string().sv());
    name_collate_key_ = name_collated_.collate_key();
}

void Torrent::Impl::notify_property_changes(Fields const& fields) const
{
    static auto constexpr PropertiesFields = std::to_array<std::pair<Property, Fields>>({
        { Property::ICON, HasMetadataFields | Shared::fields<TR_KEY_is_folder, TR_KEY_primary_mime_type>() },
        { Property::NAME, Shared::fields<TR_KEY_name>() },
        { Property::PERCENT_DONE, Shared::activity_percent_done_fields() },
        { Property::PERCENT_DONE_TEXT, Shared::activity_percent_done_fields() },
        { Property::SHORT_STATUS, ShortStatusFields },
        { Property::LONG_PROGRESS, LongProgressFields },
        { Property::LONG_STATUS, LongStatusFields },
        { Property::SENSITIVE, Shared::fields<TR_KEY_status>() },
        { Property::CSS_CLASSES, Shared::fields<TR_KEY_error, TR_KEY_status>() },
    });
    static_assert(std::size(PropertiesFields) == PropertyStore::PropertyCount - 1);

#if GTKMM_CHECK_VERSION(4, 0, 0)

    auto& properties = PropertyStore::get();

    for (auto const& [property, property_fields] : PropertiesFields) {
        if ((fields & property_fields).any()) {
            properties.notify_changed(torrent_, property);
        }
    }

#else

    static auto constexpr AnyPropertyFields = [] {
        auto ret = Fields{};
        for (auto const& [property, property_fields] : PropertiesFields) {
            ret |= property_fields;
        }
        return ret;
    }();

    // Reduce redraws by emitting non-detailed signal once for all changes
    if ((fields & AnyPropertyFields).any()) {
        gtr_object_notify_emit(torrent_);
    }

#endif
}

void Torrent::Impl::get_value(int column, Glib::ValueBase& value) const
{
    static auto const& columns = get_columns();

    if (column == columns.self.index()) {
        column_value_cast(value, columns.self).set(&torrent_);
    } else if (column == columns.name_collated.index()) {
        column_value_cast(value, columns.name_collated).set(name_collated_);
    }
}

Glib::RefPtr<TorrentIconType> Torrent::Impl::get_icon() const
{
    auto const& tor = shared();
    return tor.has_metadata() ? gtr_get_torrent_icon(tor.primary_mime_type().sv(), tor.is_folder()) : gtr_get_magnet_icon();
}

Glib::ustring Torrent::Impl::get_short_status_text() const
{
    auto const& tor = shared();

    switch (tor.activity()) {
    case TR_STATUS_STOPPED:
        return tor.is_finished() ? _("Seeding complete") : _("Paused");

    case TR_STATUS_CHECK_WAIT:
        return _("Queued for verification");

    case TR_STATUS_DOWNLOAD_WAIT:
        return _("Queued for download");

    case TR_STATUS_SEED_WAIT:
        return _("Queued for seeding");

    case TR_STATUS_CHECK:
        return fmt::format(
            // xgettext:no-c-format
            fmt::runtime(_("Verifying local data ({percent_done}% tested)")),
            fmt::arg("percent_done", tor.recheck_progress().to_string()));

    case TR_STATUS_DOWNLOAD:
    case TR_STATUS_SEED:
        return fmt::format(
            "{:s} {:s}",
            get_short_transfer_text(),
            fmt::format(fmt::runtime(_("Ratio: {ratio}")), fmt::arg("ratio", tor.upload_ratio().to_string())));

    default:
        return {};
    }
}

Glib::ustring Torrent::Impl::get_long_progress_text() const
{
    auto const& tor = shared();

    Glib::ustring gstr;

    auto const seed_ratio = tor.effective_seed_ratio();
    auto const seed_ratio_text = [&seed_ratio] {
        return tr::Values::Ratio::from_double(seed_ratio.value_or(0.0)).to_string();
    };

    if (!tor.is_done()) // downloading
    {
        // 50 MB of 200 MB (25%)
        gstr += fmt::format(
            fmt::runtime(_("{current_size} of {complete_size} ({percent_done}%)")),
            fmt::arg("current_size", tr_strlsize(tor.have_total())),
            fmt::arg("complete_size", tr_strlsize(tor.size_when_done())),
            fmt::arg("percent_done", tor.percent_done().to_string()));
    } else if (!tor.is_seed() && seed_ratio) // partial seed, seed ratio
    {
        // 50 MB of 200 MB (25%), uploaded 30 MB (Ratio: X%, Goal: Y%)
        gstr += fmt::format(
            // xgettext:no-c-format
            fmt::runtime(_(
                "{current_size} of {complete_size} ({percent_complete}%), uploaded {uploaded_size} (Ratio: {ratio}, Goal: {seed_ratio})")),
            fmt::arg("current_size", tr_strlsize(tor.have_total())),
            fmt::arg("complete_size", tr_strlsize(tor.total_size())),
            fmt::arg("percent_complete", tor.percent_complete().to_string()),
            fmt::arg("uploaded_size", tr_strlsize(tor.uploaded_ever())),
            fmt::arg("ratio", tor.upload_ratio().to_string()),
            fmt::arg("seed_ratio", seed_ratio_text()));
    } else if (!tor.is_seed()) // partial seed, no seed ratio
    {
        gstr += fmt::format(
            // xgettext:no-c-format
            fmt::runtime(
                _("{current_size} of {complete_size} ({percent_complete}%), uploaded {uploaded_size} (Ratio: {ratio})")),
            fmt::arg("current_size", tr_strlsize(tor.have_total())),
            fmt::arg("complete_size", tr_strlsize(tor.total_size())),
            fmt::arg("percent_complete", tor.percent_complete().to_string()),
            fmt::arg("uploaded_size", tr_strlsize(tor.uploaded_ever())),
            fmt::arg("ratio", tor.upload_ratio().to_string()));
    } else if (seed_ratio) // seed, seed ratio
    {
        gstr += fmt::format(
            fmt::runtime(_("{complete_size}, uploaded {uploaded_size} (Ratio: {ratio}, Goal: {seed_ratio})")),
            fmt::arg("complete_size", tr_strlsize(tor.total_size())),
            fmt::arg("uploaded_size", tr_strlsize(tor.uploaded_ever())),
            fmt::arg("ratio", tor.upload_ratio().to_string()),
            fmt::arg("seed_ratio", seed_ratio_text()));
    } else // seed, no seed ratio
    {
        gstr += fmt::format(
            fmt::runtime(_("{complete_size}, uploaded {uploaded_size} (Ratio: {ratio})")),
            fmt::arg("complete_size", tr_strlsize(tor.total_size())),
            fmt::arg("uploaded_size", tr_strlsize(tor.uploaded_ever())),
            fmt::arg("ratio", tor.upload_ratio().to_string()));
    }

    // add time remaining when applicable
    if (tor.activity() == TR_STATUS_DOWNLOAD || (seed_ratio && tor.activity() == TR_STATUS_SEED)) {
        gstr += " - ";

        if (tor.eta() < 0) {
            gstr += _("Remaining time unknown");
        } else {
            gstr += tr_format_time_left(tor.eta());
        }
    }

    return gstr;
}

Glib::ustring Torrent::Impl::get_long_status_text() const
{
    auto status_str = get_error_text();
    if (status_str.empty()) {
        status_str = get_activity_text();
    }

    switch (shared().activity()) {
    case TR_STATUS_CHECK_WAIT:
    case TR_STATUS_CHECK:
    case TR_STATUS_DOWNLOAD_WAIT:
    case TR_STATUS_SEED_WAIT:
    case TR_STATUS_STOPPED:
        break;

    default:
        if (auto const buf = get_short_transfer_text(); !std::empty(buf)) {
            status_str += fmt::format(" - {:s}", buf);
        }
    }

    return status_str;
}

std::vector<Glib::ustring> Torrent::Impl::get_css_classes() const
{
    auto result = std::vector<Glib::ustring>({
        fmt::format("tr-transfer-{}", get_activity_direction(shared().activity())),
    });

    if (shared().has_error()) {
        result.emplace_back("tr-error");
    }

    return result;
}

void Torrent::Impl::class_init(void* cls, void* /*user_data*/)
{
    PropertyStore::get().install(
        G_OBJECT_CLASS(cls),
        {
            { Property::ICON, "icon", "Icon", "Icon for the torrent's likely contents", &Torrent::get_icon },
            { Property::NAME, "name", "Name", "Torrent name / title", &Torrent::get_name },
            { Property::PERCENT_DONE,
              "percent-done",
              "Percent done",
              "Percent done (0..1) for current activity (leeching or seeding)",
              &Torrent::get_percent_done_fraction },
            { Property::PERCENT_DONE_TEXT,
              "percent-done-text",
              "Percent done text",
              "Percent done label for the progress bar in compact view mode",
              &Torrent::get_percent_done_text },
            { Property::SHORT_STATUS,
              "short-status",
              "Short status",
              "Status text displayed in compact view mode",
              &Torrent::get_short_status_text },
            { Property::LONG_PROGRESS,
              "long-progress",
              "Long progress",
              "Progress text displayed in full view mode",
              &Torrent::get_long_progress_text },
            { Property::LONG_STATUS,
              "long-status",
              "Long status",
              "Status text displayed in full view mode",
              &Torrent::get_long_status_text },
            { Property::SENSITIVE,
              "sensitive",
              "Sensitive",
              "Visual sensitivity of the view item, unrelated to activation possibility",
              &Torrent::get_sensitive },
            { Property::CSS_CLASSES,
              "css-classes",
              "CSS classes",
              "CSS class names used for styling view items",
              &Torrent::get_css_classes },
        });
}

Glib::ustring Torrent::Impl::get_short_transfer_text() const
{
    auto const& tor = shared();

    if (tor.has_metadata() && tor.active_peers_down() > 0) {
        return fmt::format("{} ▾  {} ▴", tor.download_speed().to_string(), tor.upload_speed().to_string());
    }

    if (tor.has_metadata() && tor.peers_getting_from_us() > 0) {
        return fmt::format("{} ▴", tor.upload_speed().to_string());
    }

    if (tor.is_stalled()) {
        return _("Stalled");
    }

    return {};
}

Glib::ustring Torrent::Impl::get_error_text() const
{
    auto const& tor = shared();
    auto const message = tor.error_string().sv();

    switch (tor.error_code()) {
    case tr_stat::Error::TrackerWarning:
        return fmt::format(fmt::runtime(_("Tracker warning: '{warning}'")), fmt::arg("warning", message));

    case tr_stat::Error::TrackerError:
        return fmt::format(fmt::runtime(_("Tracker error: '{error}'")), fmt::arg("error", message));

    case tr_stat::Error::LocalError:
        return fmt::format(fmt::runtime(_("Local error: '{error}'")), fmt::arg("error", message));

    default:
        return {};
    }
}

Glib::ustring Torrent::Impl::get_activity_text() const
{
    auto const& tor = shared();
    auto const peers_connected = tor.peers_connected();
    auto const peers_sending_to_us = tor.peers_sending_to_us();
    auto const webseeds_sending_to_us = tor.webseeds_sending_to_us();

    switch (tor.activity()) {
    case TR_STATUS_STOPPED:
    case TR_STATUS_CHECK_WAIT:
    case TR_STATUS_CHECK:
    case TR_STATUS_DOWNLOAD_WAIT:
    case TR_STATUS_SEED_WAIT:
        return get_short_status_text();

    case TR_STATUS_DOWNLOAD:
        if (!tor.has_metadata()) {
            return fmt::format(
                fmt::runtime(ngettext(
                    // xgettext:no-c-format
                    "Downloading metadata from {active_count:L} connected peer ({percent_done}% done)",
                    "Downloading metadata from {active_count:L} connected peers ({percent_done}% done)",
                    peers_connected)),
                fmt::arg("active_count", peers_connected),
                fmt::arg("percent_done", tor.metadata_percent_complete().to_string()));
        }

        if (peers_sending_to_us != 0 && webseeds_sending_to_us != 0) {
            return fmt::format(
                fmt::runtime(ngettext(
                    "Downloading from {active_count:L} of {connected_count:L} connected peer and webseed",
                    "Downloading from {active_count:L} of {connected_count:L} connected peers and webseeds",
                    peers_connected + webseeds_sending_to_us)),
                fmt::arg("active_count", peers_sending_to_us + webseeds_sending_to_us),
                fmt::arg("connected_count", peers_connected + webseeds_sending_to_us));
        }

        if (webseeds_sending_to_us != 0) {
            return fmt::format(
                fmt::runtime(ngettext(
                    "Downloading from {active_count:L} webseed",
                    "Downloading from {active_count:L} webseeds",
                    webseeds_sending_to_us)),
                fmt::arg("active_count", webseeds_sending_to_us));
        }

        return fmt::format(
            fmt::runtime(ngettext(
                "Downloading from {active_count:L} of {connected_count:L} connected peer",
                "Downloading from {active_count:L} of {connected_count:L} connected peers",
                peers_connected)),
            fmt::arg("active_count", peers_sending_to_us),
            fmt::arg("connected_count", peers_connected));

    case TR_STATUS_SEED:
        return fmt::format(
            fmt::runtime(ngettext(
                "Seeding to {active_count:L} of {connected_count:L} connected peer",
                "Seeding to {active_count:L} of {connected_count:L} connected peers",
                peers_connected)),
            fmt::arg("active_count", tor.peers_getting_from_us()),
            fmt::arg("connected_count", peers_connected));

    default:
        g_assert_not_reached();
        return {};
    }
}

Torrent::Torrent()
    : Glib::ObjectBase(typeid(Torrent))
    , ExtraClassInit(&Impl::class_init)
{
}

Torrent::Torrent(Shared const& torrent)
    : Glib::ObjectBase(typeid(Torrent))
    , ExtraClassInit(&Impl::class_init)
    , impl_(std::make_unique<Impl>(*this, torrent))
{
}

Shared const& Torrent::get_shared() const noexcept
{
    return impl_->shared();
}

tr_torrent_id_t Torrent::get_id() const noexcept
{
    return impl_->shared().id();
}

Glib::ustring Torrent::get_name() const
{
    return Glib::ustring{ impl_->shared().name() };
}

Glib::ustring const& Torrent::get_name_collated() const noexcept
{
    return impl_->name_collated();
}

std::string const& Torrent::get_name_collate_key() const noexcept
{
    return impl_->name_collate_key();
}

Glib::RefPtr<TorrentIconType> Torrent::get_icon() const
{
    return impl_->get_icon();
}

Torrent::Percent Torrent::get_percent_done() const
{
    return impl_->shared().activity_percent_done();
}

float Torrent::get_percent_done_fraction() const
{
    return static_cast<float>(get_percent_done().to_fraction());
}

Glib::ustring Torrent::get_percent_done_text() const
{
    return fmt::format(fmt::runtime(_("{percent}%")), fmt::arg("percent", get_percent_done().to_whole_string()));
}

Glib::ustring Torrent::get_short_status_text() const
{
    return impl_->get_short_status_text();
}

Glib::ustring Torrent::get_long_progress_text() const
{
    return impl_->get_long_progress_text();
}

Glib::ustring Torrent::get_long_status_text() const
{
    return impl_->get_long_status_text();
}

bool Torrent::get_sensitive() const
{
    return !impl_->shared().is_paused();
}

std::vector<Glib::ustring> Torrent::get_css_classes() const
{
    return impl_->get_css_classes();
}

void Torrent::update(Fields const& fields)
{
    impl_->update(fields);
}

void Torrent::detach()
{
    impl_->detach();
}

Glib::RefPtr<Torrent> Torrent::create(Shared const& torrent)
{
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
    return Glib::make_refptr_for_instance(new Torrent(torrent));
}

Torrent::Columns const& Torrent::get_columns()
{
    static Columns const columns;
    return columns;
}

int Torrent::get_item_id(Glib::RefPtr<Glib::ObjectBase const> const& item)
{
    if (auto const torrent = gtr_ptr_dynamic_cast<Torrent const>(item); torrent != nullptr) {
        return torrent->get_id();
    }

    return 0;
}

void Torrent::get_item_value(Glib::RefPtr<Glib::ObjectBase const> const& item, int column, Glib::ValueBase& value)
{
    if (auto const torrent = gtr_ptr_dynamic_cast<Torrent const>(item); torrent != nullptr) {
        torrent->impl_->get_value(column, value);
    }
}
