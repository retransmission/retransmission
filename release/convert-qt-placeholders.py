#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Rewrites Qt translations for messages whose placeholders moved
# from QString::arg() markers such as %1 and %Ln to {fmt} named fields.
#
# Each CONVERSIONS entry gives a message's context, its old English text,
# and its new English text, which may differ only in placeholders.
# Aligning the two gives the field that replaces each marker.
# The script applies that mapping to the message's <source>, <translation>,
# and <numerusform> texts in every qt/translations/*.ts file.
# It doubles literal braces, so {fmt} prints them as they are.
# The message's <extracomment> gets the same fields, matching the code's translator comment.
# Everything else, including each translation's status, stays as it was.
#
# A marker that the old English text lacks stays literal text,
# which is what QString::arg() showed.
# A literal {field} that the new English text uses becomes that field.
# The script prints both cases.
#
# lupdate extracts the new messages only with the TR_FORMAT aliases in qt/TrFormat.h.
#
# Usage: convert-qt-placeholders.py [TS_FILE]...
# With no arguments, it converts every file in qt/translations/.

import html
import pathlib
import re
import sys

CONVERSIONS = [
    ('DetailsDialog', '%1 (100%)', '{current_size} (100%)'),
    ('DetailsDialog', '%1 of %2 (%3%)', '{current_size} of {complete_size} ({percent_done}%)'),
    ('DetailsDialog', '%1 of %2 (%3%), %4 Unverified',
     '{current_size} of {complete_size} ({percent_done}%), {unverified_size} Unverified'),
    ('DetailsDialog', '%1 (+%2 discarded after failed checksum)',
     '{downloaded_size} (+{discarded_size} discarded after failed checksum)'),
    ('DetailsDialog', '%1 (Ratio: %2)', '{uploaded_size} (Ratio: {ratio})'),
    ('DetailsDialog', '%1 ago', '{time_span} ago'),
    ('DetailsDialog', '%1 (%Ln pieces @ %2)', '{total_size} ({piece_count:L} pieces @ {piece_size})'),
    ('DetailsDialog', '%1 (%Ln pieces)', '{total_size} ({piece_count:L} pieces)'),
    ('DetailsDialog', 'Created by %1', 'Created by {creator}'),
    ('DetailsDialog', 'Created on %1', 'Created on {date}'),
    ('DetailsDialog', 'Created by %1 on %2', 'Created by {creator} on {date}'),
    ('DetailsDialog', '%1 minute(s)', '{minutes:L} minute(s)'),
    ('Formatter', '%Ln second(s)', '{seconds:L} second(s)'),
    ('Formatter', '%Ln minute(s)', '{minutes:L} minute(s)'),
    ('Formatter', '%Ln hour(s)', '{hours:L} hour(s)'),
    ('Formatter', '%Ln day(s)', '{days:L} day(s)'),
    ('FreeSpaceLabel', '%1 free', '{disk_space} free'),
    ('MainWindow', 'Limited at %1', 'Limited at {speed}'),
    ('MainWindow', 'Stop at Ratio (%1)', 'Stop at Ratio ({ratio})'),
    ('MainWindow', ' - %1:%2', ' - {host}:{port}'),
    ('MainWindow', 'Down: %1, Up: %2', 'Down: {downloaded_size}, Up: {uploaded_size}'),
    ('MainWindow', 'Ratio: %1', 'Ratio: {ratio}'),
    ('MainWindow', 'Showing %L1 of %Ln torrent(s)', 'Showing {visible_count:L} of {count:L} torrent(s)'),
    ('MainWindow', 'Click to disable Alternative Speed Limits\n (%1 down, %2 up)',
     'Click to disable Alternative Speed Limits\n ({download_speed} down, {upload_speed} up)'),
    ('MainWindow', 'Click to enable Alternative Speed Limits\n (%1 down, %2 up)',
     'Click to enable Alternative Speed Limits\n ({download_speed} down, {upload_speed} up)'),
    ('MainWindow', 'Remove %Ln torrent(s)?', 'Remove {count:L} torrent(s)?'),
    ('MainWindow', "Delete these %Ln torrent(s)' downloaded files?", "Delete these {count:L} torrent(s)' downloaded files?"),
    ('MainWindow', '%1 has not responded yet', '{host} has not responded yet'),
    ('MainWindow', '%1 is responding', '{host} is responding'),
    ('MainWindow', '%1 last responded %2 ago', '{host} last responded {time_span} ago'),
    ('MainWindow', '%1 is not responding', '{host} is not responding'),
    ('MakeDialog', '%Ln File(s)', '{file_count:L} File(s)'),
    ('MakeDialog', '%Ln Piece(s)', '{piece_count:L} Piece(s)'),
    ('MakeDialog', '%1 in %2; %3 @ %4', '{total_size} in {files}; {pieces} @ {piece_size}'),
    ('MakeProgressDialog', "Creating '%1'", "Creating '{path}'"),
    ('MakeProgressDialog', "Created '%1'", "Created '{path}'"),
    ('MakeProgressDialog', "Couldn't create '%1': %2 (%3)", "Couldn't create '{path}': {error} ({error_code})"),
    ('PrefsDialog', 'Status: <b>%1</b>', 'Status: <b>{status}</b>'),
    ('PrefsDialog', 'Status: <b>%1</b> (IPv4), <b>%2</b> (IPv6)',
     'Status: <b>{status_ipv4}</b> (IPv4), <b>{status_ipv6}</b> (IPv6)'),
    ('PrefsDialog', '<b>Update succeeded!</b><p>Blocklist now has %Ln rule(s).</p>',
     '<b>Update succeeded!</b><p>Blocklist now has {count:L} rule(s).</p>'),
    ('PrefsDialog', '%1 minute(s)', '{minutes:L} minute(s)'),
    ('PrefsDialog', '%1 minute(s) ago', '{minutes_ago:L} minute(s) ago'),
    ('PrefsDialog', '<i>Blocklist contains %Ln rule(s)</i>', '<i>Blocklist contains {count:L} rule(s)</i>'),
    ('PrefsDialog', '<i>Blocklist last updated %1</i>', '<i>Blocklist last updated {date}</i>'),
    ('Session', '<p><b>Unable to rename "%1" as "%2": %3.</b></p><p>Please correct the errors and try again.</p>',
     '<p><b>Unable to rename "{old_path}" as "{path}": {error}.</b></p><p>Please correct the errors and try again.</p>'),
    ('Session', '%1 (copy of %2)', '{torrent_name} (copy of {hash})'),
    ('Session', 'Unable to add %n duplicate torrent(s)', 'Unable to add {count} duplicate torrent(s)'),
    ('Speed', '%1 %2', '{speed} {arrow}'),
    ('StatsDialog', 'Started %Ln time(s)', 'Started {count:L} time(s)'),
    ('Torrent', 'Tracker gave a warning: %1', 'Tracker gave a warning: {warning}'),
    ('Torrent', 'Tracker gave an error: %1', 'Tracker gave an error: {error}'),
    ('Torrent', 'Error: %1', 'Error: {error}'),
    ('TorrentDelegate', 'Magnetized torrent - retrieving metadata (%1%)',
     'Magnetized torrent - retrieving metadata ({percent_done}%)'),
    ('TorrentDelegate', '%1 of %2 (%3%)', '{current_size} of {complete_size} ({percent_done}%)'),
    ('TorrentDelegate', '%1 of %2 (%3%), uploaded %4 (Ratio: %5, Goal: %6)',
     '{current_size} of {complete_size} ({percent_complete}%), uploaded {uploaded_size} (Ratio: {ratio}, Goal: {seed_ratio})'),
    ('TorrentDelegate', '%1 of %2 (%3%), uploaded %4 (Ratio: %5)',
     '{current_size} of {complete_size} ({percent_complete}%), uploaded {uploaded_size} (Ratio: {ratio})'),
    ('TorrentDelegate', '%1, uploaded %2 (Ratio: %3, Goal: %4)',
     '{complete_size}, uploaded {uploaded_size} (Ratio: {ratio}, Goal: {seed_ratio})'),
    ('TorrentDelegate', '%1, uploaded %2 (Ratio: %3)', '{complete_size}, uploaded {uploaded_size} (Ratio: {ratio})'),
    ('TorrentDelegate', ' - %1 left', ' - {time_span} left'),
    ('TorrentDelegate', 'Verifying local data (%1% tested)', 'Verifying local data ({percent_done}% tested)'),
    ('TorrentDelegate', 'Ratio: %1', 'Ratio: {ratio}'),
    ('TorrentDelegate', '%1 left', '{time_span} left'),
    ('TorrentDelegate', 'Downloading metadata from %Ln peer(s) (%1% done)',
     'Downloading metadata from {active_count:L} peer(s) ({percent_done}% done)'),
    ('TorrentDelegate', 'Downloading from %Ln peer(s)', 'Downloading from {active_count:L} peer(s)'),
    ('TorrentDelegate', 'Downloading from %1 of %Ln connected peer(s)',
     'Downloading from {active_count} of {connected_count} connected peer(s)'),
    ('TorrentDelegate', ' and %Ln web seed(s)', ' and {webseed_count:L} web seed(s)'),
    ('TorrentDelegate', 'Seeding to %Ln peer(s)', 'Seeding to {active_count:L} peer(s)'),
    ('TorrentDelegate', 'Seeding to %1 of %Ln connected peer(s)',
     'Seeding to {active_count} of {connected_count} connected peer(s)'),
    ('TrackerDelegate', 'Got a list of%1 %Ln peer(s)%2 %3 ago',
     'Got a list of{markup_begin} {peer_count:L} peer(s){markup_end} {time_span} ago'),
    ('TrackerDelegate', 'Peer list request %1timed out%2 %3 ago; will retry',
     'Peer list request {markup_begin}timed out{markup_end} {time_span} ago; will retry'),
    ('TrackerDelegate', 'Got an error %1"%2"%3 %4 ago', 'Got an error {markup_begin}"{error}"{markup_end} {time_span} ago'),
    ('TrackerDelegate', 'Asking for more peers in %1', 'Asking for more peers in {time_span}'),
    ('TrackerDelegate', 'Asking for more peers now… <small>%1</small>',
     'Asking for more peers now… <small>{time_span}</small>'),
    ('TrackerDelegate', 'Got a scrape error %1"%2"%3 %4 ago',
     'Got a scrape error {markup_begin}"{error}"{markup_end} {time_span} ago'),
    ('TrackerDelegate', 'Tracker had%1 %Ln seeder(s)%2', 'Tracker had{markup_begin} {seeder_count:L} seeder(s){markup_end}'),
    ('TrackerDelegate', ' and%1 %Ln leecher(s)%2 %3 ago',
     ' and{markup_begin} {leecher_count:L} leecher(s){markup_end} {time_span} ago'),
    ('TrackerDelegate', 'Tracker had %1no information%2 on peer counts %3 ago',
     'Tracker had {markup_begin}no information{markup_end} on peer counts {time_span} ago'),
    ('TrackerDelegate', 'Asking for peer counts in %1', 'Asking for peer counts in {time_span}'),
    ('TrackerDelegate', 'Asking for peer counts now… <small>%1</small>',
     'Asking for peer counts now… <small>{time_span}</small>'),
]

