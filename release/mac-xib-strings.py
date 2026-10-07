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

  mac-xib-strings.py formats <declarations> <categories> <po> <folder>
      Writes Formats.strings and Formats.stringsdict into the folder,
      for the formatted text that the declarations file, macosx/L10nDeclarations.h, declares.
      Their translations come from the .po file, whose plural forms the categories file maps to Cocoa's;
      with "-" for the .po file, they are English.
      Each key is a declaration's English text in Cocoa's format syntax, as the code looks it up,
      and a plural's .stringsdict entry is in English where the .po file has no translation,
      so that its count still picks the singular or the plural.

  mac-xib-strings.py check-localizable <pot> <source>...
      Fails unless each key that the sources pass to NSLocalizedString() is a string literal
      that the template has as a message with no context, no plural and no {fmt} field.
      Any other key would stay in English.
      Also fails on a call to TR_TEXT(), the Qt client's lookup of plain text.

  mac-xib-strings.py check-plurals <categories> <po>...
      Fails unless each .po file's Plural-Forms gives every whole number the form
      that the categories file, po/mac-plurals.json, gives the number's CLDR plural category.
      Cocoa picks a .stringsdict plural by that category.

The xib entries are the ones that `ibtool --generate-strings-file` writes;
this reads them out of the xib's XML so that it can run where ibtool cannot.
A .po file that this cannot read fails with an error, rather than leaving text in English.
"""

import gettext
import json
import pathlib
import plistlib
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
    a plural message's translations are 'msgstr[0]', 'msgstr[1]' and so on,
    and 'line' is the number of the message's first line that isn't a comment.
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
                fields.setdefault('line', number)
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
    """Whether text is a {fmt} format string, which the code looks up in the Formats table, not with NSLocalizedString().
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
        return f'{key!r} is a {{fmt}} format string, which the code looks up in the Formats table'
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


# Cocoa's plural categories, in the order that CLDR lists their rules in.
PLURAL_CATEGORIES = ('zero', 'one', 'two', 'few', 'many', 'other')

# The whole numbers that check-plurals tries: every remainder that the rules divide by, up to 10000,
# and multiples of a million, which some languages put in "many".
WHOLE_NUMBERS = (*range(10001), 10**6, 10**6 + 1, 2 * 10**6, 10**9, 2**32, 2**64 - 1)

CLDR_RELATION = re.compile(r'([nivwftce])(?: % (\d+))? (!?=) (\d+(?:\.\.\d+)?(?:,\d+(?:\.\.\d+)?)*)')


def cldr_rule_holds(rule, number):
    """Whether a CLDR plural rule, such as "v = 0 and i % 10 = 1", holds for a whole number.
    A whole number's operands n and i are the number; v, w, f, t, c and e are 0.
    Raises ValueError on a rule that this cannot read."""
    operands = dict.fromkeys('vwftce', 0) | {'n': number, 'i': number}

    def holds(relation):
        match = CLDR_RELATION.fullmatch(relation)
        if not match:
            raise ValueError(f'cannot read the CLDR rule {rule!r}')
        value = operands[match[1]] % int(match[2]) if match[2] else operands[match[1]]
        ranges = [[int(end) for end in item.split('..')] for item in match[4].split(',')]
        return any(bounds[0] <= value <= bounds[-1] for bounds in ranges) == (match[3] == '=')

    return any(all(holds(relation) for relation in condition.split(' and ')) for condition in rule.split(' or '))


