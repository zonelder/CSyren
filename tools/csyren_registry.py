#!/usr/bin/env python3
"""Generates the CSyren type registry from markers in headers.

Markers (declared in lib/core/registration.h) are written in a header, at
namespace scope, after the type they name:

    struct Health { float value = 100.0f; static void describe(); };
    CS_COMPONENT(Health);

The script scans the given roots (directories are searched recursively for
headers, files are taken as they are), and writes one C++ file that includes
every marked header and defines

    csyren::generated::registerTypes()   CS_REFLECT, CS_COMPONENT, CS_SYSTEM, CS_EDITOR
    csyren::generated::createServices()  CS_SERVICE

Application calls them at startup. The output is rewritten only when it
changes, so an unchanged registry does not trigger a rebuild. Problems are
reported as "file(line): error CSRxxx: ..." and fail the build.

With --rtti-roots the script also checks that engine and project code does not
use C++ RTTI (the engine builds with /GR-): typeid, std::type_info,
std::type_index, dynamic_cast, std::dynamic_pointer_cast, std::any, any_cast,
make_any and <typeinfo>/<typeindex>/<any> are errors (CSR030), also inside
macro definitions. Use core::TypeKey and core::typeName (lib/core/type_key.h).

Usage (normally run by CSyren.project.targets before ClCompile):
    python csyren_registry.py --out build/generated/registry.gen.cpp
        --roots "C:/CSyren/lib;C:/CSyren/source;C:/MyGame/source"
        --include-dirs "C:/CSyren/lib;C:/CSyren/source/runtime;..."
        [--rtti-roots "C:/CSyren/lib;C:/CSyren/source;C:/MyGame/source"]
"""
import argparse
import os
import re
import sys

MARKERS = ('CS_REFLECT', 'CS_COMPONENT', 'CS_SYSTEM', 'CS_EDITOR', 'CS_SERVICE')
HEADER_EXTENSIONS = ('.h', '.hpp', '.hh', '.hxx')
SOURCE_EXTENSIONS = HEADER_EXTENSIONS + ('.inl', '.ipp', '.cpp', '.cc', '.cxx')
# type_info / type_index only std::-qualified: any use of them needs typeid anyway,
# and plain identifiers with these names (a member "type_index") are fine.
RTTI_TOKENS = re.compile(r'\b(?:typeid|dynamic_cast|dynamic_pointer_cast|any_cast|make_any)\b'
                         r'|\bstd\s*::\s*(?:any|type_info|type_index)\b')
RTTI_INCLUDES = re.compile(r'^\s*#\s*include\s*<(typeinfo|typeindex|any)>', re.MULTILINE)
SKIP_DIRECTORIES = {'third_party', 'imgui', 'joltsolution', 'build', 'x64', 'packages', 'generated'}
IDENTIFIER = r'[A-Za-z_]\w*'
QUALIFIED = re.compile(rf'^(?:::)?{IDENTIFIER}(?:\s*::\s*{IDENTIFIER})*$')
EDITOR_TYPE = re.compile(r'^[\w:<>,\s\*&]+$')


class RegistryError(Exception):
    def __init__(self, path, line, code, message):
        super().__init__(f'{path}({line}): error {code}: {message}')


class Marker:
    def __init__(self, kind, args, namespace, path, line):
        self.kind, self.args, self.namespace, self.path, self.line = kind, args, namespace, path, line

    @property
    def type(self):
        return self.args[0]

    @property
    def name(self):
        """Registered name: the unqualified type name."""
        return re.split(r'\s*::\s*', self.args[0].strip())[-1]

    def where(self):
        return f'{self.path}({self.line})'


# ---------------------------------------------------------------------------
# Source scanning

RAW_STRING = re.compile(r'(?:u8|u|U|L)?R"([^()\\\s]{0,16})\(')


def strip_source(text, keep_directives=False):
    """Blanks comments, string/char literals and (unless keep_directives) preprocessor
    lines; keeps line breaks."""
    out = []
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if text.startswith('//', i):
            end = text.find('\n', i)
            i = n if end < 0 else end
            continue
        if text.startswith('/*', i):
            end = text.find('*/', i + 2)
            end = n if end < 0 else end + 2
            out.append('\n' * text.count('\n', i, end))
            i = end
            continue
        if c in 'uULR':
            m = RAW_STRING.match(text, i)
            if m and (i == 0 or not (text[i - 1].isalnum() or text[i - 1] == '_')):
                close = ')' + m.group(1) + '"'
                end = text.find(close, m.end())
                end = n if end < 0 else end + len(close)
                out.append('""' + '\n' * text.count('\n', i, end))
                i = end
                continue
        if c == '"' or (c == "'" and not (i > 0 and text[i - 1].isalnum() and i + 1 < n and text[i + 1].isalnum())):
            j = i + 1
            while j < n and text[j] != c and text[j] != '\n':
                j += 2 if text[j] == '\\' else 1
            out.append(c + c)
            i = j + 1
            continue
        out.append(c)
        i += 1
    if keep_directives:
        return ''.join(out)
    lines = ''.join(out).split('\n')
    continuation = False
    for index, line in enumerate(lines):
        if continuation or line.lstrip().startswith('#'):
            continuation = line.rstrip().endswith('\\')
            lines[index] = ''
    return '\n'.join(lines)