# QString::arg() markers take one or two digits; tr() fills %n and %Ln with a plural count.
MARKER_RE = re.compile(r'%L?(\d{1,2}|n)')

# Escaped braces, named fields, and any other brace.
FIELD_RE = re.compile(r'\{\{|\}\}|\{[A-Za-z_]\w*(?::[^{}]*)?\}|[{}]')

# In a translation: markers, literal text that looks like a named field, and any other brace.
TOKEN_RE = re.compile(r'%L?(\d{1,2}|n)|\{[A-Za-z_]\w*(?::[^{}]*)?\}|[{}]')

CONTEXT_RE = re.compile(r'(<context>\s*<name>)(.*?)(</name>.*?</context>)', re.S)
MESSAGE_RE = re.compile(r'<message\b[^>]*>.*?</message>', re.S)
SOURCE_RE = re.compile(r'(<source>)(.*?)(</source>)', re.S)
TRANSLATION_RE = re.compile(r'(<translation\b[^>]*>)(.*?)(</translation>)', re.S)
NUMERUSFORM_RE = re.compile(r'(<numerusform\b[^>]*>)(.*?)(</numerusform>)', re.S)
EXTRACOMMENT_RE = re.compile(r'(<extracomment>)(.*?)(</extracomment>)', re.S)


