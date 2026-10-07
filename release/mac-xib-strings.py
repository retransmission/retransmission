#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later

"""Writes the .strings files that translate the Mac client.

The catalog that the clients share maps English text to its translation.
A xib's .strings file maps each element's ID to its text instead;
xgettext extracts a xib's English text into the catalog with the rules in po/its/xib.its.
Localizable.strings maps English text to its translation, as the catalog does,
for the plain text that the code looks up with NSLocalizedString().

  mac-xib-strings.py strings <po> <folder> <xib>...
      Writes each xib's .strings file for the .po file's language into the folder.

  mac-xib-strings.py check <its> <xib>...
      Fails unless the ITS rules select the same text in each xib as this script writes entries for.
      Text that only one of them finds would stay in English.

  mac-xib-strings.py localizable <po> <folder>
      Writes the .po file's Localizable.strings into the folder:
      the translation of each message that has no context, no plural and no {fmt} field.

  mac-xib-strings.py check-localizable <pot> <source>...
      Fails unless each key that the sources pass to NSLocalizedString() is a string literal
      that the template has as a message with no context, no plural and no {fmt} field.
      Any other key would stay in English.
      Also fails on a call to TR_TEXT(), the Qt client's lookup of plain text.

The xib entries are the ones that `ibtool --generate-strings-file` writes;
this reads them out of the xib's XML so that it can run where ibtool cannot.
A .po file that this cannot read fails with an error, rather than leaving text in English.
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


ESCAPES = {'n': '\n', 't': '\t', '"': '"', '\\': '\\'}

PO_FIELD = re.compile(r'(msgctxt|msgid|msgid_plural|msgstr(?:\[\d+\])?) (".*")')


def quote(text):
    return '"' + text.replace('\\', '\\\\').replace('"', '\\"').replace('\n', '\\n').replace('\t', '\\t') + '"'


def unquote(quoted):
    """Returns the text of a quoted string, such as a line of a .po file,
    if it uses only the escapes that quote() writes. Raises ValueError otherwise."""
    match = re.fullmatch(r'"((?:[^"\\]|\\.)*)"', quoted)
    if not match:
        raise ValueError(f'cannot read {quoted}')

    def unescape(escape):
        if escape[1] not in ESCAPES:
            raise ValueError(f'cannot read the escape {escape[0]} in {quoted}')
        return ESCAPES[escape[1]]

    return re.sub(r'\\(.)', unescape, match[1])


def po_messages(po_path):
    """Yields each message of a .po or .pot file as its fields, e.g. {'msgid': 'Open', 'msgstr': 'Öffnen'};
    a plural message's translations are 'msgstr[0]', 'msgstr[1]' and so on.
    Leaves out fuzzy and obsolete messages, as msgfmt does.
    Raises ValueError on a line that it cannot read."""
    fields, field, fuzzy = {}, None, False
    lines = pathlib.Path(po_path).read_text(encoding='utf-8').splitlines()
    for number, line in enumerate(lines + [''], start=1):
        try:
            if not line:
                if fields and ('msgid' not in fields or not any(name.startswith('msgstr') for name in fields)):
                    raise ValueError('the message before this line has no msgid or msgstr')
                if fields and not fuzzy:
                    yield fields
                fields, field, fuzzy = {}, None, False
            elif line.startswith('#'):
                # A comment, or a line of an obsolete message.
                fuzzy = fuzzy or (line.startswith('#,') and 'fuzzy' in line)
            elif (match := PO_FIELD.fullmatch(line)) and match[1] not in fields:
                field = match[1]
                fields[field] = unquote(match[2])
            elif line.startswith('"') and field:
                fields[field] += unquote(line)
            else:
                raise ValueError(f'cannot read {line!r}')
        except ValueError as error:
            raise ValueError(f'{po_path}:{number}: {error}') from None


def po_translations(po_path):
    """Returns {English text: translation} for a .po file's translated messages that have no context or plural."""
    return {
        fields['msgid']: fields['msgstr']
        for fields in po_messages(po_path)
        if fields['msgid'] and fields.get('msgstr') and 'msgctxt' not in fields and 'msgid_plural' not in fields
    }


def write_strings(po_path, folder, xib_paths):
    translations = po_translations(po_path)
    for xib_path in xib_paths:
        lines = []
        for key, text in xib_strings(xib_path):
            translation = translations.get(text)
            if translation:
                lines.append(f'{quote(key)} = {quote(translation)};\n')

        strings_path = pathlib.Path(folder) / (pathlib.Path(xib_path).stem + '.strings')
        strings_path.write_text(''.join(lines), encoding='utf-8')


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


