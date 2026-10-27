"""Tests of tools/csyren.py (new / generate / restore):  python -m unittest discover -s tools/tests"""
import json
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
import csyren  # noqa: E402

QUIET = lambda *args: None


def read(path):
    with open(path, encoding='utf-8-sig') as handle:
        return handle.read()


class NewProjectTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.folder = os.path.join(self.temp.name, 'Demo')

    def tearDown(self):
        self.temp.cleanup()

    def path(self, *parts):
        return os.path.join(self.folder, *parts)

    def test_new_writes_the_project_and_generates_it(self):
        csyren.new(self.folder, 'Demo', 'dev', log=QUIET)
        settings = json.loads(read(self.path('Demo.csyrenproj')))
        self.assertEqual(settings['engine'], 'dev')
        self.assertEqual(settings['startScene'], 'assets/scenes/main.scene')
        for relative in ('Demo.vcxproj', 'Demo.vcxproj.user', 'Demo.sln', 'build/engine.props', 'README.md',
                         'source/spinner.h', 'source/spinner.cpp', 'assets/app.systems', 'assets/scenes/main.scene'):
            self.assertTrue(os.path.isfile(self.path(relative)), relative)

        vcxproj = read(self.path('Demo.vcxproj'))
        self.assertIn('build\\engine.props', vcxproj)
        self.assertIn("Condition=\"'$(CSyrenEngineFound)'=='true'\"", vcxproj)
        self.assertIn('CSyrenEngineMissing', vcxproj)
        self.assertNotIn('..\\', vcxproj, 'the engine location is not stored in the committed project')

        gitignore = read(self.path('.gitignore')).splitlines()
        for pattern in ('/*.sln', '*.vcxproj.user', 'build/'):
            self.assertIn(pattern, gitignore)

        props = read(self.path('build', 'engine.props'))
        self.assertIn('<CSyrenRoot>' + csyren.windows(os.path.join(csyren.ENGINE_ROOT, '')) + '</CSyrenRoot>', props)
        self.assertIn('<CSyrenEngineId>dev</CSyrenEngineId>', props)

        solution = read(self.path('Demo.sln'))
        self.assertIn('= "Demo", "Demo.vcxproj"', solution)
        self.assertIn('= "CSyren", "CSyren"', solution)
        for relative in csyren.ENGINE_PROJECTS:
            name = os.path.splitext(os.path.basename(relative))[0]
            self.assertIn(f'= "{name}", "{csyren.windows(os.path.join(csyren.ENGINE_ROOT, relative))}"', solution)

    def test_generate_is_stable_and_keeps_the_user_file(self):
        csyren.new(self.folder, 'Demo', 'dev', log=QUIET)
        solution = read(self.path('Demo.sln'))
        with open(self.path('Demo.vcxproj.user'), 'w', encoding='utf-8') as handle:
            handle.write('<Project>custom</Project>')
        messages = []
        csyren.generate(self.folder, log=messages.append)
        self.assertEqual(read(self.path('Demo.sln')), solution, 'GUIDs of a regenerated solution do not change')
        self.assertEqual(read(self.path('Demo.vcxproj.user')), '<Project>custom</Project>')
        self.assertIn('up to date', messages[-1])

    def test_other_vcxproj_files_of_the_project_join_the_solution(self):
        csyren.new(self.folder, 'Demo', 'dev', log=QUIET)
        project = '<Project><PropertyGroup><ProjectGuid>{%s}</ProjectGuid></PropertyGroup></Project>'
        os.makedirs(self.path('rules'))
        with open(self.path('rules', 'rules.vcxproj'), 'w', encoding='utf-8') as handle:
            handle.write(project % '11111111-2222-3333-4444-555555555555')
        with open(self.path('build', 'stale.vcxproj'), 'w', encoding='utf-8') as handle:
            handle.write(project % '66666666-2222-3333-4444-555555555555')
        csyren.generate(self.folder, log=QUIET)
        solution = read(self.path('Demo.sln'))
        self.assertIn('= "rules", "rules\\rules.vcxproj", "{11111111-2222-3333-4444-555555555555}"', solution)
        self.assertNotIn('stale', solution)
        self.assertLess(solution.index('"Demo.vcxproj"'), solution.index('"rules\\rules.vcxproj"'))

    def test_bare_project_has_no_example(self):
        csyren.new(self.folder, 'Demo', log=QUIET, example=False)
        self.assertTrue(os.path.isfile(self.path('source', '.gitkeep')), 'source/ survives a clone')
        self.assertFalse(os.path.exists(self.path('source', 'spinner.h')))
        self.assertNotIn('SpinnerSystem', read(self.path('assets', 'app.systems')))
        scene = json.loads(read(self.path('assets', 'scenes', 'main.scene')))
        self.assertEqual([e['name'] for e in scene['entities']], ['camera', 'lighting settings', 'sun', 'ground'])
        self.assertNotIn('engine', json.loads(read(self.path('Demo.csyrenproj'))))

    def test_invalid_names_and_occupied_folders_are_rejected(self):
        with self.assertRaises(csyren.ToolError):
            csyren.new(self.folder, '2fast', log=QUIET)
        os.makedirs(self.folder)
        with open(self.path('keep.txt'), 'w') as handle:
            handle.write('x')
        with self.assertRaises(csyren.ToolError):
            csyren.new(self.folder, 'Demo', log=QUIET)