def read_plural_categories(path):
    """Returns {language: {category: (rule, form)}} from a file like po/mac-plurals.json,
    with each language's categories in CLDR's order. "other" has no rule.
    Raises ValueError on data that doesn't fit."""
    try:
        languages = json.loads(pathlib.Path(path).read_text(encoding='utf-8'))['languages']
    except (KeyError, TypeError, json.JSONDecodeError) as error:
        raise ValueError(f'{path}: cannot read the plural categories: {error!r}') from None

    result = {}
    for language, categories in languages.items():
        if 'other' not in categories:
            raise ValueError(f'{path}: {language} has no "other" category, which every .stringsdict plural needs')
        result[language] = {}
        for category in PLURAL_CATEGORIES:
            entry = categories.get(category)
            if entry is None:
                continue
            rule, form = entry.get('rule'), entry.get('form')
            if category == 'other' and rule is not None:
                raise ValueError(f'{path}: {language} "other" takes the numbers that no rule takes, so it has no rule')
            if category != 'other' and not isinstance(rule, str):
                raise ValueError(f'{path}: {language} "{category}" has no rule')
            if not isinstance(form, int) or form < 0:
                raise ValueError(f'{path}: {language} "{category}" has no form')
            result[language][category] = (rule, form)
        if unknown := sorted(categories.keys() - set(PLURAL_CATEGORIES)):
            raise ValueError(f'{path}: {language} has categories that Cocoa lacks: {", ".join(unknown)}')
    return result


def plural_category(categories, number):
    """Returns the CLDR category of a whole number, given a language's categories from read_plural_categories()."""
    for category, (rule, _form) in categories.items():
        if rule is not None and cldr_rule_holds(rule, number):
            return category
    return 'other'


def po_plural_forms(po_path):
    """Returns (line number, form count, formula) for a .po file's Plural-Forms header,
    where the formula is a function from a count to its form.
    Raises ValueError if the file has no such header."""
    lines = pathlib.Path(po_path).read_text(encoding='utf-8').splitlines()
    for number, line in enumerate(lines, start=1):
        if match := re.search(r'Plural-Forms: *nplurals *= *(\d+) *; *plural *= *([^;\\]+)', line):
            try:
                return number, int(match[1]), gettext.c2py(match[2].strip())
            except ValueError as error:
                raise ValueError(f'{po_path}:{number}: cannot read the plural formula: {error}') from None
    raise ValueError(f'{po_path}: no Plural-Forms header')


def plural_forms_problem(categories_path, categories, form_count, formula):
    """Returns how a catalog's Plural-Forms disagrees with its language's categories, or None if they agree."""
    for category, (_rule, form) in categories.items():
        if form >= form_count:
            return f'{categories_path} gives "{category}" form {form}, but the catalog has {form_count} forms'

    for number in WHOLE_NUMBERS:
        category = plural_category(categories, number)
        form = formula(number)
        if form != categories[category][1]:
            return (f'Plural-Forms gives {number} form {form}, '
                    f'but CLDR puts it in "{category}", which {categories_path} gives form {categories[category][1]}')
    return None


def check_plurals(categories_path, po_paths):
    languages = read_plural_categories(categories_path)
    ok = True
    for po_path in po_paths:
        number, form_count, formula = po_plural_forms(po_path)
        language = pathlib.Path(po_path).stem
        if language not in languages:
            problem = f'{categories_path} has no plural categories for {language}'
        else:
            problem = plural_forms_problem(categories_path, languages[language], form_count, formula)
        if problem:
            ok = False
            print(f'{po_path}:{number}: {problem}', file=sys.stderr)

    return ok


# English's plural categories, by which en.lproj picks a plural.
ENGLISH_CATEGORIES = {'one': ('i = 1 and v = 0', None), 'other': (None, None)}

# The Cocoa format specifier for each {fmt} spec of a formatted string's field.
# The argument of {name} is an NSString, of {name:L} an NSUInteger, which the text shows as a count,
# and of {name:d} an NSInteger.
SPECIFIERS = {'': '@', 'L': 'lu', 'd': 'ld'}

# The parts of a {fmt} format string: escaped braces, named fields, any other brace,
# a percent sign, which Cocoa's format syntax doubles, and the text between them.
FORMAT_PART = re.compile(r'\{\{|\}\}|\{(?P<name>[A-Za-z_]\w*)(?::(?P<spec>[^{}]*))?\}|(?P<brace>[{}])|%|[^{}%]+')

