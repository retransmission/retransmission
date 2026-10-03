#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Moves the Qt client's translations from qt/translations/*.ts into po/*.po.
# This runs once, when the Qt client starts using the shared gettext catalog.
#
# For each language with a .ts file, it:
# 1. converts the .ts file to .po with lconvert;
# 2. rewrites each message for the shared catalog:
#    it drops the Qt class context,
#    turns "&" mnemonics into "_" ones in the English text and its translation,
#    splits each "(s)" plural into a singular and a plural English text,
#    and reorders the plural forms from Qt's rules to the .po file's Plural-Forms;
# 3. combines the result with po/<lang>.po using msgcat,
#    which marks a message as fuzzy, with both translations, when the two differ;
# 4. updates po/<lang>.po from the template with msgmerge,
#    passing every Qt message as a compendium,
#    so that Qt's older wordings become fuzzy suggestions.
#
# Qt's unfinished translations stay fuzzy.
# Catalogs keep their unwrapped lines, so that a change shows as the lines it touches.
#
# Usage: migrate-qt-translations.py POT_FILE
# POT_FILE is the template that xgettext builds with the options in po/POTFILES.in.
# Set LCONVERT to the lconvert to run; the default is the one on PATH.

import collections
import gettext
import os
import pathlib
import re
import subprocess
import sys
import tempfile

# Messages whose English text gained a context in the shared catalog, matching GTK's.
CONTEXTS = {
    ('FilterBar', 'Downloading'): 'Verb',
    ('FilterBar', 'Seeding'): 'Verb',
    ('FilterBar', 'Verifying'): 'Verb',
    ('Torrent', 'Downloading'): 'Verb',
    ('Torrent', 'Seeding'): 'Verb',
}

# Plural messages whose text doesn't show the count.
# The code picks between two messages with count == 1, so each language's form for 1
# becomes the first message's translation,
# and the form that most counts use becomes the second's.
SPLITS = {
    'Torrent(s) Completed': ('Torrent Completed', 'Torrents Completed'),
    'Duplicate Torrent(s)': ('Duplicate Torrent', 'Duplicate Torrents'),
}

# Plural messages whose English forms aren't the "(s)" text with "(s)" dropped or made "s".
PLURALS = {
    "Delete these {count:L} torrent(s)' downloaded files?": (
        "Delete this {count:L} torrent's downloaded files?",
        "Delete these {count:L} torrents' downloaded files?",
    ),
    '{total_size} ({piece_count:L} pieces @ {piece_size})': (
        '{total_size} ({piece_count:L} piece @ {piece_size})',
        '{total_size} ({piece_count:L} pieces @ {piece_size})',
    ),
    '{total_size} ({piece_count:L} pieces)': (
        '{total_size} ({piece_count:L} piece)',
        '{total_size} ({piece_count:L} pieces)',
    ),
}

KEYWORD_RE = re.compile(r'^(msgctxt|msgid|msgid_plural|msgstr(?:\[(\d+)\])?)\s+"(.*)"\s*$')


class Message:
    def __init__(self):
        self.comments = []
        self.flags = []
        self.msgctxt = None
        self.msgid = None
        self.msgid_plural = None
        self.msgstr = None
        self.forms = {}

    @property
    def key(self):
        return (self.msgctxt, self.msgid)

    def is_translated(self):
        if 'fuzzy' in self.flags:
            return False
        if self.msgid_plural is not None:
            return bool(self.forms) and all(self.forms.values())
        return bool(self.msgstr)


def unescape(text):
    escapes = {'n': '\n', 't': '\t', 'r': '\r', '"': '"', '\\': '\\'}
    return re.sub(r'\\(.)', lambda match: escapes.get(match[1], match[0]), text)


def escape(text):
    return text.replace('\\', '\\\\').replace('"', '\\"').replace('\t', '\\t').replace('\r', '\\r').replace('\n', '\\n')