TOKEN = re.compile(
    r'(?P<namespace>\b(?:inline\s+)?namespace\b\s*(?P<nsname>[A-Za-z_][\w:\s]*?)?\s*(?:\[\[[^\]]*\]\]\s*)?\{)'
    r'|(?P<extern>\bextern\s*""\s*\{)'
    r'|(?P<open>\{)|(?P<close>\})'
    r'|(?P<marker>\b(?P<kind>' + '|'.join(MARKERS) + r')\s*\()')


def split_arguments(text, start, path, line):
    """Splits the marker arguments after '(' at `start`; returns (args, end)."""
    depth, angle, current, args = 1, 0, [], []
    i = start
    while i < len(text):
        c = text[i]
        if c == '(':
            depth += 1
        elif c == ')':
            depth -= 1
            if depth == 0:
                args.append(''.join(current).strip())
                return args, i + 1
        elif c == '<':
            angle += 1
        elif c == '>':
            angle = max(0, angle - 1)
        elif c == ',' and depth == 1 and angle == 0:
            args.append(''.join(current).strip())
            current = []
            i += 1
            continue
        current.append(c)
        i += 1
    raise RegistryError(path, line, 'CSR001', 'unterminated marker')


def scan_header(path, display):
    with open(path, encoding='utf-8', errors='replace') as handle:
        text = strip_source(handle.read())
    if not any(m in text for m in MARKERS):
        return []
    markers, scopes = [], []
    position = 0
    while True:
        match = TOKEN.search(text, position)
        if not match:
            break
        line = text.count('\n', 0, match.start()) + 1
        position = match.end()
        if match.group('namespace'):
            name = (match.group('nsname') or '').strip()
            name = re.sub(r'\s+', '', re.sub(r'\binline\s+', '', name)) if name else ''
            scopes.append(('namespace', name))
        elif match.group('extern'):
            scopes.append(('extern', ''))
        elif match.group('open'):
            scopes.append(('block', ''))
        elif match.group('close'):
            if scopes:
                scopes.pop()
        elif match.group('marker'):
            kind = match.group('kind')
            args, position = split_arguments(text, match.end(), display, line)
            if any(scope == 'block' for scope, _ in scopes):
                raise RegistryError(display, line, 'CSR002', f'{kind} must be at namespace scope (outside classes and functions)')
            if any(scope == 'namespace' and not name for scope, name in scopes):
                raise RegistryError(display, line, 'CSR003', f'{kind} cannot be used in an anonymous namespace')
            expected = 2 if kind == 'CS_EDITOR' else 1
            if len(args) != expected or not all(args):
                raise RegistryError(display, line, 'CSR004', f'{kind} expects {expected} argument(s)')
            if kind == 'CS_EDITOR':
                if not EDITOR_TYPE.match(args[0]) or not QUALIFIED.match(args[1]):
                    raise RegistryError(display, line, 'CSR005', f'{kind}: invalid type name')
            elif not QUALIFIED.match(args[0]):
                raise RegistryError(display, line, 'CSR005', f'{kind}: expected a type name, got "{args[0]}"')
            namespace = '::'.join(name for scope, name in scopes if scope == 'namespace')
            markers.append(Marker(kind, args, namespace, path, line))
    return markers


def collect(roots, extensions=HEADER_EXTENSIONS):
    """Files under the roots in a stable order (roots in order, then sorted paths)."""
    seen, headers = set(), []
    for root in roots:
        root = os.path.normpath(os.path.abspath(root))
        if os.path.isfile(root):
            candidates = [root]
        elif os.path.isdir(root):
            candidates = []
            for directory, subdirectories, files in os.walk(root):
                subdirectories[:] = sorted(d for d in subdirectories
                                           if d.lower() not in SKIP_DIRECTORIES and not d.startswith('.'))
                candidates += [os.path.join(directory, f) for f in sorted(files) if f.lower().endswith(extensions)]
        else:
            raise RegistryError(root, 1, 'CSR010', 'scan root does not exist')
        for candidate in candidates:
            key = os.path.normcase(candidate)
            if key not in seen:
                seen.add(key)
                headers.append(candidate)
    return headers