def field_mapping(old, new):
    """Return {marker id: field} for two English texts that differ only in placeholders."""
    old_literals = MARKER_RE.split(old)[::2]
    markers = MARKER_RE.findall(old)

    # The literal text between the new text's fields, with escaped braces undone.
    new_literals, fields, pos, literal = [], [], 0, ''
    for match in FIELD_RE.finditer(new):
        literal += new[pos:match.start()]
        token = match[0]
        if token in ('{{', '}}'):
            literal += token[0]
        elif len(token) > 1:
            new_literals.append(literal)
            fields.append(token)
            literal = ''
        else:
            sys.exit(f'unmatched brace in {new!r}')
        pos = match.end()
    new_literals.append(literal + new[pos:])

    if old_literals != new_literals or len(markers) != len(fields):
        sys.exit(f'{old!r} and {new!r} differ in more than placeholders')

    mapping = {}
    for marker, field in zip(markers, fields):
        if mapping.setdefault(marker, field) != field:
            sys.exit(f'{old!r} maps %{marker} to two fields')
    return mapping


def convert_text(text, mapping, fields, notes):
    """Convert one XML-escaped text from markers to fields."""
    def replace(match):
        token = match[0]
        if match[1] is not None:
            if match[1] in mapping:
                return mapping[match[1]]
            notes.append(f'kept unknown marker {token} as text')
            return token
        if token in fields:
            notes.append(f'kept literal {token} as a field')
            return token
        return token.replace('{', '{{').replace('}', '}}')

    return TOKEN_RE.sub(replace, text)


