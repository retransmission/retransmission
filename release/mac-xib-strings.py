#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later

"""Writes the .strings files that AppKit translates the Mac client's xib files with.

A .strings file maps each element's ID to its text,
while the catalog that the clients share maps English text to its translation.
xgettext extracts a xib's English text into the catalog with the rules in po/its/xib.its.

A xib's text marks a mnemonic with "_" wherever the GTK and Qt clients' text does, as in "_File",
so that the three clients share one catalog entry. AppKit has no mnemonics,
so every language needs a .strings file, English too, to show such text without its marker.
A literal underscore in a xib's text is written "__".

  mac-xib-strings.py strings <po> <folder> <xib>...
      Writes each xib's .strings file for the .po file's language into the folder.
      With "-" for the .po file, the text stays in English.

  mac-xib-strings.py check <its> <xib>...
      Fails unless the ITS rules select the same text in each xib as this script writes entries for.
      Text that only one of them finds would stay in English.

The entries are the ones that `ibtool --generate-strings-file` writes;
this reads them out of the xib's XML so that it can run where ibtool cannot.
"""

import pathlib
import re
import sys
import xml.etree.ElementTree as ET

# An element's own text. A pop-up button's cell repeats its selected menu item's title, which the menu item provides.
TITLE_TAGS = {'buttonCell', 'menu', 'menuItem', 'textFieldCell', 'window'}
PLACEHOLDER_TAGS = {'searchFieldCell', 'textFieldCell'}
LABEL_TAGS = {'tabViewItem'}


def xib_strings(xib_path):
    """Returns [(key, text)] for a xib, where key is "<element ID>.<property>" as in a .strings file."""
    found = []
    for element in ET.parse(xib_path).getroot().iter():
        object_id = element.get('id')
        if not object_id:
            continue

        if element.tag in TITLE_TAGS:
            found.append((f'{object_id}.title', element.get('title')))
        if element.tag in PLACEHOLDER_TAGS:
            found.append((f'{object_id}.placeholderString', element.get('placeholderString')))
        if element.tag in LABEL_TAGS:
            found.append((f'{object_id}.label', element.get('label')))
        if element.tag == 'tableColumn':
            header = element.find('tableHeaderCell')
            found.append((f'{object_id}.headerCell.title', header.get('title') if header is not None else None))
        if element.tag == 'segmentedCell':
            for index, segment in enumerate(element.findall('segments/segment')):
                found.append((f'{object_id}.ibShadowedLabels[{index}]', segment.get('label')))

    return [(key, text) for key, text in found if text]


def strip_mnemonic(text):
    """Returns text without its mnemonic markers: "_File" becomes "File", and "__" becomes "_".
    A parenthesized mnemonic, which Chinese, Japanese and Korean translations add after the text, goes away whole.
    tr::app::l10n::strip_mnemonic() does the same for the text in the Mac client's code."""
    text = re.sub(r' *[(（]_[A-Za-z0-9][)）]', '', text)
    return re.sub(r'_(.)', r'\1', text, flags=re.DOTALL)


def quote(text):
    return '"' + text.replace('\\', '\\\\').replace('"', '\\"').replace('\n', '\\n').replace('\t', '\\t') + '"'


def unquote(quoted):
    escapes = {'n': '\n', 't': '\t'}
    return re.sub(r'\\(.)', lambda match: escapes.get(match.group(1), match.group(1)), quoted[1:-1])


def po_translations(po_path):
    """Returns {English text: translation} for a .po file's messages that have no context or plural.
    Leaves out fuzzy and obsolete messages, as msgfmt does."""
    translations = {}
    for block in pathlib.Path(po_path).read_text(encoding='utf-8').split('\n\n'):
        lines = block.splitlines()
        if any(line.startswith('#,') and 'fuzzy' in line for line in lines):
            continue

        fields = {}
        name = None
        for line in lines:
            match = re.match(r'(msgctxt|msgid_plural|msgid|msgstr)\s+(".*")$', line)
            if match:
                name = match.group(1)
                fields[name] = unquote(match.group(2))
            elif line.startswith('"') and name:
                fields[name] += unquote(line)
            elif not line.startswith('#'):
                name = None

        if fields.get('msgid') and fields.get('msgstr') and 'msgctxt' not in fields and 'msgid_plural' not in fields:
            translations[fields['msgid']] = fields['msgstr']

    return translations


def write_strings(po_path, folder, xib_paths):
    translations = {} if po_path == '-' else po_translations(po_path)
    for xib_path in xib_paths:
        lines = []
        for key, text in xib_strings(xib_path):
            translation = translations.get(text)
            if '_' in text:
                # Without an entry, AppKit would show the xib's own text, marker and all.
                translation = strip_mnemonic(translation or text)
            if translation:
                lines.append(f'{quote(key)} = {quote(translation)};\n')

        strings_path = pathlib.Path(folder) / (pathlib.Path(xib_path).stem + '.strings')
        # AppKit logs a parse error for a .strings file with nothing in it.
        strings_path.write_text(''.join(lines) or '/* No entries */\n', encoding='utf-8')


def its_texts(its_path, xib_path):
    """Returns the text that the ITS rules select in a xib. Each rule's selector is a path of tags to an attribute."""
    root = ET.parse(xib_path).getroot()
    texts = set()
    for rule in ET.parse(its_path).getroot():
        if rule.tag.endswith('translateRule') and rule.get('translate') == 'yes':
            path, attribute = rule.get('selector').rsplit('/@', 1)
            texts.update(element.get(attribute) for element in root.iterfind('.' + path))

    return texts - {None, ''}


def check(its_path, xib_paths):
    ok = True
    for xib_path in xib_paths:
        extracted = its_texts(its_path, xib_path)
        written = {text for _key, text in xib_strings(xib_path)}
        for text in sorted(extracted ^ written):
            ok = False
            finder = its_path if text in extracted else 'this script'
            print(f'{xib_path}: only {finder} finds {text!r}', file=sys.stderr)

    return ok


def main(argv):
    if len(argv) >= 5 and argv[1] == 'strings':
        write_strings(argv[2], argv[3], argv[4:])
    elif len(argv) >= 4 and argv[1] == 'check':
        sys.exit(0 if check(argv[2], argv[3:]) else 1)
    else:
        sys.exit(__doc__)


if __name__ == '__main__':
    main(sys.argv)