def check_rtti(path):
    """CSR030 errors for RTTI use in one file. Comments and string literals are
    ignored; macro definitions are checked like code."""
    with open(path, encoding='utf-8', errors='replace') as handle:
        raw = handle.read()
    errors = []
    text = strip_source(raw, keep_directives=True)
    for match in RTTI_INCLUDES.finditer(text):
        line = text.count('\n', 0, match.start()) + 1
        errors.append(RegistryError(path, line, 'CSR030', f'<{match.group(1)}> is not allowed: the engine builds without RTTI (use core::TypeKey / core::typeName)'))
    for match in RTTI_TOKENS.finditer(text):
        line = text.count('\n', 0, match.start()) + 1
        token = re.sub(r'\s+', '', match.group(0))
        errors.append(RegistryError(path, line, 'CSR030', f'"{token}" is not allowed: the engine builds without RTTI (use core::TypeKey / core::typeName)'))
    return errors


# ---------------------------------------------------------------------------
# Output

def include_path(header, include_dirs, out_dir):
    """Shortest spelling of `header` that the compiler resolves to this very file.

    A quoted include in the generated file is looked up in its own directory first,
    then in the include directories in order, so a spelling relative to one include
    directory may be shadowed by a header of the same name in an earlier one (a
    project's source/serializer.h by the engine's source/runtime/serializer.h).
    Shadowed spellings are skipped; the fallback is relative to the generated file,
    which is searched first and therefore always finds the right header."""
    target = os.path.normcase(os.path.realpath(header))
    search = [os.path.abspath(out_dir)] + [os.path.abspath(d) for d in include_dirs]

    def resolves_to_header(spelling):
        for directory in search:
            candidate = os.path.join(directory, spelling)
            if os.path.isfile(candidate):
                return os.path.normcase(os.path.realpath(candidate)) == target
        return False

    spellings = []
    for directory in search[1:]:
        try:
            relative = os.path.relpath(header, directory)
        except ValueError:  # another drive
            continue
        if relative != os.pardir and not relative.startswith(os.pardir + os.sep):
            spellings.append(relative)
    for spelling in sorted(spellings, key=len):
        if resolves_to_header(spelling):
            return spelling.replace('\\', '/')
    try:
        relative = os.path.relpath(header, out_dir)
    except ValueError:  # another drive: absolute path
        relative = os.path.abspath(header)
    return relative.replace('\\', '/')


def qualified(namespace, symbol):
    return f'::{namespace}::{symbol}' if namespace else f'::{symbol}'


def validate(markers):
    def unique(kind, key, what):
        seen = {}
        for marker in (m for m in markers if m.kind == kind):
            value = key(marker)
            if value in seen:
                raise RegistryError(marker.path, marker.line, 'CSR020',
                                    f'{what} "{value}" is already registered at {seen[value].where()}')
            seen[value] = marker
    unique('CS_COMPONENT', lambda m: m.name, 'component')
    unique('CS_SYSTEM', lambda m: m.name, 'system')
    unique('CS_REFLECT', lambda m: (m.namespace, m.type), 'reflected type')
    unique('CS_SERVICE', lambda m: (m.namespace, m.type), 'service')
    unique('CS_EDITOR', lambda m: (m.namespace, re.sub(r'\s+', ' ', m.args[0])), 'editor for type')