class FindProjectTest(unittest.TestCase):
    def test_a_folder_must_hold_exactly_one_project(self):
        with tempfile.TemporaryDirectory() as folder:
            with self.assertRaises(csyren.ToolError):
                csyren.find_project(folder)
            for name in ('A.csyrenproj', 'B.csyrenproj'):
                with open(os.path.join(folder, name), 'w') as handle:
                    handle.write('{"version": 1}')
            with self.assertRaises(csyren.ToolError):
                csyren.find_project(folder)
            self.assertEqual(csyren.find_project(os.path.join(folder, 'B.csyrenproj')).name, 'B')

    def test_the_engine_sandbox_is_not_regenerated(self):
        solution = os.path.join(csyren.ENGINE_ROOT, 'CSyren.sln')
        before = read(solution)
        project = csyren.generate(csyren.ENGINE_ROOT, log=QUIET)
        self.assertTrue(project.is_engine)
        self.assertEqual(read(solution), before)
        self.assertFalse(os.path.exists(os.path.join(csyren.ENGINE_ROOT, 'build', 'engine.props')))


class LocalPropsTest(unittest.TestCase):
    def test_the_registry_python_is_the_tool_python(self):
        with tempfile.TemporaryDirectory() as root:
            self.assertTrue(csyren.write_local_props(root, python=r'C:\Users\Тест & Co\python.exe'))
            text = read(os.path.join(root, 'Build', 'csyren.local.props'))
            self.assertIn('"C:\\Users\\Тест &amp; Co\\python.exe"</CSyrenPython>', text)
            self.assertIn("Condition=\"'$(CSyrenPython)'==''\"", text)
            self.assertFalse(csyren.write_local_props(root, python=r'C:\Users\Тест & Co\python.exe'))


class PackagesTest(unittest.TestCase):
    def test_missing_packages_are_listed(self):
        with tempfile.TemporaryDirectory() as root:
            os.makedirs(os.path.join(root, 'lib', 'a'))
            with open(os.path.join(root, 'lib', 'a', 'packages.config'), 'w', encoding='utf-8') as handle:
                handle.write('<packages><package id="One" version="1.0" /><package id="Two" version="2.0" /></packages>')
            os.makedirs(os.path.join(root, 'packages', 'One.1.0'))
            self.assertEqual(csyren.missing_packages(root), ['Two.2.0'])


