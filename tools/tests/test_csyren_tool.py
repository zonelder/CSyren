"""Tests of tools/csyren.py (new / generate):  python -m unittest discover -s tools/tests"""
import json
import os
import sys
import tempfile
import unittest

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
        self.assertTrue(os.path.isdir(self.path('source')))
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


class PackagesTest(unittest.TestCase):
    def test_missing_packages_are_listed(self):
        with tempfile.TemporaryDirectory() as root:
            os.makedirs(os.path.join(root, 'lib', 'a'))
            with open(os.path.join(root, 'lib', 'a', 'packages.config'), 'w', encoding='utf-8') as handle:
                handle.write('<packages><package id="One" version="1.0" /><package id="Two" version="2.0" /></packages>')
            os.makedirs(os.path.join(root, 'packages', 'One.1.0'))
            self.assertEqual(csyren.missing_packages(root), ['Two.2.0'])


class EngineManifestTest(unittest.TestCase):
    def test_the_engine_names_its_tool(self):
        manifest = csyren.engine_manifest()
        self.assertEqual(manifest['tool'], 'tools/csyren.py')
        self.assertTrue(manifest['version'])


if __name__ == '__main__':
    unittest.main()