def convert_file(path, conversions, found):
    """Convert one .ts file in place. Returns the number of messages it changed."""
    text = path.read_text(encoding='utf-8')
    changed = 0

    def convert_message(context, message):
        nonlocal changed
        source_match = SOURCE_RE.search(message)
        if source_match is None or '<comment>' in message:
            return message
        key = (context, html.unescape(source_match[2]))
        if key not in conversions:
            return message

        mapping, new_source = conversions[key]
        fields = set(mapping.values())
        found.add(key)
        changed += 1

        head, tail = message[:source_match.start()], message[source_match.end():]
        notes = []
        source = convert_text(source_match[2], mapping, fields, notes)
        if html.unescape(source) != new_source:
            sys.exit(f'{path}: converting {key[1]!r} gave {source!r}')

        pattern = NUMERUSFORM_RE if 'numerus="yes"' in message.split('>', 1)[0] else TRANSLATION_RE
        tail = pattern.sub(lambda m: m[1] + convert_text(m[2], mapping, fields, notes) + m[3], tail)
        tail = EXTRACOMMENT_RE.sub(
            lambda m: m[1] + MARKER_RE.sub(lambda marker: mapping.get(marker[1], marker[0]), m[2]) + m[3], tail)
        for note in notes:
            print(f'{path.name}: {context}: {key[1]!r}: {note}')
        return head + source_match[1] + source + source_match[3] + tail

    def convert_context(match):
        context = html.unescape(match[2])
        body = MESSAGE_RE.sub(lambda m: convert_message(context, m[0]), match[3])
        return match[1] + match[2] + body

    new_text = CONTEXT_RE.sub(convert_context, text)
    if new_text != text:
        path.write_text(new_text, encoding='utf-8')
    return changed


def main(args):
    ts_dir = pathlib.Path(__file__).resolve().parent.parent / 'qt' / 'translations'
    paths = [pathlib.Path(arg) for arg in args] or sorted(ts_dir.glob('*.ts'))
    conversions = {(context, old): (field_mapping(old, new), new) for context, old, new in CONVERSIONS}

    found = set()
    for path in paths:
        print(f'{path.name}: converted {convert_file(path, conversions, found)} messages')
    for context, old in sorted(conversions.keys() - found):
        print(f'no .ts file has {context}: {old!r}')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
