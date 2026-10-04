#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later

"""Renames messages in the .po catalogs and keeps their translations.

msgmerge treats a changed msgid as a new message and marks its old translation fuzzy, which shows English.
This moves the translation to the new msgid instead.
Run it after changing the text in the code and before msgmerge:

  fold-catalog-msgids.py [PO_FILE]...

With no arguments, it changes every file in po/.
Running it again changes nothing, because a catalog without a rename's old message is left alone.

In each catalog that has the old message of a rename in RENAMES:
  - Without the new message, the old one takes the new msgid where it stands.
  - With the new message already translated, that translation stays and the old message goes away.
  - Otherwise the old message's translation moves to the new message.
"""

import collections
import pathlib
import re
import sys

# Each rename has:
#   'from':    the old message: {'msgid': ..., 'msgctxt': ..., 'msgid_plural': ...}; only msgid is required
#   'to':      the new message, in the same form
# and may have, for the translation that moves:
#   'prefer':  'from' has the old message's translation win over the new message's
#   'keep':    True leaves the old message in place, for a client that still uses it;
#              its translation is copied to the new message if that has none
#   'strip':   text to take out, e.g. ['<i>', '</i>']
#   'replace': [[old, new], ...] text to replace
#   'fields':  {old: new} for {fmt} fields, e.g. {'count': 'piece_count:L'}
#   'colon':   True adds the colon that the catalog's other labels end with
#   'append':  text to add at the end, unless it is there already
#   'rstrip':  characters to take off the end, e.g. '.。'
#   'period':  True adds the period that the catalog's other sentences end with
#   'remove_prefix': text that the translation must start with and that is taken off; a translation without it is dropped
RENAMES = [
    # The Qt client's New Torrent dialog takes the GTK client's tracker-list help.
    {
        'from': {
            'msgid': 'To add a backup URL, add it on the line after the primary URL.\n'
            'To add another primary URL, add it after a blank line.'
        },
        'to': {
            'msgid': 'To add a backup URL, add it on the next line after a primary URL.\n'
            'To add a new primary URL, add it after a blank line.'
        },
    },
    # "Adding" has one meaning, so it needs no context. The Mac client's Preferences tab uses the same text.
    {'from': {'msgctxt': 'Gerund', 'msgid': 'Adding'}, 'to': {'msgid': 'Adding'}, 'prefer': 'from'},
    # The Qt client's "Have:" text for a finished torrent takes the GTK client's, which has the 100 as a field.
    {
        'from': {'msgid': '{current_size} (100%)'},
        'to': {'msgid': '{current_size} ({percent_done}%)'},
        'replace': [['100', '{percent_done}']],
    },
    # The peer counts in the status line are "L" fields in every client, and the Mac client says "connected" too.
    {
        'from': {
            'msgid': 'Downloading from {active_count} of {connected_count} connected peer',
            'msgid_plural': 'Downloading from {active_count} of {connected_count} connected peers',
        },
        'to': {
            'msgid': 'Downloading from {active_count:L} of {connected_count:L} connected peer',
            'msgid_plural': 'Downloading from {active_count:L} of {connected_count:L} connected peers',
        },
        'fields': {'active_count': 'active_count:L', 'connected_count': 'connected_count:L'},
    },
    {
        'from': {
            'msgid': 'Downloading from {active_count:L} of {connected_count:L} peer',
            'msgid_plural': 'Downloading from {active_count:L} of {connected_count:L} peers',
        },
        'to': {
            'msgid': 'Downloading from {active_count:L} of {connected_count:L} connected peer',
            'msgid_plural': 'Downloading from {active_count:L} of {connected_count:L} connected peers',
        },
    },
    {
        'from': {
            'msgid': 'Seeding to {active_count} of {connected_count} connected peer',
            'msgid_plural': 'Seeding to {active_count} of {connected_count} connected peers',
        },
        'to': {
            'msgid': 'Seeding to {active_count:L} of {connected_count:L} connected peer',
            'msgid_plural': 'Seeding to {active_count:L} of {connected_count:L} connected peers',
        },
        'fields': {'active_count': 'active_count:L', 'connected_count': 'connected_count:L'},
    },
    {
        'from': {
            'msgid': 'Seeding to {active_count:L} of {connected_count:L} peer',
            'msgid_plural': 'Seeding to {active_count:L} of {connected_count:L} peers',
        },
        'to': {
            'msgid': 'Seeding to {active_count:L} of {connected_count:L} connected peer',
            'msgid_plural': 'Seeding to {active_count:L} of {connected_count:L} connected peers',
        },
    },
    {
        'from': {
            'msgid': 'Downloading metadata from {active_count} connected peer ({percent_done}% done)',
            'msgid_plural': 'Downloading metadata from {active_count} connected peers ({percent_done}% done)',
        },
        'to': {
            'msgid': 'Downloading metadata from {active_count:L} connected peer ({percent_done}% done)',
            'msgid_plural': 'Downloading metadata from {active_count:L} connected peers ({percent_done}% done)',
        },
        'fields': {'active_count': 'active_count:L'},
    },
    {
        'from': {
            'msgid': 'Downloading metadata from {active_count:L} peer ({percent_done}% done)',
            'msgid_plural': 'Downloading metadata from {active_count:L} peers ({percent_done}% done)',
        },
        'to': {
            'msgid': 'Downloading metadata from {active_count:L} connected peer ({percent_done}% done)',
            'msgid_plural': 'Downloading metadata from {active_count:L} connected peers ({percent_done}% done)',
        },
    },
    # A torrent's error status reads the same in every client. The GTK client's translations keep their quotes.
    {'from': {'msgid': "Tracker warning: '{warning}'"}, 'to': {'msgid': 'Tracker warning: {warning}'}},
    {'from': {'msgid': 'Tracker gave a warning: {warning}'}, 'to': {'msgid': 'Tracker warning: {warning}'}},
    {'from': {'msgid': "Tracker error: '{error}'"}, 'to': {'msgid': 'Tracker error: {error}'}},
    {'from': {'msgid': 'Tracker gave an error: {error}'}, 'to': {'msgid': 'Tracker error: {error}'}},
    {'from': {'msgid': "Local error: '{error}'"}, 'to': {'msgid': 'Error: {error}'}},
    # transmission-create prints its line breaks itself, so its piece summary is the Mac client's too.
    {
        'from': {'msgid': '{file_count:L} file, {total_size}\n', 'msgid_plural': '{file_count:L} files, {total_size}\n'},
        'to': {'msgid': '{file_count:L} file, {total_size}', 'msgid_plural': '{file_count:L} files, {total_size}'},
        'strip': ['\n'],
    },
    {
        'from': {'msgid': '{piece_count:L} piece, {piece_size}\n', 'msgid_plural': '{piece_count:L} pieces, {piece_size} each\n'},
        'to': {'msgid': '{piece_count:L} piece, {piece_size}', 'msgid_plural': '{piece_count:L} pieces, {piece_size} each'},
        'strip': ['\n'],
    },
    {
        'from': {'msgid': '{count} piece, {size}', 'msgid_plural': '{count} pieces, {size} each'},
        'to': {'msgid': '{piece_count:L} piece, {piece_size}', 'msgid_plural': '{piece_count:L} pieces, {piece_size} each'},
        'fields': {'count': 'piece_count:L', 'size': 'piece_size'},
    },
    # The notification for a finished download has the Mac client's title in every client.
    {'from': {'msgid': 'Torrent Complete'}, 'to': {'msgid': 'Download Complete'}},
    {'from': {'msgid': 'Torrent Completed'}, 'to': {'msgid': 'Download Complete'}},
    {'from': {'msgid': 'Torrents Completed'}, 'to': {'msgid': 'Downloads Complete'}},
    # The blocklist's size, its date and its update message read the same in every client.
    {
        'from': {'msgid': 'Blocklist has {count:L} entry', 'msgid_plural': 'Blocklist has {count:L} entries'},
        'to': {'msgid': 'Blocklist has {count:L} rule', 'msgid_plural': 'Blocklist has {count:L} rules'},
    },
    {
        'from': {'msgid': '<i>Blocklist contains {count:L} rule</i>', 'msgid_plural': '<i>Blocklist contains {count:L} rules</i>'},
        'to': {'msgid': 'Blocklist has {count:L} rule', 'msgid_plural': 'Blocklist has {count:L} rules'},
        'strip': ['<i>', '</i>'],
    },
    {'from': {'msgid': 'Updating blocklist'}, 'to': {'msgid': 'Updating blocklist…'}, 'append': '…'},
    {'from': {'msgid': 'Getting new blocklist…'}, 'to': {'msgid': 'Updating blocklist…'}},
    {'from': {'msgid': 'Last updated'}, 'to': {'msgid': 'Last updated: {date}'}, 'colon': True, 'append': ' {date}'},
    {
        'from': {'msgid': '<i>Blocklist last updated {date}</i>'},
        'to': {'msgid': 'Last updated: {date}'},
        'strip': ['<i>', '</i>'],
    },
    # The GTK client's URL label ends in a colon, as its other field labels do.
    {'from': {'msgid': '_URL'}, 'to': {'msgid': '_URL:'}, 'colon': True},
    # Field labels are in sentence case.
    {'from': {'msgid': 'Created On:'}, 'to': {'msgid': 'Created on:'}},
    # The Mac client's text carries the GTK and Qt clients' mnemonic markers, and shows it without them.
    {'from': {'msgid': 'Cancel'}, 'to': {'msgid': '_Cancel'}},
    {'from': {'msgid': 'Delete'}, 'to': {'msgid': '_Delete'}},
    {'from': {'msgid': 'Remove'}, 'to': {'msgid': '_Remove'}},
    {'from': {'msgid': 'Save'}, 'to': {'msgid': '_Save'}},
    {'from': {'msgid': 'Reset'}, 'to': {'msgid': '_Reset'}},
    {'from': {'msgid': 'File'}, 'to': {'msgid': '_File'}},
    {'from': {'msgid': 'Open URL…'}, 'to': {'msgid': 'Open _URL…'}},
    {'from': {'msgid': 'Start All'}, 'to': {'msgid': '_Start All'}},
    {'from': {'msgid': 'Pause All'}, 'to': {'msgid': '_Pause All'}},
    {'from': {'msgid': 'Quit'}, 'to': {'msgid': '_Quit'}},
    {'from': {'msgid': 'Edit'}, 'to': {'msgid': '_Edit'}},
    {'from': {'msgid': 'Select All'}, 'to': {'msgid': 'Select _All'}},
    {'from': {'msgid': 'Preferences'}, 'to': {'msgid': '_Preferences'}},
    {'from': {'msgid': 'Start'}, 'to': {'msgid': '_Start'}},
    {'from': {'msgid': 'Ask Tracker for More Peers'}, 'to': {'msgid': 'Ask Tracker for _More Peers'}},
    {'from': {'msgid': 'Pause'}, 'to': {'msgid': '_Pause'}},
    {'from': {'msgid': 'Set Location…'}, 'to': {'msgid': 'Set _Location…'}},
    {'from': {'msgid': 'Verify Local Data'}, 'to': {'msgid': '_Verify Local Data'}},
    {'from': {'msgid': 'Copy Magnet Link to Clipboard'}, 'to': {'msgid': '_Copy Magnet Link to Clipboard'}},
    {'from': {'msgid': 'Delete Files and Remove'}, 'to': {'msgid': '_Delete Files and Remove'}},
    {'from': {'msgid': 'View'}, 'to': {'msgid': '_View'}},
    {'from': {'msgid': 'Help'}, 'to': {'msgid': '_Help'}},
    {'from': {'msgid': 'Donate'}, 'to': {'msgid': '_Donate'}},
    {'from': {'msgid': 'Sort Torrents By'}, 'to': {'msgid': '_Sort Torrents By'}},
    {'from': {'msgid': 'Add'}, 'to': {'msgid': '_Add'}},
    {'from': {'msgid': 'Honor global limits'}, 'to': {'msgid': 'Honor global _limits'}},
    {'from': {'msgid': 'Torrent priority:'}, 'to': {'msgid': 'Torrent _priority:'}},
    {'from': {'msgid': 'Idle:'}, 'to': {'msgid': '_Idle:'}},
    {'from': {'msgid': 'Maximum peers:'}, 'to': {'msgid': '_Maximum peers:'}},
    {'from': {'msgid': 'Create'}, 'to': {'msgid': 'C_reate'}},
    {'from': {'msgid': 'Trackers:'}, 'to': {'msgid': '_Trackers:'}},
    {'from': {'msgid': 'Source:'}, 'to': {'msgid': '_Source:'}},
    {'from': {'msgid': 'Private torrent'}, 'to': {'msgid': '_Private torrent'}},
    {'from': {'msgid': 'Torrent file:'}, 'to': {'msgid': '_Torrent file:'}},
    {'from': {'msgid': 'Destination folder:'}, 'to': {'msgid': '_Destination folder:'}},
    {'from': {'msgid': 'Start when added'}, 'to': {'msgid': '_Start when added'}},
    {'from': {'msgid': 'Move torrent file to the Trash'}, 'to': {'msgid': 'Mo_ve torrent file to the Trash'}},
    {'from': {'msgid': 'Scheduled times:'}, 'to': {'msgid': '_Scheduled times:'}},
    {'from': {'msgid': 'to'}, 'to': {'msgid': '_to'}},
    {'from': {'msgid': 'Automatically add torrent files from:'}, 'to': {'msgid': 'Automatically add torrent files _from:'}},
    {'from': {'msgid': 'Start added torrents'}, 'to': {'msgid': '_Start added torrents'}},
    {'from': {'msgid': 'Save to location:'}, 'to': {'msgid': 'Save to _location:'}},
    {'from': {'msgid': 'Append ".part" to incomplete files\' names'}, 'to': {'msgid': 'Append "._part" to incomplete files\' names'}},
    {'from': {'msgid': 'Keep incomplete files in:'}, 'to': {'msgid': 'Keep _incomplete files in:'}},
    {'from': {'msgid': 'Automatically update weekly'}, 'to': {'msgid': '_Automatically update weekly'}},
    {'from': {'msgid': 'Update'}, 'to': {'msgid': '_Update'}},
    {'from': {'msgid': 'Port for incoming connections:'}, 'to': {'msgid': '_Port for incoming connections:'}},
    {'from': {'msgid': 'Pick a random port at startup'}, 'to': {'msgid': 'Pick a _random port at startup'}},
    {'from': {'msgid': 'Maximum peers for new torrents:'}, 'to': {'msgid': 'Maximum peers for new _torrents:'}},
    {'from': {'msgid': 'Maximum peers overall:'}, 'to': {'msgid': 'Maximum peers _overall:'}},
    {'from': {'msgid': 'Enable µTP for peer connections'}, 'to': {'msgid': 'Enable µ_TP for peer connections'}},
    {'from': {'msgid': 'Use PEX to find more peers for public torrents'}, 'to': {'msgid': 'Use PE_X to find more peers for public torrents'}},
    {'from': {'msgid': 'Use DHT to find more peers for public torrents'}, 'to': {'msgid': 'Use _DHT to find more peers for public torrents'}},
    {'from': {'msgid': 'Use Local Peer Discovery to find more peers for public torrents'}, 'to': {'msgid': 'Use _Local Peer Discovery to find more peers for public torrents'}},
    {'from': {'msgid': 'Prevent computer from sleeping with active torrents'}, 'to': {'msgid': '_Prevent computer from sleeping with active torrents'}},
    {'from': {'msgid': 'Allow remote access'}, 'to': {'msgid': 'Allow _remote access'}},
    {'from': {'msgid': 'Open Web Client'}, 'to': {'msgid': '_Open Web Client'}},
    {'from': {'msgid': 'HTTP port:'}, 'to': {'msgid': 'HTTP _port:'}},
    {'from': {'msgid': 'Use authentication'}, 'to': {'msgid': 'Use _authentication'}},
    {'from': {'msgid': 'Username:'}, 'to': {'msgid': '_Username:'}},
    {'from': {'msgid': 'Password:'}, 'to': {'msgid': 'Pass_word:'}},
    {'from': {'msgid': 'Only allow these IP addresses:'}, 'to': {'msgid': 'Only allow these IP a_ddresses:'}},
    {'from': {'msgid': 'URL:'}, 'to': {'msgid': '_URL:'}},
    {'from': {'msgid': 'Limit upload speed:'}, 'to': {'msgid': 'Limit _upload speed:'}},
    {'from': {'msgid': 'Limit download speed:'}, 'to': {'msgid': 'Limit _download speed:'}},
    {'from': {'msgid': 'Open…'}, 'to': {'msgid': '_Open…'}},
    {'from': {'msgid': 'Priority:'}, 'to': {'msgid': '_Priority:'}},
    {'from': {'msgid': 'Upload:'}, 'to': {'msgid': '_Upload:'}},
    {'from': {'msgid': 'Download:'}, 'to': {'msgid': '_Download:'}},
    # "web seed" is two words in every client.
    {
        'from': {
            'msgid': 'Downloading from {active_count} of {connected_count} connected peer and webseed',
            'msgid_plural': 'Downloading from {active_count} of {connected_count} connected peers and webseeds',
        },
        'to': {
            'msgid': 'Downloading from {active_count} of {connected_count} connected peer and web seed',
            'msgid_plural': 'Downloading from {active_count} of {connected_count} connected peers and web seeds',
        },
    },
    {
        'from': {'msgid': 'Downloading from {active_count} webseed', 'msgid_plural': 'Downloading from {active_count} webseeds'},
        'to': {'msgid': 'Downloading from {active_count} web seed', 'msgid_plural': 'Downloading from {active_count} web seeds'},
    },
    # The Qt client's combo box item takes the GTK client's sentence case. The Mac client's pop-up item keeps title case.
    {'from': {'msgid': 'Use Global Settings'}, 'to': {'msgid': 'Use global settings'}, 'keep': True},
    # The Mac client's tracker counts keep their colons inside the message.
    {'from': {'msgid': 'Seeders'}, 'to': {'msgid': 'Seeders:'}, 'colon': True},
    {'from': {'msgid': 'Leechers'}, 'to': {'msgid': 'Leechers:'}, 'colon': True},
    {'from': {'msgid': 'Downloaded'}, 'to': {'msgid': 'Downloaded:'}, 'colon': True},
    # The Mac client's status bar tooltips keep their colons inside the message.
    {'from': {'msgid': 'Global upload limit'}, 'to': {'msgid': 'Global upload limit: {limit}'}, 'colon': True, 'append': ' {limit}'},
    {
        'from': {'msgid': 'Global download limit'},
        'to': {'msgid': 'Global download limit: {limit}'},
        'colon': True,
        'append': ' {limit}',
    },
    {'from': {'msgid': 'Group'}, 'to': {'msgid': 'Group: {group_name}'}, 'keep': True, 'colon': True, 'append': ' {group_name}'},
    # A torrent that can't be downloaded or isn't one gets the same message in every client.
    {
        'from': {'msgid': 'The torrent could not be downloaded from {url}: {error}.'},
        'to': {'msgid': 'The torrent could not be downloaded from {url}: {error}'},
        'rstrip': '.。',
    },
    {
        'from': {'msgid': '"{filename}" is not a valid torrent file.'},
        'to': {'msgid': '"{source}" is not a valid torrent file.'},
        'fields': {'filename': 'source'},
    },
    # The sentence about a URL that can't be used ends with a period, as the other two do.
    {
        'from': {'msgid': "{appname} doesn't know how to use '{url}'"},
        'to': {'msgid': "{appname} doesn't know how to use '{url}'."},
        'period': True,
    },
    # A torrent's size line is two messages again: the size with the file count, then the pieces in parentheses.
    {
        'from': {
            'msgid': '{total_size} ({piece_count:L} piece @ {piece_size})',
            'msgid_plural': '{total_size} ({piece_count:L} pieces @ {piece_size})',
        },
        'to': {'msgid': '({piece_count:L} piece @ {piece_size})', 'msgid_plural': '({piece_count:L} pieces @ {piece_size})'},
        'remove_prefix': '{total_size} ',
    },
    {
        'from': {
            'msgid': '({piece_count} BitTorrent piece @ {piece_size})',
            'msgid_plural': '({piece_count} BitTorrent pieces @ {piece_size})',
        },
        'to': {'msgid': '({piece_count:L} piece @ {piece_size})', 'msgid_plural': '({piece_count:L} pieces @ {piece_size})'},
        'fields': {'piece_count': 'piece_count:L'},
    },
]

