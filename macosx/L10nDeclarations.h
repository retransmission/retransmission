// This file Copyright © Retransmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

// The English of every formatted string that the app looks up, in the catalog's {fmt} syntax.
// Nothing compiles this file;
// po/compile-mac-catalogs.sh writes each language's Formats.strings and Formats.stringsdict from it.
//
// The code looks up text by its key, the English in Cocoa's format syntax,
// and a plural by its plural English:
//   TR_DECLARE("Created by {creator}");
//   [NSString localizedStringWithFormat:NSLocalizedStringFromTable(@"Created by %@", @"Formats", nil), creator]
//   TR_DECLARE_N("{count:L} file", "{count:L} files");
//   [NSString localizedStringWithFormat:NSLocalizedStringFromTable(@"%lu files", @"Formats", nil), count]
// The key's specifiers follow the fields in order, and its arguments follow the specifiers:
//   {name}    %@   an NSString
//   {name:L}  %lu  an NSUInteger, which the text shows as a count
//   {name:d}  %ld  an NSInteger
// A literal % is %%. With NUMBERED_KEY, the key numbers its specifiers, as in %1$@.
// A plural's count is its {count} field, or else its last {name:L} field.

TR_DECLARE("Select the download folder for \"{torrent_name}\"");

// Translators: Add torrent -> info
// Translators: Inspector -> Activity tab -> progress
// Translators: Torrent -> progress string
TR_DECLARE("{amount} selected");

// Translators: Inspector -> Files tab -> file status string
// Translators: Status bar transfer count
// Translators: file size string
TR_DECLARE("{part} of {whole}");

// Translators: Legal alert -> title
TR_DECLARE("Welcome to {appname}");

TR_DECLARE(
    "{appname} is a file-sharing program. When you run a torrent, its data will be made available to others by means of upload. You and you alone are fully responsible for exercising proper judgement and abiding by your local laws.");

TR_DECLARE(
    "{appname} can't copy Transmission's remote access password. Until you set a new one in the Remote preferences, remote access refuses every login.");

TR_DECLARE(
    "{appname} can't copy Transmission's remote access password or save a new one, so remote access is off. Set a password in the Remote preferences, then turn remote access back on.");

TR_DECLARE(
    "{appname} can copy your settings and transfers from Transmission. The two apps will share your downloaded files, so run only one at a time.");

TR_DECLARE("{appname} can copy your settings from Transmission, but not its transfers.");

TR_DECLARE("{appname} is already running.");

TR_DECLARE("There is already a copy of {appname} running. This copy cannot be opened until that instance is quit.");

TR_DECLARE("Quit Transmission before opening {appname}. The two apps would work on the same downloads.");

TR_DECLARE(
    "{appname} is a full-featured torrent application. A lot of time and effort have gone into development, coding, and refinement. If you enjoy using it, please consider showing your love with a donation.");

TR_DECLARE_N(
    "There is {count:L} active torrent that will be paused on quit. The torrent will start again automatically on the next launch.",
    "There are {count:L} active torrents that will be paused on quit. The torrents will start again automatically on the next launch.");

TR_DECLARE("{appname} will quit, because the two apps would work on the same downloads.");

TR_DECLARE("It appears that the file \"{filename}\" from {url} is not a torrent file.");

TR_DECLARE("The torrent could not be downloaded from {url}: {error}.");

TR_DECLARE("\"{filename}\" is not a valid torrent file.");

TR_DECLARE("There was an error when adding the magnet link \"{magnet_link}\". The torrent will not be added.");

TR_DECLARE("A torrent for \"{torrent_name}\" already exists.");

TR_DECLARE("The magnet link \"{magnet_link}\" cannot be added because it is a duplicate of an already existing torrent.");

TR_DECLARE("Are you sure you want to remove \"{torrent_name}\" from the torrent list and trash the data file?");

TR_DECLARE("Are you sure you want to remove \"{torrent_name}\" from the torrent list?");

TR_DECLARE_N(
    "Are you sure you want to remove {count:L} torrent from the torrent list and trash the data file?",
    "Are you sure you want to remove {count:L} torrents from the torrent list and trash the data files?");

TR_DECLARE_N(
    "Are you sure you want to remove {count:L} torrent from the torrent list?",
    "Are you sure you want to remove {count:L} torrents from the torrent list?");

TR_DECLARE_N("There is {count:L} active torrent.", "There are {count:L} active torrents.");

TR_DECLARE_N(
    "There is {count:L} torrent ({active_count:L} active).",
    "There are {count:L} torrents ({active_count:L} active).");

TR_DECLARE_N(
    "Are you sure you want to remove {count:L} completed torrent from the torrent list?",
    "Are you sure you want to remove {count:L} completed torrents from the torrent list?");

TR_DECLARE("Select the new folder for \"{torrent_name}\".");

TR_DECLARE_N("Select the new folder for {count:L} data file.", "Select the new folder for {count:L} data files.");

TR_DECLARE("Copy of \"{torrent_name}\" Cannot Be Created");

TR_DECLARE("The torrent file ({path}) cannot be found.");

// Translators: Dock item - Seeding
TR_DECLARE("{count:L} Seeding");

// Translators: Dock item - Downloading
TR_DECLARE("{count:L} Downloading");

// Translators: Create torrent -> info
TR_DECLARE_N("{count:L} piece, {size}", "{count:L} pieces, {size} each");