# The declarations of formatted text, with the number of English forms that each takes.
DECLARATION_KEYWORDS = {'TR_DECLARE': 1, 'TR_DECLARE_N': 2}

# A declaration's last argument, which gives its key numbered specifiers (%1$@).
NUMBERED_KEY = 'NUMBERED_KEY'


def format_parts(text):
    """Yields (text, None, None) for each run of literal text in a {fmt} format string, as Cocoa's syntax writes it,
    and (None, name, spec) for each field.
    Raises ValueError on a brace that is neither escaped nor part of a named field."""
    for match in FORMAT_PART.finditer(text):
        if match['brace']:
            raise ValueError(f'{text!r} has a brace that is neither escaped nor part of a named field')
        if match['name']:
            yield None, match['name'], match['spec'] or ''
        else:
            yield {'{{': '{', '}}': '}', '%': '%%'}.get(match[0], match[0]), None, None


class Declaration:
    """Formatted text that the code looks up in the Formats table and formats with
    [NSString localizedStringWithFormat:NSLocalizedStringFromTable(key, @"Formats", nil), arguments...].
    Raises ValueError on English text that doesn't fit."""

    def __init__(self, english, numbered):
        self.msgid = english[0]
        self.msgid_plural = english[1] if len(english) > 1 else None

        # The key is the English text, a plural's in its plural form, whose fields give the arguments' order.
        self.fields = {}
        for _text, name, spec in format_parts(english[-1]):
            if name is None:
                continue
            if name in self.fields:
                raise ValueError(f'{{{name}}} appears twice, but each field takes an argument of its own')
            if spec not in SPECIFIERS:
                raise ValueError(f'{{{name}:{spec}}} has no Cocoa specifier; '
                                 f'a field is {{{name}}}, {{{name}:L}} or {{{name}:d}}')
            self.fields[name] = (len(self.fields) + 1, spec)
        if self.msgid_plural is not None:
            for _text, name, spec in format_parts(self.msgid):
                if name is not None and self.fields.get(name, (0, None))[1] != spec:
                    raise ValueError(f'the singular has {{{name}{":" if spec else ""}{spec}}}, which the plural lacks')

        self.key = self.cocoa_format(english[-1], numbered)

        # A plural counts its {count} field, or else its last {name:L} field.
        self.count = None
        if self.msgid_plural is not None:
            counts = [name for name, (_position, spec) in self.fields.items() if spec == 'L']
            self.count = 'count' if 'count' in self.fields else (counts[-1] if counts else None)
            if self.count is None or not self.fields[self.count][1]:
                raise ValueError('a plural counts a number field, {count:L} or else its last {name:L}, '
                                 'which this lacks')

    def cocoa_format(self, text, numbered=True):
        """Returns English or translated text in Cocoa's format syntax, with the specifiers of the English fields.
        Raises ValueError on a field that the English lacks, or that has another spec."""
        parts = []
        for literal, name, spec in format_parts(text):
            if name is None:
                parts.append(literal)
                continue
            if name not in self.fields:
                raise ValueError(f'{{{name}}} is not a field of the English text')
            position, english_spec = self.fields[name]
            if spec not in ('', english_spec):
                raise ValueError(f'{{{name}:{spec}}} has another spec than the English field')
            specifier = SPECIFIERS[english_spec]
            parts.append(f'%{position}${specifier}' if numbered else f'%{specifier}')
        return ''.join(parts)

    def translation(self, text, count_position=None):
        """Returns translated text in Cocoa's format syntax, with numbered specifiers.
        Cocoa reads the arguments in turn, so it can't skip one that the text leaves out before a later one.
        A plural form may leave out its count, whose position is count_position, which the plural's format key uses.
        Raises ValueError on text that Cocoa can't format."""
        cocoa = self.cocoa_format(text)
        used = {int(position) for position in re.findall(r'%(\d+)\$', cocoa)}
        if count_position is not None:
            used.add(count_position)
        skipped = [name for name, (position, _spec) in self.fields.items() if position not in used]
        if skipped and min(self.fields[name][0] for name in skipped) < max(used, default=0):
            raise ValueError(f'it leaves out {{{skipped[0]}}} before a field that it uses')
        return cocoa

    def plural_entry(self, forms):
        """Returns the plural's .stringsdict entry, given its text for each category in Cocoa's format syntax."""
        position, spec = self.fields[self.count]
        return {
            'NSStringLocalizedFormatKey': f'%{position}$#@{self.count}@',
            self.count: {
                'NSStringFormatSpecTypeKey': 'NSStringPluralRuleType',
                'NSStringFormatValueTypeKey': SPECIFIERS[spec],
                **forms,
            },
        }

    def english_plural(self, categories):
        """Returns the plural's English .stringsdict entry for a language with the given categories:
        the singular for "one" and the plural for every other category, and for 0 where "one" holds it."""
        singular, plural = self.cocoa_format(self.msgid), self.cocoa_format(self.msgid_plural)
        forms = {category: singular if category == 'one' else plural for category in categories}
        if plural_category(categories, 0) == 'one':
            forms['zero'] = plural
        return self.plural_entry(forms)

    def translated_plural(self, categories, fields):
        """Returns the plural's .stringsdict entry from its message in a .po file, or None if a form is untranslated.
        Raises ValueError on a translation that Cocoa can't format."""
        forms = {category: fields.get(f'msgstr[{form}]') for category, (_rule, form) in categories.items()}
        if not all(forms.values()):
            return None
        count_position = self.fields[self.count][0]
        return self.plural_entry({category: self.translation(form, count_position) for category, form in forms.items()})

    def shared_form(self, entry):
        """Returns a key's text in a form that two declarations share if they translate alike,
        whatever they name their fields."""
        if not isinstance(entry, dict):
            return entry
        shared = dict(entry)
        shared['NSStringLocalizedFormatKey'] = shared['NSStringLocalizedFormatKey'].replace(f'#@{self.count}@', '#@@')
        shared[''] = shared.pop(self.count)
        return shared