FIELD_NAMES = ('msgctxt', 'msgid', 'msgid_plural')


def unquote(quoted):
    escapes = {'n': '\n', 't': '\t'}
    return re.sub(r'\\(.)', lambda match: escapes.get(match.group(1), match.group(1)), quoted[1:-1])


def quote(text):
    return '"' + text.replace('\\', '\\\\').replace('"', '\\"').replace('\n', '\\n').replace('\t', '\\t') + '"'


def field_lines(name, text):
    """Returns a field's lines as msgmerge --no-wrap writes them: text with a line break inside gets a line per line."""
    parts = text.split('\n')
    if len(parts) == 1 or (len(parts) == 2 and parts[1] == ''):
        return [f'{name} {quote(text)}']

    lines = [f'{name} ""']
    for index, part in enumerate(parts):
        last = index == len(parts) - 1
        if not last or part:
            lines.append(quote(part if last else part + '\n'))
    return lines


class Message:
    def __init__(self, lines):
        self.lines = lines
        self.obsolete = any(line.startswith('#~') for line in lines)
        self.fields = {}
        name = None
        for line in lines:
            match = re.match(r'(msgctxt|msgid_plural|msgid|msgstr(?:\[\d+\])?)\s+(".*")$', line)
            if match:
                name = match.group(1)
                self.fields[name] = unquote(match.group(2))
            elif line.startswith('"') and name:
                self.fields[name] += unquote(line)
            else:
                name = None

    def key(self):
        return (self.fields.get('msgctxt'), self.fields.get('msgid'))

    def translations(self):
        return {name: text for name, text in self.fields.items() if name.startswith('msgstr')}

    def is_translated(self):
        fuzzy = any(line.startswith('#,') and 'fuzzy' in line for line in self.lines)
        translations = self.translations()
        return bool(translations) and all(translations.values()) and not fuzzy

    def comments(self):
        return [line for line in self.lines if line.startswith('#')]

    def rebuild(self, comments, to, translations):
        lines = list(comments)
        for name in FIELD_NAMES:
            if to.get(name) is not None:
                lines += field_lines(name, to[name])
        for name, text in translations.items():
            lines += field_lines(name, text)
        self.__init__(lines)


