#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Fails when a translation in po/*.po or qt/translations/*.ts has {fmt} fields
# that don't fit its English text.
#
# {fmt} is built without exceptions (FMT_USE_EXCEPTIONS=0 in CMakeLists.txt),
# so a format error calls abort() instead of throwing.
# The GTK client would abort on such a translation;
# the Qt client ignores it and shows English.
# Every message whose English text contains a brace is a {fmt} format string
# with named arguments,
# so each of its translations must parse as one
# and may use only the fields that its English text uses.
# Omitting a field is fine: singular forms often drop the count.
# Format specs such as ":L" are not checked,
# because their validity depends on the argument type.
#
# Usage: find-broken-translations.py [PO_OR_TS_FILE]...
# With no arguments, it checks every file in po/ and qt/translations/.

import os
import pathlib
import re
import sys
import xml.parsers.expat

KEYWORD_RE = re.compile(r'(msgctxt|msgid|msgid_plural|msgstr(?:\[\d+\])?)\s+"(.*)"\s*$')

# Escaped braces, replacement fields ({name}, {name:spec}, {0}, {}), and any other brace,
# which {fmt} rejects.
TOKEN_RE = re.compile(r'\{\{|\}\}|\{(?P<id>[A-Za-z_][A-Za-z0-9_]*|[0-9]*)(?::[^{}]*)?\}|(?P<stray>[{}])')


def po_messages(path):
    """Yield each compiled message of a PO file as ([(line, English text)], [(line, translation)])."""
    flags, fields, keyword = set(), {}, None
    lines = path.read_text(encoding='utf-8').splitlines()
    for number, line in enumerate(lines + [''], start=1):
        if not line.strip():
            # msgfmt doesn't compile fuzzy entries.
            if fields and 'fuzzy' not in flags:
                sources = [fields[key] for key in ('msgid', 'msgid_plural') if key in fields]
                translations = [value for key, value in fields.items() if key.startswith('msgstr')]
                yield sources, translations
            flags, fields, keyword = set(), {}, None
        elif line.startswith('#,'):
            flags.update(flag.strip() for flag in line[2:].split(','))
        elif match := KEYWORD_RE.match(line):
            keyword = match[1]
            fields[keyword] = (number, match[2])
        elif line.startswith('"') and keyword is not None:
            start, text = fields[keyword]
            fields[keyword] = (start, text + line.strip()[1:-1])


def ts_messages(path):
    """Yield each compiled message of a Qt .ts file as ([(line, English text)], [(line, translation)])."""
    parser = xml.parsers.expat.ParserCreate()
    messages, message, texts, compiled = [], None, None, True

    def start(name, attrs):
        nonlocal message, texts, compiled
        if name == 'message':
            message = ([], [])
        elif name == 'translation':
            # lrelease compiles unfinished translations, but not vanished or obsolete ones.
            compiled = attrs.get('type') not in ('vanished', 'obsolete')
        if name in ('source', 'translation', 'numerusform'):
            texts = (parser.CurrentLineNumber, [])

    def end(name):
        nonlocal message, texts
        if name == 'source':
            message[0].append((texts[0], ''.join(texts[1])))
        elif name in ('translation', 'numerusform') and compiled and texts is not None:
            message[1].append((texts[0], ''.join(texts[1])))
        elif name == 'message':
            messages.append(message)
        texts = None

    def characters(data):
        if texts is not None:
            texts[1].append(data)

    parser.StartElementHandler = start
    parser.EndElementHandler = end
    parser.CharacterDataHandler = characters
    with path.open('rb') as file:
        parser.ParseFile(file)
    return messages


def field_ids(text):
    """Return the ids of the replacement fields in `text`, or None if {fmt} can't parse it."""
    ids = set()
    for token in TOKEN_RE.finditer(text):
        if token['stray'] is not None:
            return None
        if token['id'] is not None:
            ids.add(token['id'])
    return ids


def problems(path):
    name = os.path.relpath(path)
    messages = ts_messages(path) if path.suffix == '.ts' else po_messages(path)
    for sources, translations in messages:
        if not any(c in text for _, text in sources for c in '{}'):
            continue

        source_ids = [field_ids(text) for _, text in sources]
        if None in source_ids:
            yield f'{name}:{sources[0][0]}: English text is not a format string this script can parse'
            continue
        allowed = set().union(*source_ids)

        for number, text in translations:
            if not text:
                continue
            ids = field_ids(text)
            if ids is None:
                yield f'{name}:{number}: unmatched brace or malformed field'
            elif unknown := sorted(ids - allowed):
                yield f'{name}:{number}: unknown field ' + ', '.join(f'{{{field}}}' for field in unknown)


def main(args):
    root = pathlib.Path(__file__).resolve().parent.parent
    paths = [pathlib.Path(arg) for arg in args] or [
        *sorted((root / 'po').glob('*.po')),
        *sorted((root / 'qt' / 'translations').glob('*.ts')),
    ]
    found = [problem for path in paths for problem in problems(path)]
    for problem in found:
        print(problem)
    if found:
        print(f"{len(found)} translations have fields that don't fit their English text. "
              'A translation may use only the fields in its English text.')
    return 1 if found else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