def read_declarations(path):
    """Returns [(line number, Declaration)] for the TR_DECLARE and TR_DECLARE_N declarations in a file,
    which holds nothing else. Raises ValueError on anything that doesn't fit."""
    tokens = source_tokens(path)
    declarations = []
    index = 0
    while index < len(tokens):
        line, _kind, text = tokens[index]
        if text == ';':
            index += 1
            continue
        if text not in DECLARATION_KEYWORDS or index + 1 == len(tokens) or tokens[index + 1][2] != '(':
            raise ValueError(f'{path}:{line}: expected TR_DECLARE( or TR_DECLARE_N(, not {text}')

        # Each argument is a string literal, which may be split, or NUMBERED_KEY.
        arguments = [[]]
        index += 2
        while index < len(tokens) and tokens[index][2] != ')':
            if tokens[index][2] == ',':
                arguments.append([])
            else:
                arguments[-1].append(tokens[index])
            index += 1
        if index == len(tokens):
            raise ValueError(f'{path}:{line}: {text}( has no closing parenthesis')
        index += 1

        numbered = [token[2] for token in arguments[-1]] == [NUMBERED_KEY]
        if numbered:
            arguments.pop()
        if len(arguments) != DECLARATION_KEYWORDS[text] or not all(
                argument and all(token[1] == 'string' and token[2].startswith('"') for token in argument)
                for argument in arguments):
            english = '"English"' if DECLARATION_KEYWORDS[text] == 1 else '"singular English", "plural English"'
            raise ValueError(f'{path}:{line}: expected {text}({english}) or {text}({english}, {NUMBERED_KEY})')
        try:
            english = [''.join(unquote(token[2]) for token in argument) for argument in arguments]
            declarations.append((line, Declaration(english, numbered)))
        except ValueError as error:
            raise ValueError(f'{path}:{line}: {error}') from None

    return declarations


