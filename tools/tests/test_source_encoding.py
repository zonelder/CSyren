"""Engine sources are UTF-8:  python -m unittest discover -s tools/tests

cl compiles the engine with /utf-8 (CSyren.engine.props) and IntelliSense reads a
file without a BOM as UTF-8. A file saved in another code page (Windows-1251 from
a Russian-locale editor) still builds without /utf-8, but IntelliSense then fails
on every translation unit that includes it, standard headers included; with /utf-8
cl rejects it as well (C4828). A BOM is allowed; new files are saved without one
(.editorconfig).

Fix a reported file in Visual Studio with File > Save As > Save with Encoding >
"Unicode (UTF-8 without signature) - Codepage 65001"."""
import os
import re
import unittest

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))

# Engine code, tests, the transitional game/ folder and engine shaders.
SCANNED = ('lib', 'source', 'game', 'test', 'assets')
EXTENSIONS = {'.h', '.hpp', '.inl', '.ipp', '.c', '.cc', '.cpp', '.hlsl', '.hlsli', '.shader', '.coshader'}
# Third-party code keeps its own conventions; build output is not source.
SKIPPED_DIRS = {'imGui', 'JoltSolution', 'third_party', 'packages', 'Build', 'build', 'x64', '.vs', '.git'}


def source_files():
    for name in sorted(os.listdir(ROOT)):
        path = os.path.join(ROOT, name)
        if os.path.isfile(path) and os.path.splitext(name)[1].lower() in EXTENSIONS:
            yield path
    for top in SCANNED:
        for directory, subdirs, files in os.walk(os.path.join(ROOT, top)):
            subdirs[:] = sorted(d for d in subdirs if d not in SKIPPED_DIRS)
            for name in sorted(files):
                if os.path.splitext(name)[1].lower() in EXTENSIONS:
                    yield os.path.join(directory, name)


def encoding_error(path):
    """None for UTF-8 (with or without a BOM), otherwise where decoding fails."""
    with open(path, 'rb') as stream:
        data = stream.read()
    try:
        data.decode('utf-8-sig')
        return None
    except UnicodeDecodeError as error:
        line = data.count(b'\n', 0, error.start) + 1
        return f'line {line}: byte 0x{data[error.start]:02x} is not UTF-8'


class SourceEncodingTest(unittest.TestCase):
    def test_sources_are_utf8(self):
        files = list(source_files())
        self.assertTrue(files, 'no sources found under ' + ROOT)
        bad = []
        for path in files:
            error = encoding_error(path)
            if error:
                bad.append(f'{os.path.relpath(path, ROOT)}({error})')
        self.assertFalse(bad, 'sources not saved as UTF-8:\n  ' + '\n  '.join(bad))

    def test_engine_compiles_as_utf8(self):
        with open(os.path.join(ROOT, 'CSyren.engine.props'), encoding='utf-8-sig') as stream:
            props = stream.read()
        options = re.findall(r'<AdditionalOptions>(.*?)</AdditionalOptions>', props)
        self.assertTrue(any('/utf-8' in value.split() for value in options),
                        'CSyren.engine.props must pass /utf-8 to cl')


if __name__ == '__main__':
    unittest.main()
