"""C++20 compiler lookup for the tests that compile code (test_csyren_registry,
test_typed_contract).

A compiler on PATH that cannot compile C++20 (an old MinGW g++ that only knows
-std=c++2a) is passed over: those tests are skipped, not failed, when no compiler
on PATH can build C++20."""
import os
import shutil
import subprocess
import tempfile


def is_msvc(compiler):
    return os.path.splitext(os.path.basename(compiler))[0].lower() == 'cl'


def supports_cpp20(compiler):
    """True when `compiler` compiles an empty translation unit as C++20."""
    with tempfile.TemporaryDirectory() as directory:
        source = os.path.join(directory, 'probe.cpp')
        with open(source, 'w', encoding='utf-8') as handle:
            handle.write('int main() { return 0; }\n')
        if is_msvc(compiler):
            command = [compiler, '/nologo', '/std:c++20', '/Zs', source]
        else:
            command = [compiler, '-std=c++20', '-fsyntax-only', source]
        try:
            result = subprocess.run(command, capture_output=True, text=True, errors='replace', cwd=directory)
        except OSError:
            return False
    # cl ignores an option it does not know (warning D9002) instead of failing
    return result.returncode == 0 and 'D9002' not in result.stdout + result.stderr


def find_cpp20(candidates):
    """(path of the first of `candidates` on PATH that compiles C++20 or None,
    paths of the ones on PATH that do not)."""
    passed_over = []
    for candidate in candidates:
        path = shutil.which(candidate)
        if not path:
            continue
        if supports_cpp20(path):
            return path, passed_over
        passed_over.append(path)
    return None, passed_over


def missing_reason(candidates, passed_over):
    reason = f'no C++20 compiler on PATH ({", ".join(candidates)})'
    if passed_over:
        reason += '; without -std=c++20: ' + ', '.join(passed_over)
    return reason