def write_formats(declarations_path, categories_path, po_path, folder):
    """Writes Formats.strings and Formats.stringsdict, the tables of the declared formatted text,
    in the .po file's language, or in English if po_path is "-"."""
    declarations = read_declarations(declarations_path)
    if po_path == '-':
        categories, messages = ENGLISH_CATEGORIES, {}
    else:
        language = pathlib.Path(po_path).stem
        categories = read_plural_categories(categories_path).get(language)
        if categories is None:
            raise ValueError(f'{categories_path} has no plural categories for {language}')
        messages = {
            (fields['msgid'], fields.get('msgid_plural')): fields
            for fields in po_messages(po_path)
            if 'msgctxt' not in fields
        }

    # Each key's text, from the first declaration that has the key: a translation or None in Formats.strings,
    # and a .stringsdict entry, translated or in English, in Formats.stringsdict.
    strings, plurals = {}, {}
    for line, declaration in declarations:
        fields = messages.get((declaration.msgid, declaration.msgid_plural))
        is_plural = declaration.msgid_plural is not None
        table, other_table = (plurals, strings) if is_plural else (strings, plurals)
        text = None
        try:
            if fields and is_plural:
                text = declaration.translated_plural(categories, fields)
            elif fields and fields['msgstr']:
                text = declaration.translation(fields['msgstr'])
        except ValueError as error:
            print(f'warning: {po_path}:{fields["line"]}: {error}, so the Mac shows {declaration.key!r} in English',
                  file=sys.stderr)
        if is_plural and text is None:
            text = declaration.english_plural(categories)

        where = f'{declarations_path}:{line}'
        if declaration.key in other_table:
            raise ValueError(f'{where}: the key {declaration.key!r} is also the key of line '
                             f'{other_table[declaration.key][1]}, and only one of them is a plural')
        if declaration.key in table:
            earlier_text, earlier_line, earlier = table[declaration.key]
            if earlier.shared_form(earlier_text) != declaration.shared_form(text):
                raise ValueError(f'{where}: the key {declaration.key!r} is also the key of line {earlier_line}, '
                                 f'and {"English" if po_path == "-" else po_path} words them differently; '
                                 f'give one of them {NUMBERED_KEY}, a key with numbered specifiers')
            continue
        table[declaration.key] = (text, line, declaration)

    folder = pathlib.Path(folder)
    lines = [f'{quote(key)} = {quote(text)};\n' for key, (text, _line, _declaration) in strings.items() if text]
    (folder / 'Formats.strings').write_text(''.join(lines), encoding='utf-8')
    with open(folder / 'Formats.stringsdict', 'wb') as stringsdict:
        plistlib.dump({key: entry for key, (entry, _line, _declaration) in plurals.items()}, stringsdict)


def main(argv):
    try:
        if len(argv) >= 5 and argv[1] == 'strings':
            write_strings(argv[2], argv[3], argv[4:])
        elif len(argv) >= 4 and argv[1] == 'check':
            sys.exit(0 if check(argv[2], argv[3:]) else 1)
        elif len(argv) == 4 and argv[1] == 'localizable':
            write_localizable(argv[2], argv[3])
        elif len(argv) == 6 and argv[1] == 'formats':
            write_formats(argv[2], argv[3], argv[4], argv[5])
        elif len(argv) >= 4 and argv[1] == 'check-localizable':
            sys.exit(0 if check_localizable(argv[2], argv[3:]) else 1)
        elif len(argv) >= 4 and argv[1] == 'check-plurals':
            sys.exit(0 if check_plurals(argv[2], argv[3:]) else 1)
        else:
            sys.exit(__doc__)
    except ValueError as error:
        sys.exit(f'error: {error}')


if __name__ == '__main__':
    main(sys.argv)
