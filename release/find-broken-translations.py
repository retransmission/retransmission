#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Fails when a translation in po/*.po would make {fmt} abort the GTK client.
#
# {fmt} is built without exceptions (FMT_USE_EXCEPTIONS=0 in CMakeLists.txt),
# so a format error calls abort() instead of throwing.
# Every message whose English text contains a brace is a {fmt} format string
# with named arguments,
# so each of its translations must parse as one
# and may use only the fields that its msgid or msgid_plural uses.
# Omitting a field is fine: singular forms often drop the count.
# Format specs such as ":L" are not checked,
# because their validity depends on the argument type.
#
# Usage: find-broken-translations.py [PO_FILE]...
# With no arguments, it checks every file in po/.

import os
import pathlib
import re
import sys

KEYWORD_RE = re.compile(r'(msgctxt|msgid|msgid_plural|msgstr(?:\[\d+\])?)\s+"(.*)"\s*$')

# Escaped braces, replacement fields ({name}, {name:spec}, {0}, {}), and any other brace,
# which {fmt} rejects.
TOKEN_RE = re.compile(r'\{\{|\}\}|\{(?P<id>[A-Za-z_][A-Za-z0-9_]*|[0-9]*)(?::[^{}]*)?\}|(?P<stray>[{}])')


def entries(path):
    """Yield each entry of a PO file as (flags, {keyword: (line number, text)})."""
    flags, fields, keyword = set(), {}, None
    lines = path.read_text(encoding='utf-8').splitlines()
    for number, line in enumerate(lines + [''], start=1):
        if not line.strip():
            if fields:
                yield flags, fields
            flags, fields, keyword = set(), {}, None
        elif line.startswith('#,'):
            flags.update(flag.strip() for flag in line[2:].split(','))
        elif match := KEYWORD_RE.match(line):
            keyword = match[1]
            fields[keyword] = (number, match[2])
        elif line.startswith('"') and keyword is not None:
            start, text = fields[keyword]
            fields[keyword] = (start, text + line.strip()[1:-1])


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
    for flags, fields in entries(path):
        sources = [fields[key] for key in ('msgid', 'msgid_plural') if key in fields]
        if 'fuzzy' in flags or not any(c in text for _, text in sources for c in '{}'):
            continue

        source_ids = [field_ids(text) for _, text in sources]
        if None in source_ids:
            yield f'{name}:{sources[0][0]}: msgid is not a format string this script can parse'
            continue
        allowed = set().union(*source_ids)

        for keyword, (number, text) in fields.items():
            if not keyword.startswith('msgstr') or not text:
                continue
            ids = field_ids(text)
            if ids is None:
                yield f'{name}:{number}: unmatched brace or malformed field'
            elif unknown := sorted(ids - allowed):
                yield f'{name}:{number}: unknown field ' + ', '.join(f'{{{field}}}' for field in unknown)


def main(args):
    po_dir = pathlib.Path(__file__).resolve().parent.parent / 'po'
    paths = [pathlib.Path(arg) for arg in args] or sorted(po_dir.glob('*.po'))
    found = [problem for path in paths for problem in problems(path)]
    for problem in found:
        print(problem)
    if found:
        print(f'{len(found)} translations would abort fmt::format(). '
              'A translation may use only the fields in its English text.')
    return 1 if found else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