def read_po(path):
    """Returns a PO file's messages. The header is the first, with msgid ''."""
    messages, message, field = [], Message(), None
    for line in path.read_text(encoding='utf-8').splitlines() + ['']:
        if not line.strip():
            if message.msgid is not None:
                messages.append(message)
            message, field = Message(), None
        elif line.startswith('#~'):
            continue
        elif line.startswith('#,'):
            message.flags += [flag.strip() for flag in line[2:].split(',') if flag.strip()]
        elif line.startswith('#'):
            message.comments.append(line)
        elif match := KEYWORD_RE.match(line):
            field = (match[1], match[2])
            value = unescape(match[3])
            if match[1] in ('msgctxt', 'msgid', 'msgid_plural', 'msgstr'):
                setattr(message, match[1], value)
            else:
                message.forms[int(match[2])] = value
        elif line.startswith('"') and field is not None:
            value = unescape(line.strip()[1:-1])
            name, index = field
            if name.startswith('msgstr['):
                message.forms[int(index)] += value
            else:
                setattr(message, name, getattr(message, name) + value)
    return messages


def write_po(path, messages):
    def put(lines, keyword, value):
        lines.append(f'{keyword} "{escape(value)}"')

    lines = []
    for message in messages:
        lines += message.comments
        if message.flags:
            lines.append('#, ' + ', '.join(message.flags))
        if message.msgctxt is not None:
            put(lines, 'msgctxt', message.msgctxt)
        put(lines, 'msgid', message.msgid)
        if message.msgid_plural is not None:
            put(lines, 'msgid_plural', message.msgid_plural)
            for index in sorted(message.forms):
                put(lines, f'msgstr[{index}]', message.forms[index])
        else:
            put(lines, 'msgstr', message.msgstr or '')
        lines.append('')
    path.write_text('\n'.join(lines), encoding='utf-8')


def plural_formula(header):
    """Returns (number of forms, function of n) from a header's Plural-Forms."""
    match = re.search(r'Plural-Forms:\s*nplurals\s*=\s*(\d+)\s*;\s*plural\s*=\s*(.*?);?\s*$', header, re.MULTILINE)
    if match is None:
        return 2, lambda n: int(n != 1)
    return int(match[1]), gettext.c2py(match[2])


SAMPLE_COUNTS = [*range(0, 2001), *(1000000 * k for k in range(1, 4))]


def form_map(qt_rule, po_rule):
    """For each .po plural form, returns the Qt form that most of its counts use.

    A .po form that no whole count picks, such as one for fractions, takes Qt's last form.
    """
    qt_count, qt_plural = qt_rule
    po_count, po_plural = po_rule

    def qt_form(n):
        return min(qt_plural(n), qt_count - 1)

    mapping = []
    for po_form in range(po_count):
        counts = [n for n in SAMPLE_COUNTS if po_plural(n) == po_form]
        if not counts:
            mapping.append(qt_count - 1)
            continue
        votes = collections.Counter(qt_form(n) for n in counts)
        # A tie goes to the form of the smallest count above 0, so a form for 0 and 1 takes Qt's form for 1.
        tiebreak = qt_form(min((n for n in counts if n > 0), default=counts[0]))
        mapping.append(max(votes, key=lambda form: (votes[form], form == tiebreak)))
    return mapping


def most_used_form(rule):
    count, plural = rule
    return collections.Counter(min(plural(n), count - 1) for n in SAMPLE_COUNTS).most_common(1)[0][0]


def has_qt_mnemonic(text):
    return re.search(r'&[^&]', text.replace('&&', '')) is not None


def to_catalog_mnemonic(text):
    """Turns Qt's mnemonic markers into the catalog's: "&&" becomes "&", "&X" becomes "_X", "_" becomes "__"."""
    return re.sub(r'&&|&|_', lambda match: {'&&': '&', '&': '_', '_': '__'}[match[0]], text)


def match_newlines(english, translation):
    """Gives `translation` the leading and trailing newlines of `english`, as msgfmt requires and Qt doesn't."""
    if not translation:
        return translation
    lead = '\n' if english.startswith('\n') else ''
    trail = '\n' if english.endswith('\n') else ''
    return lead + translation.strip('\n') + trail


def plural_english(source):
    if source in PLURALS:
        return PLURALS[source]
    if '(s)' not in source:
        return None
    return source.replace('(s)', ''), source.replace('(s)', 's')