def is_format_string(text):
    """Whether text is a {fmt} format string, which the code looks up with TR_FORMAT rather than NSLocalizedString().
    Every message whose English text has a brace is one."""
    return '{' in text or '}' in text


def write_localizable(po_path, folder):
    lines = [
        f'{quote(text)} = {quote(translation)};\n'
        for text, translation in po_translations(po_path).items()
        if not is_format_string(text)
    ]
    (pathlib.Path(folder) / 'Localizable.strings').write_text(''.join(lines), encoding='utf-8')


# Enough of C's tokens to find calls and their string literals.
# Comments and character literals are tokens too, so that a quote in one starts no string.
SOURCE_TOKEN = re.compile(
    r'''(?P<comment>//[^\n]*|/\*.*?\*/)
      | (?P<string>@?"(?:[^"\\\n]|\\.)*")
      | (?P<char>'(?:[^'\\\n]|\\.)*')
      | (?P<name>[A-Za-z_]\w*)
      | (?P<space>\s+)
      | (?P<other>.)''',
    re.VERBOSE | re.DOTALL)


def source_tokens(source_path):
    """Returns [(line number, kind, text)] for the tokens of a C, C++ or Objective-C file, without comments or space.
    A kind is a group name of SOURCE_TOKEN."""
    tokens = []
    number = 1
    for match in SOURCE_TOKEN.finditer(pathlib.Path(source_path).read_text(encoding='utf-8')):
        if match.lastgroup not in ('comment', 'space'):
            tokens.append((number, match.lastgroup, match[0]))
        number += match[0].count('\n')
    return tokens


def key_problem(tokens, plain, with_context, plural):
    """Returns why the key of an NSLocalizedString() call would stay in English, or None if it wouldn't.
    `tokens` are the call's first two tokens after its "(".
    The sets hold the template's msgids: without context or plural, with a context, and with a plural."""
    if len(tokens) < 2 or tokens[0][1] != 'string' or not tokens[0][2].startswith('@'):
        return 'the key is not an @"..." string literal'
    if tokens[1][1] == 'string':
        return 'the key is split across string literals'
    if tokens[1][2] != ',':
        return 'the key is not a single string literal'

    key = unquote(tokens[0][2][1:])
    if is_format_string(key):
        return f'{key!r} is a {{fmt}} format string, which TR_FORMAT looks up'
    if key in plain:
        return None
    if key in plural:
        return f'the template has {key!r} only as a plural message'
    if key in with_context:
        return f'the template has {key!r} only with a context'
    return f'the template has no {key!r}; xgettext reads only the files in po/POTFILES.in'


def check_localizable(pot_path, source_paths):
    plain, with_context, plural = set(), set(), set()
    for fields in po_messages(pot_path):
        if 'msgid_plural' in fields:
            plural.add(fields['msgid'])
        elif 'msgctxt' in fields:
            with_context.add(fields['msgid'])
        else:
            plain.add(fields['msgid'])

    ok = True
    for source_path in source_paths:
        tokens = source_tokens(source_path)
        for index, (number, kind, text) in enumerate(tokens[:-1]):
            if kind != 'name' or tokens[index + 1][2] != '(':
                continue

            if text == 'TR_TEXT':
                problem = "TR_TEXT() is the Qt client's; the Mac client looks up plain text with NSLocalizedString()"
            elif text == 'NSLocalizedString':
                try:
                    problem = key_problem(tokens[index + 2:index + 4], plain, with_context, plural)
                except ValueError as error:
                    problem = str(error)
            else:
                continue

            if problem:
                ok = False
                print(f'{source_path}:{number}: {problem}', file=sys.stderr)

    return ok


def main(argv):
    try:
        if len(argv) >= 5 and argv[1] == 'strings':
            write_strings(argv[2], argv[3], argv[4:])
        elif len(argv) >= 4 and argv[1] == 'check':
            sys.exit(0 if check(argv[2], argv[3:]) else 1)
        elif len(argv) == 4 and argv[1] == 'localizable':
            write_localizable(argv[2], argv[3])
        elif len(argv) >= 4 and argv[1] == 'check-localizable':
            sys.exit(0 if check_localizable(argv[2], argv[3:]) else 1)
        else:
            sys.exit(__doc__)
    except ValueError as error:
        sys.exit(f'error: {error}')


if __name__ == '__main__':
    main(sys.argv)