class SubmodulesTest(unittest.TestCase):
    def test_missing_submodules_are_listed(self):
        with tempfile.TemporaryDirectory() as root:
            with open(os.path.join(root, '.gitmodules'), 'w', encoding='utf-8') as handle:
                for name in ('a', 'b', 'c'):
                    handle.write(f'[submodule "lib/{name}"]\n\tpath = lib/{name}\n\turl = https://example.com/{name}.git\n')
            os.makedirs(os.path.join(root, 'lib', 'a'))  # empty: cloned without --recurse-submodules
            os.makedirs(os.path.join(root, 'lib', 'b'))
            with open(os.path.join(root, 'lib', 'b', 'b.h'), 'w', encoding='utf-8') as handle:
                handle.write('#pragma once\n')
            self.assertEqual(csyren.missing_submodules(root), ['lib/a', 'lib/c'])
            os.remove(os.path.join(root, '.gitmodules'))
            self.assertEqual(csyren.missing_submodules(root), [])

    @unittest.skipUnless(shutil.which('git'), 'git not found')
    def test_restore_checks_out_a_missing_submodule(self):
        with tempfile.TemporaryDirectory() as temp:
            def git(folder, *arguments):
                subprocess.run(['git', '-C', folder, '-c', 'protocol.file.allow=always', '-c', 'user.name=t',
                                '-c', 'user.email=t@t'] + list(arguments), check=True, capture_output=True)

            library, engine, clone = (os.path.join(temp, name) for name in ('library', 'engine', 'clone'))
            for folder in (library, engine):
                os.makedirs(folder)
                git(folder, 'init', '-q', '-b', 'main')
            with open(os.path.join(library, 'library.h'), 'w', encoding='utf-8') as handle:
                handle.write('#pragma once\n')
            git(library, 'add', '-A')
            git(library, 'commit', '-q', '-m', 'library')
            git(engine, 'submodule', '-q', 'add', library, 'lib/third_party/library')
            git(engine, 'commit', '-q', '-m', 'engine')
            subprocess.run(['git', 'clone', '-q', engine, clone], check=True, capture_output=True)
            self.assertEqual(csyren.missing_submodules(clone), ['lib/third_party/library'])

            # the submodule URL is a local path: allow git's file transport for this test
            allow_file = {'GIT_CONFIG_COUNT': '1', 'GIT_CONFIG_KEY_0': 'protocol.file.allow', 'GIT_CONFIG_VALUE_0': 'always'}
            with mock.patch.dict(os.environ, allow_file):
                csyren.update_submodules(clone)
            self.assertEqual(csyren.missing_submodules(clone), [])
            self.assertTrue(os.path.isfile(os.path.join(clone, 'lib', 'third_party', 'library', 'library.h')))


class RunArgumentsTest(unittest.TestCase):
    """CSyrenHub runs `run --config <cfg> <project>`; older hubs put --config after the project."""

    def parse(self, *argv):
        calls = []
        with mock.patch.object(csyren, 'run', lambda project, config, arguments: calls.append((project, config, arguments))):
            self.assertEqual(csyren.main(list(argv)), 0)
        return calls[0]

    def test_config_is_read_on_either_side_of_the_project(self):
        self.assertEqual(self.parse('run', 'Demo.csyrenproj', '--config', 'Release'), ('Demo.csyrenproj', 'Release', []))
        self.assertEqual(self.parse('run', '--config', 'Release', 'Demo.csyrenproj'), ('Demo.csyrenproj', 'Release', []))
        self.assertEqual(self.parse('run', 'Demo'), ('Demo', 'Debug', []))

    def test_only_arguments_after_the_separator_reach_the_editor(self):
        self.assertEqual(self.parse('run', 'Demo', '--config', 'Release', '--', '--console'),
                         ('Demo', 'Release', ['--console']))
        with mock.patch('sys.stderr'), self.assertRaises(SystemExit):
            csyren.main(['run', 'Demo', '--console'])
        with mock.patch('sys.stderr'), self.assertRaises(SystemExit):
            csyren.main(['build', 'Demo', '--', '--console'])


class EngineManifestTest(unittest.TestCase):
    def test_the_engine_names_its_tool(self):
        manifest = csyren.engine_manifest()
        self.assertEqual(manifest['tool'], 'tools/csyren.py')
        self.assertTrue(manifest['version'])


if __name__ == '__main__':
    unittest.main()