def without_fuzzy(comments):
    """Drops the fuzzy flag and the previous-msgid lines that go with it."""
    kept = []
    for line in comments:
        if line.startswith('#|'):
            continue
        if line.startswith('#,'):
            flags = [flag.strip() for flag in line[2:].split(',') if flag.strip() != 'fuzzy']
            if not flags:
                continue
            line = '#, ' + ', '.join(flags)
        kept.append(line)
    return kept


def colon_of(messages):
    """Returns the colon that most of the catalog's labels end with, e.g. " :" in French and "：" in Chinese."""
    colons = collections.Counter()
    for message in messages:
        msgid = message.fields.get('msgid', '')
        match = re.search(r'[ \u00a0\u202f]?[:：]$', message.fields.get('msgstr', ''))
        if msgid.endswith(':') and match and message.is_translated():
            colons[match.group(0)] += 1
    return colons.most_common(1)[0][0] if colons else ':'


def period_of(messages):
    """Returns the mark that most of the catalog's sentences end with, e.g. "。" in Japanese."""
    periods = collections.Counter()
    for message in messages:
        msgid = message.fields.get('msgid', '')
        match = re.search(r'[.。]$', message.fields.get('msgstr', ''))
        if msgid.endswith('.') and not msgid.endswith('…') and match and message.is_translated():
            periods[match.group(0)] += 1
    return periods.most_common(1)[0][0] if periods else '.'