def generate(markers, headers, include_dirs, out_dir):
    validate(markers)
    lines = ['// Generated by tools/csyren_registry.py. Do not edit: the file is rewritten on every build.',
             '// Markers: lib/core/registration.h. Layout: docs/project_structure.md.',
             '#include "core/registration.h"',
             '#include "core/component_registry.h"',
             '#include "core/meta.h"',
             '#include "core/services.h"',
             '#include "core/system_base.h"']
    if any(m.kind == 'CS_EDITOR' for m in markers):
        lines.append('#include "editor_base.h"')
    lines.append('#include <memory>')
    lines.append('')
    marked = []
    for header in headers:
        if any(m.path == header for m in markers):
            marked.append(header)
    for header in marked:
        lines.append(f'#include "{include_path(header, include_dirs, out_dir)}"')
    lines.append('')

    calls = {'CS_REFLECT': [], 'CS_COMPONENT': [], 'CS_SYSTEM': [], 'CS_EDITOR': [], 'CS_SERVICE': []}
    counter = 0

    def emit_group(namespace, body_lines, kind, extra=()):
        nonlocal counter
        function = f'csyrenRegistry{counter}'
        counter += 1
        block = list(extra) + [f'static void {function}()', '{'] + [f'    {b}' for b in body_lines] + ['}']
        if namespace:
            lines.append(f'namespace {namespace}')
            lines.append('{')
            lines.extend(f'    {b}' if b else '' for b in block)
            lines.append('}')
        else:
            lines.extend(block)
        lines.append('')
        calls[kind].append(qualified(namespace, function))

    # One function per (header, namespace, kind), in scan order.
    groups = []
    for marker in markers:
        key = (marker.path, marker.namespace, marker.kind)
        if not groups or groups[-1][0] != key:
            groups.append((key, []))
        groups[-1][1].append(marker)
    for (path, namespace, kind), items in groups:
        body, extra = [], []
        for marker in items:
            t = marker.type
            if kind == 'CS_REFLECT':
                body.append(f'::csyren::core::reflection::details::tryDescribe<{t}>();')
            elif kind == 'CS_COMPONENT':
                body.append(f'::csyren::core::reflection::details::tryDescribe<{t}>();')
                body.append(f'static const ::csyren::core::reflection::ComponentRegistrar<{t}> component{counter}_{len(body)}("{marker.name}");')
            elif kind == 'CS_SYSTEM':
                body.append(f'::csyren::core::details::SystemRegistry::instance().registerSystem("{marker.name}",')
                body.append(f'    []() -> std::shared_ptr<::csyren::core::System> {{ return std::make_shared<{t}>(); }});')
            elif kind == 'CS_EDITOR':
                instance = f'csyrenEditor{counter}_{len(extra)}'
                extra.append(f'static {marker.args[1]} {instance};')
                body.append(f'::csyren::editor::EditorRegistry::addPending([] {{')
                body.append(f'    ::csyren::editor::EditorRegistry::add(::csyren::core::reflection::resolve<{marker.args[0]}>(), &{instance}); }});')
            elif kind == 'CS_SERVICE':
                body.append(f'::csyren::core::details::ServiceRegistry::create<{t}>();')
        emit_group(namespace, body, kind, extra)

    lines.append('namespace csyren::generated')
    lines.append('{')
    lines.append('    void registerTypes()')
    lines.append('    {')
    for kind in ('CS_REFLECT', 'CS_COMPONENT', 'CS_SYSTEM', 'CS_EDITOR'):
        lines.extend(f'        {call}();' for call in calls[kind])
    lines.append('    }')
    lines.append('')
    lines.append('    void createServices()')
    lines.append('    {')
    lines.extend(f'        {call}();' for call in calls['CS_SERVICE'])
    lines.append('    }')
    lines.append('}')
    return '\n'.join(lines) + '\n'


def split_list(values):
    result = []
    for value in values or []:
        result += [part.strip().strip('"') for part in value.split(';') if part.strip().strip('"')]
    return result


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    parser.add_argument('--out', required=True, help='generated .cpp file')
    parser.add_argument('--roots', action='append', help='scan roots (files or directories), ";"-separated')
    parser.add_argument('--include-dirs', action='append', help='compiler include directories, ";"-separated')
    parser.add_argument('--list', action='store_true', help='print the markers that were found')
    parser.add_argument('--rtti-roots', action='append', help='files or directories that must not use C++ RTTI, ";"-separated')
    args = parser.parse_args(argv)
    out = os.path.normpath(os.path.abspath(args.out))
    try:
        roots = split_list(args.roots)
        if not roots:
            raise RegistryError(out, 1, 'CSR011', 'no scan roots given')
        headers = collect(roots)
        markers = []
        for header in headers:
            markers += scan_header(header, header)
        text = generate(markers, headers, split_list(args.include_dirs), os.path.dirname(out))
        rtti_errors = []
        for source in collect(split_list(args.rtti_roots), SOURCE_EXTENSIONS):
            rtti_errors += check_rtti(source)
    except RegistryError as error:
        print(error, file=sys.stderr)
        return 1
    if rtti_errors:
        for error in rtti_errors:
            print(error, file=sys.stderr)
        return 1
    if args.list:
        for marker in markers:
            print(f'{marker.where()}: {marker.kind}({", ".join(marker.args)}) in "{marker.namespace}"')
    current = None
    if os.path.exists(out):
        with open(out, encoding='utf-8', newline='') as handle:
            current = handle.read()
    counts = {kind: sum(1 for m in markers if m.kind == kind) for kind in MARKERS}
    summary = ', '.join(f'{counts[k]} {k[3:].lower()}' for k in MARKERS)
    if current != text:
        os.makedirs(os.path.dirname(out), exist_ok=True)
        with open(out, 'w', encoding='utf-8', newline='\n') as handle:
            handle.write(text)
        print(f'csyren_registry: {summary} -> {out} (updated)')
    else:
        print(f'csyren_registry: {summary} -> {out} (unchanged)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
