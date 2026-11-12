#!/usr/bin/env python3
"""Transcribe pinned IINA named mpv constants; no network, SDK or compiler.

This is a source-data transformation, not a documentation scraper. Unknown
Swift declarations fail rather than silently disappearing from the catalog.
Regenerate after deliberately updating the reference snapshot; never edit the
reference sources. Runtime property-list/command-list remain authoritative.
"""
from pathlib import Path
import json
import re

ROOT = Path(__file__).resolve().parent.parent
DEST = ROOT / 'src/IIMA.Core/Mpv/Catalog'
SOURCE = ROOT / 'references/iina/iina'
KEYWORDS = set('alignas alignof and and_eq asm atomic_cancel atomic_commit atomic_noexcept auto bitand bitor bool break case catch char char8_t char16_t char32_t class compl concept const consteval constexpr constinit const_cast continue co_await co_return co_yield decltype default delete do double dynamic_cast else enum explicit export extern false float for friend goto if inline int long mutable namespace new noexcept not not_eq nullptr operator or or_eq private protected public reflexpr register reinterpret_cast requires return short signed sizeof static static_assert static_cast struct switch synchronized template this thread_local throw true try typedef typeid typename union unsigned using virtual void volatile wchar_t while xor xor_eq'.split())
BANNER = '''// Transcribed from IINA's pinned mpv constant catalog; GPL v3.
// Copyright IINA contributors. Source/license: references/iina-source.txt,
// references/iina/LICENSE. Regenerate with scripts/generate-mpv-catalog.py.
// Names describe the reference API, not availability in the loaded libmpv.
'''


def identifier(name):
    return name + '_' if name in KEYWORDS else name


def quoted(value):
    return json.dumps(value, ensure_ascii=False)


def parse(name):
    text = (SOURCE / (name + '.swift')).read_text()
    category = ''
    syntax = ''
    records = []
    lines = text.splitlines()
    for i, line in enumerate(lines):
        group = re.fullmatch(r'  struct (\w+) \{', line)
        if group:
            category = group[1]
        if line == '  }':
            category = ''
        comment = re.fullmatch(r'\s*/\*\* (.*) \*/', line)
        if comment:
            syntax = comment[1]
        constant = re.fullmatch(r'\s*static let (\w+) = (?:MPVCommand\()?"([^"\\]*)"\)?', line)
        if constant:
            symbol, value = constant.groups()
            records.append((category, symbol, value, syntax, 'None'))
            continue
        function = re.fullmatch(r'\s*static func (\w+)\(_ (\w+): (Int|String)\) -> String \{', line)
        if function:
            symbol, parameter, param_type = function.groups()
            body = re.fullmatch(r'\s*return "(.*)"', lines[i + 1])
            if not body or lines[i + 2].strip() != '}':
                raise ValueError(f'Unsupported function {name}.{symbol}')
            value = body[1].replace('\\(' + parameter + ')', '{}')
            if value.count('{}') != 1 or '\\' in value:
                raise ValueError(f'Unsupported interpolation {name}.{symbol}')
            records.append((category, symbol, value, syntax, 'Index' if param_type == 'Int' else 'Name'))
            continue
        if 'static let ' in line or 'static func ' in line:
            raise ValueError(f'Unparsed declaration {name}:{i+1}: {line}')
    expected = len(re.findall(r'\bstatic (?:let|func)\b', text))
    if expected != len(records):
        raise ValueError(f'{name}: parsed {len(records)}, expected {expected}')
    names = [(group, identifier(symbol)) for group, symbol, *_ in records]
    if len(names) != len(set(names)):
        raise ValueError(f'{name}: colliding C++ declarations')
    return records


def emit_header(name, records):
    out = [BANNER, '#pragma once', '#include <cstdint>', '#include <optional>', '#include <string>', '#include <string_view>',
           'namespace iima::mpv_catalog {', f'struct {name} final {{']
    category = ''
    for group, symbol, value, syntax, argument in records:
        if group != category:
            if category:
                out.append('    };')
            if group:
                out.append(f'    struct {group} final {{')
            category = group
        indent = '        ' if group else '    '
        cpp = identifier(symbol)
        if argument == 'None':
            out.append(f'{indent}static constexpr std::string_view {cpp} = {quoted(value)};')
        else:
            prefix, suffix = value.split('{}')
            parameter = 'std::int64_t index' if argument == 'Index' else 'std::string_view name'
            expression = 'std::to_string(index)' if argument == 'Index' else 'std::string(name)'
            out.append(f'{indent}static std::string {cpp}({parameter}) {{ return {quoted(prefix)} + {expression} + {quoted(suffix)}; }}')
    if category:
        out.append('    };')
    out.append('};')
    if name == 'MPVOption':
        out.extend(['// Source option-value enum retained for inspecting imported macOS configs.',
                    '// It is not selected or applied by the Windows player.',
                    'enum class CocoaCbSwRenderer { Automatic, No, Yes };',
                    'inline constexpr CocoaCbSwRenderer DefaultCocoaCbSwRenderer = CocoaCbSwRenderer::Automatic;',
                    'inline constexpr std::string_view RawValue(CocoaCbSwRenderer value) noexcept {',
                    '    switch (value) {',
                    '    case CocoaCbSwRenderer::Automatic: return "auto";',
                    '    case CocoaCbSwRenderer::No: return "no";',
                    '    case CocoaCbSwRenderer::Yes: return "yes";',
                    '    }',
                    '    return {};',
                    '}',
                    'inline constexpr std::optional<CocoaCbSwRenderer> ParseCocoaCbSwRenderer(std::string_view value) noexcept {',
                    '    if (value == "auto") return CocoaCbSwRenderer::Automatic;',
                    '    if (value == "no") return CocoaCbSwRenderer::No;',
                    '    if (value == "yes") return CocoaCbSwRenderer::Yes;',
                    '    return {};',
                    '}'])
    out.append('}')
    (DEST / (name + '.h')).write_text('\n'.join(out) + '\n')


def main():
    DEST.mkdir(parents=True, exist_ok=True)
    catalog = []
    for name, kind in [('MPVCommand', 'Command'), ('MPVOption', 'Option'), ('MPVProperty', 'Property')]:
        rows = parse(name)
        emit_header(name, rows)
        for category, symbol, value, syntax, argument in rows:
            ref = name + '::' + (category + '::' if category else '') + identifier(symbol)
            raw = ref if argument == 'None' else quoted(value)
            catalog.append(f'    {{Kind::{kind}, {quoted(category)}, {quoted(symbol)}, {raw}, {quoted(syntax)}, Argument::{argument}}},')
        print(f'{name}: {len(rows)} source declarations')
    (DEST / 'CatalogData.inc').write_text(BANNER + '\n'.join(catalog) + '\n')


if __name__ == '__main__':
    main()