def moved_translation(text, rename, colon, period):
    if not text:
        return text

    prefix = rename.get('remove_prefix')
    if prefix:
        if not text.startswith(prefix):
            return ''
        text = text[len(prefix):]

    for stripped in rename.get('strip', []):
        text = text.replace(stripped, '')

    for old, new in rename.get('replace', []):
        text = text.replace(old, new)

    fields = rename.get('fields', {})
    if fields:
        text = re.sub(
            r'\{([A-Za-z_][A-Za-z0-9_]*)(?::[^{}]*)?\}',
            lambda match: '{' + fields[match.group(1)] + '}' if match.group(1) in fields else match.group(0),
            text,
        )

    if rename.get('colon') and not re.search(r'[:：]$', text):
        text += colon

    text = text.rstrip(rename.get('rstrip', ''))

    if rename.get('period') and not re.search(r'[.。!?！？]$', text):
        text += period

    append = rename.get('append', '')
    if append and not text.endswith(append):
        text += append

    return text


def fold(po_path):
    text = pathlib.Path(po_path).read_text(encoding='utf-8')
    messages = [Message(block.split('\n')) for block in text.rstrip('\n').split('\n\n')]
    colon = colon_of(messages)
    period = period_of(messages)
    n_changed = 0

    for rename in RENAMES:
        old_key = (rename['from'].get('msgctxt'), rename['from']['msgid'])
        new_key = (rename['to'].get('msgctxt'), rename['to']['msgid'])
        old = next((message for message in messages if not message.obsolete and message.key() == old_key), None)
        new = next((message for message in messages if not message.obsolete and message.key() == new_key), None)
        if old is None or old is new:
            continue

        moved = {name: moved_translation(text, rename, colon, period) for name, text in old.translations().items()}

        if rename.get('keep'):
            if new is not None and old.is_translated() and not new.is_translated():
                new.rebuild(without_fuzzy(new.comments()), rename['to'], moved)
                n_changed += 1
            continue

        n_changed += 1
        if new is None:
            old.rebuild(old.comments(), rename['to'], moved)
            continue

        old_wins = old.is_translated() and (rename.get('prefer') == 'from' or not new.is_translated())
        if old_wins:
            new.rebuild(without_fuzzy(new.comments()), rename['to'], moved)
        messages.remove(old)

    if n_changed != 0:
        pathlib.Path(po_path).write_text('\n\n'.join('\n'.join(message.lines) for message in messages) + '\n', encoding='utf-8')

    return n_changed


def main(argv):
    po_paths = argv[1:] or sorted(pathlib.Path(__file__).resolve().parent.parent.glob('po/*.po'))
    n_changed = sum(fold(po_path) for po_path in po_paths)
    print(f'renamed {n_changed} messages in {len(po_paths)} catalogs')


if __name__ == '__main__':
    main(sys.argv)