def convert(qt_messages, po_header, template):
    """Returns Qt's messages, rewritten for the shared catalog, and the Qt sources it couldn't place."""
    qt_rule = plural_formula(qt_messages[0].msgstr)
    po_rule = plural_formula(po_header.msgstr)
    forms = form_map(qt_rule, po_rule)
    most_used = most_used_form(qt_rule)

    converted, unplaced = [], []
    for qt_message in qt_messages[1:]:
        class_name = (qt_message.msgctxt or '').split('|')[0]
        source = qt_message.msgid
        mnemonic = has_qt_mnemonic(source)
        fix = to_catalog_mnemonic if mnemonic else (lambda text: text)

        outputs = []
        if qt_message.msgid_plural is not None and source in SPLITS:
            for english, qt_form in zip(SPLITS[source], (0, most_used)):
                message = Message()
                message.msgid = english
                message.msgstr = qt_message.forms.get(qt_form, '')
                outputs.append(message)
        elif qt_message.msgid_plural is not None:
            english = plural_english(source)
            if english is None:
                unplaced.append(source)
                continue
            message = Message()
            message.msgid, message.msgid_plural = english
            message.forms = {index: qt_message.forms.get(qt_form, '') for index, qt_form in enumerate(forms)}
            outputs.append(message)
        else:
            message = Message()
            message.msgid = fix(source)
            message.msgstr = fix(qt_message.msgstr or '')
            message.msgctxt = CONTEXTS.get((class_name, source))
            outputs.append(message)

        for message in outputs:
            message.flags = list(qt_message.flags)
            if message.msgstr is not None:
                message.msgstr = match_newlines(message.msgid, message.msgstr)
            message.forms = {index: match_newlines(message.msgid, form) for index, form in message.forms.items()}
            converted.append(message)
            if message.key not in template:
                unplaced.append(message.msgid)
            elif (template[message.key].msgid_plural is None) != (message.msgid_plural is None):
                unplaced.append(message.msgid)
                converted.pop()

    return converted, unplaced


def split_duplicates(messages):
    """Spreads messages over as few lists as can each hold one message per key, dropping exact duplicates."""
    lists, seen = [], collections.defaultdict(list)
    for message in messages:
        value = (message.msgstr, tuple(sorted(message.forms.items())))
        if value in seen[message.key]:
            continue
        index = len(seen[message.key])
        seen[message.key].append(value)
        while len(lists) <= index:
            lists.append([])
        lists[index].append(message)
    return lists


def run(*args):
    subprocess.run(args, check=True)


def main(args):
    if len(args) != 1:
        print('usage: migrate-qt-translations.py POT_FILE', file=sys.stderr)
        return 2

    root = pathlib.Path(__file__).resolve().parent.parent
    pot = pathlib.Path(args[0]).resolve()
    template = {message.key: message for message in read_po(pot)[1:]}
    lconvert = os.environ.get('LCONVERT', 'lconvert')

    for ts in sorted((root / 'qt' / 'translations').glob('transmission_*.ts')):
        lang = ts.stem.removeprefix('transmission_')
        po = root / 'po' / f'{lang}.po'
        if lang == 'en' or not po.exists():
            continue

        with tempfile.TemporaryDirectory() as tmp:
            tmp = pathlib.Path(tmp)
            run(lconvert, '-i', str(ts), '-o', str(tmp / 'qt.po'))

            po_messages = read_po(po)
            converted, unplaced = convert(read_po(tmp / 'qt.po'), po_messages[0], template)
            for source in sorted(set(unplaced)):
                print(f'{lang}: not in the template: {source!r}', flush=True)

            # Finished translations of current messages join po/<lang>.po;
            # every converted message is a compendium entry for msgmerge's fuzzy matching.
            def write_parts(name, messages):
                paths = []
                for index, part in enumerate(split_duplicates(messages)):
                    paths.append(str(tmp / f'{name}-{index}.po'))
                    write_po(pathlib.Path(paths[-1]), [po_messages[0], *part])
                return paths

            current = write_parts('qt', [m for m in converted if m.key in template and m.is_translated()])
            compendia = write_parts('compendium', [m for m in converted if m.msgstr or any(m.forms.values())])

            run('msgcat', '--output-file', str(tmp / 'combined.po'), str(po), *current)
            run('msgmerge', '--quiet', '--previous', '--no-wrap', *(f'--compendium={path}' for path in compendia),
                '--output-file', str(po), str(tmp / 'combined.po'), str(pot))

    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