TR_DECLARE(
    "The directory \"{directory}\" does not currently exist. Create this directory or choose a different one to create the torrent file.");

TR_DECLARE(
    "A file with the name \"{filename}\" already exists in the directory \"{directory}\". Choose a new name or directory to create the torrent file.");

TR_DECLARE("Creation of \"{filename}\" failed.");

// Translators: Drag overlay -> torrents
TR_DECLARE_N("{count:L} Torrent File", "{count:L} Torrent Files");

TR_DECLARE("Rename the file \"{filename}\":");

// Translators: Inspector -> Activity tab -> have
TR_DECLARE("{size} verified");

// Translators: Info options -> global setting
TR_DECLARE_N("{minutes:L} minute", "{minutes:L} minutes");

// Translators: Inspector -> Peers tab -> peers
TR_DECLARE_N("{count:L} Connected", "{count:L} Connected");

// Translators: Inspector -> Peers tab -> peers
TR_DECLARE("DL from {count:L}");

// Translators: Inspector -> Peers tab -> peers
TR_DECLARE("UL to {count:L}");

// Translators: Inspector -> Peers tab -> peers
TR_DECLARE("{count:L} Known:");

// Translators: Inspector -> Peers tab -> peers
TR_DECLARE("{count:L} tracker");

// Translators: Inspector -> Peers tab -> peers
TR_DECLARE("{count:L} incoming");

// Translators: Inspector -> Peers tab -> peers
TR_DECLARE("{count:L} cache");

// Translators: Inspector -> Peers tab -> peers
TR_DECLARE("{count:L} local discovery");

// Translators: Inspector -> Peers tab -> peers
TR_DECLARE("{count:L} PEX");

// Translators: Inspector -> Peers tab -> peers
TR_DECLARE("{count:L} DHT");

// Translators: Inspector -> Peers tab -> peers
TR_DECLARE("{count:L} LTEP");

// Translators: Inspector -> Peers tab -> table row tooltip
TR_DECLARE("Progress: {percent}");

// Translators: Inspector -> Peers tab -> table row tooltip
TR_DECLARE("Protocol: {protocol}");

// Translators: Inspector -> Peers tab -> table row tooltip
TR_DECLARE("Port: {port}");

// Translators: Inspector -> tracker table
TR_DECLARE("Tier {tier:d}");

TR_DECLARE_N("Are you sure you want to remove {count:L} tracker?", "Are you sure you want to remove {count:L} trackers?");

TR_DECLARE("Once removed, {appname} will no longer attempt to contact them. This cannot be undone.");

TR_DECLARE("Once removed, {appname} will no longer attempt to contact it. This cannot be undone.");

// Translators: Inspector -> selected torrents
TR_DECLARE_N("{count:L} Torrent Selected", "{count:L} Torrents Selected");

// Translators: Inspector -> selected torrents
TR_DECLARE_N("{count:L} magnetized torrent", "{count:L} magnetized torrents");

// Translators: Inspector -> selected torrents
// Translators: stats total
TR_DECLARE("{amount} total");

TR_DECLARE("There was a problem creating the file \"{filename}\".");

TR_DECLARE_N("{file_count:L} file", "{file_count:L} files");

TR_DECLARE_N("{count:L} torrent", "{count:L} torrents");

TR_DECLARE_N("{count:L} IP address rule in list", "{count:L} IP address rules in list");

TR_DECLARE("This will clear the global statistics displayed by {appname}. Individual torrent statistics will not be affected.");

// Translators: stats window -> times opened
TR_DECLARE_N("{count:L} time", "{count:L} times");

TR_DECLARE("Down: {downloaded_size}, Up: {uploaded_size}");

// Translators: Status Bar -> speed tooltip
TR_DECLARE("{speed:L} KB/s");

TR_DECLARE("The move operation of \"{torrent_name}\" cannot be done.");

TR_DECLARE("Not enough remaining disk space to download \"{torrent_name}\" completely.");

TR_DECLARE("The torrent will be paused. Clear up space on {volume} or deselect files in the torrent inspector to continue.");

TR_DECLARE("{percent} of torrent metadata retrieved");

TR_DECLARE("uploaded {uploaded_size} (Ratio: {ratio})");

TR_DECLARE_N(
    "Downloading from {active_count:L} of {connected_count:L} peer",
    "Downloading from {active_count:L} of {connected_count:L} peers");

// Translators: Torrent -> status string
TR_DECLARE_N("{count:L} web seed", "{count:L} web seeds");

TR_DECLARE_N(
    "Seeding to {active_count:L} of {connected_count:L} peer",
    "Seeding to {active_count:L} of {connected_count:L} peers");

// Its key numbers its specifiers, "Down: %1$@, Up: %2$@",
// because "Down: {downloaded_size}, Up: {uploaded_size}" has the same English for amounts rather than speeds,
// which a language may word differently.
TR_DECLARE("Down: {download_speed}, Up: {upload_speed}", NUMBERED_KEY);

// Translators: Torrent -> status string
TR_DECLARE("Up: {upload_speed}");

TR_DECLARE("Ratio: {ratio}, Up: {upload_speed}");

// Translators: Tracker last announce
TR_DECLARE_N("got {count:L} peer", "got {count:L} peers");

TR_DECLARE("Next announce in {time_span}");
