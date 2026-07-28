/*
 * icons.[ch] written by Paolo Bacchilega, who writes:
 * "There is no problem for me, you can license
 * my code under whatever licence you wish :)"
 *
 */

#pragma once

#include "GtkCompat.h"

#include <giomm/icon.h>

#include <glibmm/refptr.h>

#if GTKMM_CHECK_VERSION(4, 0, 0)
#include <gdkmm/paintable.h>
#endif

#include <string_view>

extern std::string_view const DirectoryMimeType;

using TorrentIconType = IF_GTKMM4(Gdk::Paintable, Gio::Icon);

Glib::RefPtr<Gio::Icon> gtr_get_mime_type_icon(std::string_view mime_type);

Glib::RefPtr<TorrentIconType> gtr_get_magnet_icon();

// The icon for a torrent whose bytes are mostly `mime_type`. When `is_folder`,
// a folder is drawn behind it so the row shows both what the torrent holds and
// that it lands in a directory.
Glib::RefPtr<TorrentIconType> gtr_get_torrent_icon(std::string_view mime_type, bool is_folder);
