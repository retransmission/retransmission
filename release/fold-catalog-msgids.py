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
#   'strip':   text to take out, e.g. ['<i>', '</i>']
#   'replace': [[old, new], ...] text to replace
#   'fields':  {old: new} for {fmt} fields, e.g. {'count': 'piece_count:L'}
#   'colon':   True adds the colon that the catalog's other labels end with
#   'append':  text to add at the end, unless it is there already
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


def moved_translation(text, rename, colon):
    if not text:
        return text

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

    append = rename.get('append', '')
    if append and not text.endswith(append):
        text += append

    return text


def fold(po_path):
    text = pathlib.Path(po_path).read_text(encoding='utf-8')
    messages = [Message(block.split('\n')) for block in text.rstrip('\n').split('\n\n')]
    colon = colon_of(messages)
    n_changed = 0

    for rename in RENAMES:
        old_key = (rename['from'].get('msgctxt'), rename['from']['msgid'])
        new_key = (rename['to'].get('msgctxt'), rename['to']['msgid'])
        old = next((message for message in messages if not message.obsolete and message.key() == old_key), None)
        new = next((message for message in messages if not message.obsolete and message.key() == new_key), None)
        if old is None or old is new:
            continue

        n_changed += 1
        moved = {name: moved_translation(text, rename, colon) for name, text in old.translations().items()}

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
