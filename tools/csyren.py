#!/usr/bin/env python3
"""CSyren project tool: creates projects and prepares them for this machine.

    py -3 tools/csyren.py new <folder> <Name> [--engine-id <id>] [--bare]
    py -3 tools/csyren.py generate <project>
    py -3 tools/csyren.py restore
    py -3 tools/csyren.py build <project> [--config Debug|Release]
    py -3 tools/csyren.py run <project> [--config Debug|Release] [editor arguments...]

<project> is a .csyrenproj file or the folder that holds one. The engine is the
one this script belongs to (engine.json in the folder above tools/); CSyrenHub
calls the tool of the engine a project is bound to.

A project folder:

    <Name>.csyrenproj     engine id, systems files, start scene        committed
    <Name>.vcxproj        imports CSyren.project.props / .targets      committed
    source/  assets/      code and content                             committed
    <Name>.sln            the project's .vcxproj files and the         generated
                          engine projects (solution folder "CSyren")
    <Name>.vcxproj.user   debugger arguments (kept once written)       generated
    build/engine.props    CSyrenRoot: the engine on this machine       generated
    build/                registry, binaries, cache                    generated

generate writes the generated files; new creates a project and runs generate;
build and run run generate first. The engine's own sandbox (CSyren.csyrenproj
next to CSyren.sln) builds with the committed CSyren.sln and is never
regenerated. generate and restore also write <engine>/Build/csyren.local.props:
the registry step of every build then runs this Python, not whichever "python"
comes first on PATH. restore downloads the engine's NuGet packages (msbuild
-t:restore); build runs it when packages are missing.
"""
import argparse
import json
import os
import re
import subprocess
import sys
import uuid
from xml.sax.saxutils import escape

TOOLS = os.path.dirname(os.path.abspath(__file__))
ENGINE_ROOT = os.path.dirname(TOOLS)

CPP_PROJECT_TYPE = '8BC9CEB8-8B4A-11D0-8D11-00A0C91BC942'
FOLDER_TYPE = '2150E333-8FDC-42A3-9474-1A3956D46DE8'
CONFIGURATIONS = ('Debug', 'Release')
# Engine projects a project solution needs (path relative to the engine root).
ENGINE_PROJECTS = [
    'source/csyren_host.vcxproj',
    'lib/core/core.vcxproj',
    'lib/cstdmf/cstdmf.vcxproj',
    'lib/math/math.vcxproj',
    'lib/dx12_graphic/dx12_graphic.vcxproj',
    'lib/physics/physics.vcxproj',
    'lib/JoltSolution/Jolt.vcxproj',
    'lib/imGui/imGui.vcxproj',
]
# Folders of a project that never hold its own .vcxproj files.
SKIPPED_DIRECTORIES = {'build', '.vs', 'x64', 'packages', '.git'}
SUN_ROTATION = [0.45128136651016376, -0.3798879351514666, 0.2207144081588125, 0.7767338250637598]


class ToolError(Exception):
    pass


def windows(path):
    return path.replace('/', '\\')


def write(path, text, bom=False):
    """Writes text with CRLF line ends; returns False when the file already has this content."""
    data = text.replace('\r\n', '\n').replace('\n', '\r\n').encode('utf-8')
    if bom:
        data = b'\xef\xbb\xbf' + data
    if os.path.isfile(path):
        with open(path, 'rb') as handle:
            if handle.read() == data:
                return False
    directory = os.path.dirname(path)
    if directory:
        os.makedirs(directory, exist_ok=True)
    with open(path, 'wb') as handle:
        handle.write(data)
    return True


def project_guid(vcxproj):
    with open(vcxproj, encoding='utf-8-sig', errors='replace') as handle:
        match = re.search(r'<ProjectGuid>\s*\{([0-9A-Fa-f-]+)\}\s*</ProjectGuid>', handle.read())
    if not match:
        raise ToolError(f'no ProjectGuid in {vcxproj}')
    return match.group(1).upper()


def engine_manifest(root=ENGINE_ROOT):
    path = os.path.join(root, 'engine.json')
    try:
        with open(path, encoding='utf-8') as handle:
            manifest = json.load(handle)
    except (OSError, ValueError) as error:
        raise ToolError(f'cannot read {path}: {error}')
    if not isinstance(manifest, dict) or not manifest.get('version'):
        raise ToolError(f'{path}: "version" is missing')
    return manifest


# ---------------------------------------------------------------------------
# Projects

class Project:
    def __init__(self, file):
        self.file = file
        self.folder = os.path.dirname(file)
        self.name = os.path.splitext(os.path.basename(file))[0]
        try:
            with open(file, encoding='utf-8-sig') as handle:
                self.settings = json.load(handle)
        except (OSError, ValueError) as error:
            raise ToolError(f'cannot read {file}: {error}')
        if not isinstance(self.settings, dict):
            raise ToolError(f'{file}: not a JSON object')

    @property
    def engine_id(self):
        return self.settings.get('engine', '')

    @property
    def is_engine(self):
        """The engine's own sandbox project: built with the committed CSyren.sln."""
        return os.path.normcase(os.path.abspath(self.folder)) == os.path.normcase(ENGINE_ROOT)

    @property
    def vcxproj(self):
        return os.path.join(self.folder, self.name + '.vcxproj')

    @property
    def solution(self):
        return os.path.join(ENGINE_ROOT, 'CSyren.sln') if self.is_engine else os.path.join(self.folder, self.name + '.sln')

    def executable(self, configuration):
        if self.is_engine:
            return os.path.join(ENGINE_ROOT, 'Build', configuration, 'CSyren.exe')
        return os.path.join(self.folder, 'build', 'bin', configuration, self.name + '.exe')


def find_project(path):
    path = os.path.abspath(path)
    if os.path.isdir(path):
        found = sorted(name for name in os.listdir(path) if name.lower().endswith('.csyrenproj'))
        if not found:
            raise ToolError(f'no .csyrenproj file in {path}')
        if len(found) > 1:
            raise ToolError(f'several .csyrenproj files in {path} ({", ".join(found)}): name one')
        path = os.path.join(path, found[0])
    elif not (path.lower().endswith('.csyrenproj') and os.path.isfile(path)):
        raise ToolError(f'not a .csyrenproj file or a folder with one: {path}')
    return Project(path)


def project_vcxprojs(project):
    """The project's own .vcxproj files: <Name>.vcxproj first, then any others in its folder tree."""
    others = []
    for directory, subdirs, files in os.walk(project.folder):
        subdirs[:] = sorted(d for d in subdirs if d.lower() not in SKIPPED_DIRECTORIES)
        for name in sorted(files):
            path = os.path.join(directory, name)
            if name.lower().endswith('.vcxproj') and os.path.normcase(path) != os.path.normcase(project.vcxproj):
                others.append(path)
    return [project.vcxproj] + others


# ---------------------------------------------------------------------------
# Generated files

def solution_text(name, projects, engine_projects):
    """projects / engine_projects: (name, path written into the solution, GUID)."""
    stable = lambda what: str(uuid.uuid5(uuid.NAMESPACE_URL, f'csyren:{name}:{what}')).upper()
    folder = stable('engine-folder')
    lines = ['', 'Microsoft Visual Studio Solution File, Format Version 12.00', '# Visual Studio Version 17',
             'VisualStudioVersion = 17.12.35506.116', 'MinimumVisualStudioVersion = 10.0.40219.1']
    for project_name, path, guid in projects:
        lines += [f'Project("{{{CPP_PROJECT_TYPE}}}") = "{project_name}", "{windows(path)}", "{{{guid}}}"', 'EndProject']
    lines += [f'Project("{{{FOLDER_TYPE}}}") = "CSyren", "CSyren", "{{{folder}}}"', 'EndProject']
    for project_name, path, guid in engine_projects:
        lines += [f'Project("{{{CPP_PROJECT_TYPE}}}") = "{project_name}", "{windows(path)}", "{{{guid}}}"', 'EndProject']
    lines += ['Global', '\tGlobalSection(SolutionConfigurationPlatforms) = preSolution']
    lines += [f'\t\t{c}|x64 = {c}|x64' for c in CONFIGURATIONS]
    lines += ['\tEndGlobalSection', '\tGlobalSection(ProjectConfigurationPlatforms) = postSolution']
    for _, _, guid in projects + engine_projects:
        for c in CONFIGURATIONS:
            lines += [f'\t\t{{{guid}}}.{c}|x64.ActiveCfg = {c}|x64', f'\t\t{{{guid}}}.{c}|x64.Build.0 = {c}|x64']
    lines += ['\tEndGlobalSection', '\tGlobalSection(SolutionProperties) = preSolution', '\t\tHideSolutionNode = FALSE',
              '\tEndGlobalSection', '\tGlobalSection(NestedProjects) = preSolution']
    lines += [f'\t\t{{{guid}}} = {{{folder}}}' for _, _, guid in engine_projects]
    lines += ['\tEndGlobalSection', '\tGlobalSection(ExtensibilityGlobals) = postSolution',
              f'\t\tSolutionGuid = {{{stable("solution")}}}', '\tEndGlobalSection', 'EndGlobal', '']
    return '\n'.join(lines)


def engine_props_text(engine_root, engine_id):
    root = escape(windows(os.path.join(engine_root, '')))
    return f'''<?xml version="1.0" encoding="utf-8"?>
<Project xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
  <!-- Written by tools/csyren.py generate: the CSyren engine this project builds with on this
       machine. Not in source control; CSyrenHub rewrites it when the project is opened. -->
  <PropertyGroup>
    <CSyrenRoot>{root}</CSyrenRoot>
    <CSyrenEngineId>{escape(engine_id)}</CSyrenEngineId>
  </PropertyGroup>
</Project>
'''


def local_props_text(python):
    return f'''<?xml version="1.0" encoding="utf-8"?>
<Project xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
  <!-- Written by tools/csyren.py: settings of this engine checkout on this machine (not in
       source control). The registry step of every build runs the Python the tool runs with,
       whatever "python" comes first on PATH. -->
  <PropertyGroup>
    <CSyrenPython Condition="'$(CSyrenPython)'==''">"{escape(python)}"</CSyrenPython>
  </PropertyGroup>
</Project>
'''


def write_local_props(root=ENGINE_ROOT, python=sys.executable):
    """<engine>/Build/csyren.local.props (imported by CSyren.project.props); True when it changed."""
    return write(os.path.join(root, 'Build', 'csyren.local.props'), local_props_text(python))


def vcxproj_user_text(name):
    groups = ''.join(f'''  <PropertyGroup Condition="'$(Configuration)|$(Platform)'=='{c}|x64'">
    <DebuggerFlavor>WindowsLocalDebugger</DebuggerFlavor>
    <LocalDebuggerCommandArguments>--project {name}.csyrenproj{' --console' if c == 'Debug' else ''}</LocalDebuggerCommandArguments>
    <LocalDebuggerWorkingDirectory>$(ProjectDir)</LocalDebuggerWorkingDirectory>
  </PropertyGroup>
''' for c in CONFIGURATIONS)
    return f'''<?xml version="1.0" encoding="utf-8"?>
<Project ToolsVersion="Current" xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
{groups}</Project>
'''


def generate(path, log=print):
    """Writes the files of a project that depend on this machine (see the module docstring)."""
    project = path if isinstance(path, Project) else find_project(path)
    local = write_local_props()
    if project.is_engine:
        log(f'{project.name}: the engine sandbox builds with the committed CSyren.sln'
            + ('; wrote Build/csyren.local.props' if local else ''))
        return project
    if not os.path.isfile(project.vcxproj):
        raise ToolError(f'{project.vcxproj} not found')
    projects = []
    for vcxproj in project_vcxprojs(project):
        relative = os.path.relpath(vcxproj, project.folder)
        projects.append((os.path.splitext(os.path.basename(vcxproj))[0], relative, project_guid(vcxproj)))
    engine_projects = []
    for relative in ENGINE_PROJECTS:
        vcxproj = os.path.join(ENGINE_ROOT, relative)
        if not os.path.isfile(vcxproj):
            raise ToolError(f'engine project not found: {vcxproj}')
        engine_projects.append((os.path.splitext(os.path.basename(relative))[0], vcxproj, project_guid(vcxproj)))

    written = ['<engine>/Build/csyren.local.props'] if local else []
    if write(project.solution, solution_text(project.name, projects, engine_projects), bom=True):
        written.append(os.path.basename(project.solution))
    props = os.path.join(project.folder, 'build', 'engine.props')
    if write(props, engine_props_text(ENGINE_ROOT, project.engine_id)):
        written.append('build/engine.props')
    user = project.vcxproj + '.user'
    if not os.path.isfile(user):
        write(user, vcxproj_user_text(project.name), bom=True)
        written.append(os.path.basename(user))
    engine = f'{project.engine_id} = {ENGINE_ROOT}' if project.engine_id else ENGINE_ROOT
    log(f'{project.name}: engine {engine}; ' + (f'wrote {", ".join(written)}' if written else 'up to date'))
    return project


# ---------------------------------------------------------------------------
# MSBuild

def msbuild():
    override = os.environ.get('CSYREN_MSBUILD')
    if override:
        return override
    program_files = os.environ.get('ProgramFiles(x86)', r'C:\Program Files (x86)')
    vswhere = os.path.join(program_files, 'Microsoft Visual Studio', 'Installer', 'vswhere.exe')
    if not os.path.isfile(vswhere):
        raise ToolError('vswhere.exe not found: install Visual Studio 2022 with "Desktop development with C++" '
                        '(or set CSYREN_MSBUILD to MSBuild.exe)')
    result = subprocess.run([vswhere, '-latest', '-products', '*', '-requires', 'Microsoft.Component.MSBuild',
                             '-find', r'MSBuild\**\Bin\MSBuild.exe'], capture_output=True, text=True)
    found = [line.strip() for line in result.stdout.splitlines() if line.strip()]
    if not found:
        raise ToolError('MSBuild.exe not found by vswhere (set CSYREN_MSBUILD to MSBuild.exe)')
    return found[0]


def execute(command, cwd=None):
    print('>', subprocess.list2cmdline(command), flush=True)
    result = subprocess.run(command, cwd=cwd)
    if result.returncode:
        raise ToolError(f'{os.path.basename(command[0])} failed with exit code {result.returncode}')


def missing_packages(root=ENGINE_ROOT):
    """NuGet packages listed in the engine's packages.config files that are not in <engine>/packages."""
    missing = set()
    for directory, subdirs, files in os.walk(root):
        subdirs[:] = [d for d in subdirs if d.lower() not in {'packages', 'build', '.git', '.vs', 'x64', 'third_party'}]
        if 'packages.config' not in files:
            continue
        with open(os.path.join(directory, 'packages.config'), encoding='utf-8-sig', errors='replace') as handle:
            text = handle.read()
        for package_id, version in re.findall(r'<package\s+id="([^"]+)"\s+version="([^"]+)"', text):
            if not os.path.isdir(os.path.join(root, 'packages', f'{package_id}.{version}')):
                missing.add(f'{package_id}.{version}')
    return sorted(missing)


def restore():
    write_local_props()
    execute([msbuild(), os.path.join(ENGINE_ROOT, 'CSyren.sln'), '-t:restore', '-p:RestorePackagesConfig=true',
             '-p:Configuration=Debug', '-p:Platform=x64', '-nologo', '-v:minimal'])
    missing = missing_packages()
    if missing:
        raise ToolError('packages still missing after restore: ' + ', '.join(missing) +
                        f'; build {os.path.join(ENGINE_ROOT, "CSyren.sln")} once in Visual Studio')
    print('engine packages restored')


def build(path, configuration='Debug'):
    project = generate(path)
    if missing_packages():
        restore()
    execute([msbuild(), project.solution, '-m', f'-p:Configuration={configuration}', '-p:Platform=x64',
             '-nologo', '-v:minimal'])
    return project


def run(path, configuration='Debug', arguments=()):
    project = build(path, configuration)
    executable = project.executable(configuration)
    if not os.path.isfile(executable):
        raise ToolError(f'{executable} not found after the build')
    command = [executable, '--project', project.file] + list(arguments)
    if configuration == 'Debug' and '--console' not in command:
        command.append('--console')
    print('>', subprocess.list2cmdline(command), flush=True)
    subprocess.Popen(command, cwd=project.folder)
    return project


# ---------------------------------------------------------------------------
# New project templates

CPP_KEYWORDS = {'and', 'auto', 'bool', 'break', 'case', 'catch', 'char', 'class', 'const', 'continue', 'default',
                'delete', 'do', 'double', 'else', 'enum', 'explicit', 'export', 'extern', 'false', 'float', 'for',
                'friend', 'goto', 'if', 'inline', 'int', 'long', 'module', 'namespace', 'new', 'not', 'operator',
                'or', 'private', 'protected', 'public', 'register', 'return', 'short', 'signed', 'sizeof', 'static',
                'struct', 'switch', 'template', 'this', 'throw', 'true', 'try', 'typedef', 'typename', 'union',
                'unsigned', 'using', 'virtual', 'void', 'volatile', 'while', 'csyren', 'std'}


def identifier(name):
    """C++ namespace for the project code: MySandbox -> my_sandbox."""
    text = re.sub(r'[^0-9A-Za-z_]+', '_', re.sub(r'(?<=[a-z0-9])(?=[A-Z])', '_', name)).strip('_').lower()
    if not text or text[0].isdigit():
        text = 'project_' + text
    return text + '_project' if text in CPP_KEYWORDS else text


def vcxproj_text(name, guid):
    configurations = ''.join(f'''    <ProjectConfiguration Include="{c}|x64">
      <Configuration>{c}</Configuration>
      <Platform>x64</Platform>
    </ProjectConfiguration>
''' for c in CONFIGURATIONS)
    groups = ''.join(f'''  <PropertyGroup Condition="'$(Configuration)|$(Platform)'=='{c}|x64'" Label="Configuration">
    <ConfigurationType>Application</ConfigurationType>
    <UseDebugLibraries>{'true' if c == 'Debug' else 'false'}</UseDebugLibraries>
    <PlatformToolset>v143</PlatformToolset>
    <CharacterSet>Unicode</CharacterSet>
  </PropertyGroup>
''' for c in CONFIGURATIONS)
    definitions = ''.join(f'''  <ItemDefinitionGroup Condition="'$(Configuration)|$(Platform)'=='{c}|x64'">
    <ClCompile>
      <PreprocessorDefinitions>{'_DEBUG' if c == 'Debug' else 'NDEBUG'};%(PreprocessorDefinitions)</PreprocessorDefinitions>
    </ClCompile>
  </ItemDefinitionGroup>
''' for c in CONFIGURATIONS)
    return f'''<?xml version="1.0" encoding="utf-8"?>
<Project DefaultTargets="Build" ToolsVersion="17.0" xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
  <ItemGroup Label="ProjectConfigurations">
{configurations}  </ItemGroup>
  <PropertyGroup Label="Globals">
    <VCProjectVersion>17.0</VCProjectVersion>
    <Keyword>Win32Proj</Keyword>
    <ProjectGuid>{{{guid.lower()}}}</ProjectGuid>
    <RootNamespace>{name}</RootNamespace>
    <WindowsTargetPlatformVersion>10.0.22621.0</WindowsTargetPlatformVersion>
  </PropertyGroup>
  <!-- Engine location on this machine: build\\engine.props, written by `csyren.py generate`
       (CSyrenHub runs it when it opens the project); without it, the CSYREN_ROOT environment variable. -->
  <Import Project="$(MSBuildThisFileDirectory)build\\engine.props" Condition="Exists('$(MSBuildThisFileDirectory)build\\engine.props')" />
  <PropertyGroup Condition="'$(CSyrenRoot)'=='' and '$(CSYREN_ROOT)'!=''">
    <CSyrenRoot>$([MSBuild]::EnsureTrailingSlash('$(CSYREN_ROOT)'))</CSyrenRoot>
  </PropertyGroup>
  <PropertyGroup>
    <CSyrenEngineFound Condition="'$(CSyrenRoot)'!='' and Exists('$(CSyrenRoot)CSyren.project.props')">true</CSyrenEngineFound>
  </PropertyGroup>
  <Import Project="$(VCTargetsPath)\\Microsoft.Cpp.Default.props" />
{groups}  <Import Project="$(VCTargetsPath)\\Microsoft.Cpp.props" />
  <ImportGroup Label="PropertySheets">
    <Import Project="$(UserRootDir)\\Microsoft.Cpp.$(Platform).user.props" Condition="exists('$(UserRootDir)\\Microsoft.Cpp.$(Platform).user.props')" Label="LocalAppDataPlatform" />
    <Import Project="$(CSyrenRoot)CSyren.project.props" Condition="'$(CSyrenEngineFound)'=='true'" />
  </ImportGroup>
  <PropertyGroup>
    <OutDir>$(MSBuildProjectDirectory)\\build\\bin\\$(Configuration)\\</OutDir>
    <IntDir>$(MSBuildProjectDirectory)\\build\\obj\\$(Configuration)\\</IntDir>
  </PropertyGroup>
{definitions}  <!-- Everything in source/ is part of the project; CS_* markers register it. -->
  <ItemGroup>
    <ClCompile Include="source\\**\\*.cpp" />
    <ClInclude Include="source\\**\\*.h" />
  </ItemGroup>
  <ItemGroup>
    <None Include="{name}.csyrenproj" />
    <None Include="assets\\**\\*.systems" />
    <None Include="assets\\**\\*.scene" />
  </ItemGroup>
  <Import Project="$(VCTargetsPath)\\Microsoft.Cpp.targets" />
  <Import Project="$(CSyrenRoot)CSyren.project.targets" Condition="'$(CSyrenEngineFound)'=='true'" />
  <Target Name="CSyrenEngineMissing" BeforeTargets="PrepareForBuild" Condition="'$(CSyrenEngineFound)'!='true'">
    <Error Text="CSyren engine not found (CSyrenRoot='$(CSyrenRoot)'). Open the project with CSyrenHub (csyren-hub open) or run: py -3 &lt;CSyren&gt;\\tools\\csyren.py generate $(MSBuildProjectName).csyrenproj" />
  </Target>
</Project>
'''


def spinner_header(namespace):
    return f'''#pragma once
// Example component and system (made by tools/csyren.py new). Every header in
// source/ is scanned before each build; CS_* markers register what they name.
#include <vector>
#include "core/registration.h"
#include "core/typed_system.h"
#include "core/transform.h"
#include "core/time.h"
#include "math/math.h"

CS_STRICT_SYSTEMS_BEGIN
namespace {namespace}
{{
    // Rotates the entity around `axis` (degrees per second) while playing.
    struct Spinner
    {{
        csyren::math::Vector3 axis{{ 0.0f, 1.0f, 0.0f }};
        float speed = 45.0f;

        static void describe(); // spinner.cpp
    }};
    CS_COMPONENT(Spinner);

    // The parameters of execute() are what the system reads and writes; the
    // compiler rejects anything else (docs/frame_graph.md, "Typed systems").
    class SpinnerSystem : public csyren::core::TypedSystem<SpinnerSystem>
    {{
    public:
        static constexpr auto phase = "simulation";

        void execute(csyren::core::Update<csyren::core::Transform, const Spinner> spinners,
                     csyren::core::Res<const csyren::core::Time> time)
        {{
            using namespace csyren;
            const float dt = time->deltaTime();
            _rotated.clear();
            for (auto [entity, transform, spinner] : spinners)
            {{
                transform.rotation = math::Quaternion::angleAxis(spinner.speed * dt, spinner.axis) * transform.rotation;
                _rotated.push_back(entity);
            }}
            for (auto entity : _rotated) spinners.patch<core::Transform>(entity); // publish the change
        }}

    private:
        std::vector<csyren::core::Entity::ID> _rotated;
    }};
    CS_SYSTEM(SpinnerSystem);
}}
CS_STRICT_SYSTEMS_END
'''


def spinner_source(namespace):
    return f'''#include "spinner.h"
#include "core/meta.h"

namespace {namespace}
{{
    void Spinner::describe()
    {{
        using namespace csyren::core::reflection;
        MetaFactory<Spinner>{{}}.type("Spinner"_hs)
            .data<&Spinner::axis>("axis"_hs)
            .data<&Spinner::speed>("speed"_hs);
    }}
}}
'''


def systems_files(example):
    include = lambda path: {'include': path}
    app = [include('engine:/pipelines/begin.systems')]
    if example:
        app.append({'class': 'SpinnerSystem'})
    app += [include('engine:/pipelines/simulation.systems'), include('engine:/pipelines/render.systems')]
    editor = {'systems': [include('engine:/pipelines/editor.systems')]}
    return json.dumps({'systems': app}, indent=4) + '\n', json.dumps(editor, indent=4) + '\n'


def main_scene(example):
    def transform(position, rotation=(0.0, 0.0, 0.0, 1.0), scale=(1.0, 1.0, 1.0)):
        return {'position': list(map(float, position)), 'rotation': list(map(float, rotation)), 'scale': list(map(float, scale))}
    pitch = 0.15  # sin/cos of half the camera pitch (about 17 degrees down)
    camera_rotation = [pitch, 0.0, 0.0, (1.0 - pitch * pitch) ** 0.5]
    resources = [{'id': 0, 'path': '__primitive_cube__', 'type': 'mesh'},
                 {'id': 1, 'path': '__primitive_lit_material__', 'type': 'material'}]
    entities = [
        {'id': 0, 'name': 'camera', 'components': {
            'Transform': transform((0, 3, -8), camera_rotation),
            'Camera': {'render_type': 'EachFrame', 'far': 500.0, 'near': 0.05, 'aspectRatio': 1.7777778, 'fov': 60.0,
                       'priority': 0, 'projection': 'Perspective', 'background': [0.52, 0.66, 0.86, 1.0],
                       'viewportRect': {'x': 0.0, 'y': 0.0, 'w': 1.0, 'h': 1.0}}}},
        {'id': 1, 'name': 'lighting settings', 'components': {
            'Transform': transform((0, 0, 0)),
            'LightingSettings': {'ambientColor': [0.42, 0.47, 0.58, 1.0], 'ambientIntensity': 0.35, 'exposure': 1.0,
                                 'clustered': True, 'debugView': 'Lit'}}},
        {'id': 2, 'name': 'sun', 'components': {
            'Transform': transform((0, 10, 0), SUN_ROTATION),
            'DirectionalLight': {'color': [1.0, 0.95, 0.88, 1.0], 'intensity': 3.0, 'enabled': True}}},
        {'id': 3, 'name': 'ground', 'components': {
            'Transform': transform((0, -0.1, 0), scale=(20, 0.2, 20)),
            'MeshFilter': {'mesh': 0}, 'MeshRenderer': {'material': 1}}},
    ]
    if example:
        entities.append({'id': 4, 'name': 'spinner', 'components': {
            'Transform': transform((0, 1, 0)),
            'MeshFilter': {'mesh': 0}, 'MeshRenderer': {'material': 1},
            'Spinner': {'axis': [0.0, 1.0, 0.0], 'speed': 45.0}}})
    return json.dumps({'$version': 2, 'entities': entities, 'resources': resources}, indent=4) + '\n'


GITIGNORE = '''# Generated for this machine by csyren.py generate (CSyrenHub runs it)
/*.sln
*.vcxproj.user
# Build output, generated registry, editor cache
build/
x64/
.vs/
imgui.ini
*.log
'''


def readme_text(name, engine_id):
    engine = f' bound to the engine id `{engine_id}`' if engine_id else ''
    return f'''# {name}

CSyren project{engine}.

- Open: `csyren-hub open .` ([CSyrenHub](https://github.com/zonelder/CSyrenHub)), or
  `py -3 <CSyren>\\tools\\csyren.py generate {name}.csyrenproj` and open `{name}.sln`; build x64.
  The solution, `{name}.vcxproj.user` and `build/` are generated for this machine and are not
  committed.
- `source/`: components, systems and services. Put `CS_COMPONENT(T);`, `CS_SYSTEM(T);`,
  `CS_SERVICE(T);`, `CS_EDITOR(T, Editor);` or `CS_REFLECT(T);` after the type in its header;
  the registry is regenerated before every build.
- `assets/`: scenes, models, `app.systems` / `editor.systems` (they include the engine pipeline
  from `engine:/pipelines/`).

See `docs/project_structure.md` in the engine.
'''


def new(folder, name, engine_id='', example=True, log=print):
    if not re.fullmatch(r'[A-Za-z_][A-Za-z0-9_]*', name):
        raise ToolError('the project name must be an identifier (letters, digits, underscores)')
    folder = os.path.abspath(folder)
    if os.path.exists(folder) and os.listdir(folder):
        raise ToolError(f'{folder} exists and is not empty')
    guid = str(uuid.uuid4()).upper()
    namespace = identifier(name)
    app_systems, editor_systems = systems_files(example)
    settings = {'version': 1, 'name': name}
    if engine_id:
        settings['engine'] = engine_id
    settings.update({'projectPath': '.', 'assetsPath': 'assets', 'appSystems': 'assets/app.systems',
                     'editorSystems': 'assets/editor.systems', 'startScene': 'assets/scenes/main.scene'})
    files = {
        f'{name}.csyrenproj': json.dumps(settings, indent=4) + '\n',
        f'{name}.vcxproj': vcxproj_text(name, guid),
        'assets/app.systems': app_systems,
        'assets/editor.systems': editor_systems,
        'assets/scenes/main.scene': main_scene(example),
        '.gitignore': GITIGNORE,
        'README.md': readme_text(name, engine_id),
    }
    if example:
        files['source/spinner.h'] = spinner_header(namespace)
        files['source/spinner.cpp'] = spinner_source(namespace)
    else:
        files['source/.gitkeep'] = ''  # the registry scans source/: it must exist in a fresh clone
    for relative, text in files.items():
        write(os.path.join(folder, relative), text, bom=relative.endswith('.vcxproj'))
    log(f'Created {name} in {folder}')
    return generate(os.path.join(folder, f'{name}.csyrenproj'), log=log)


# ---------------------------------------------------------------------------

def main(argv=None):
    parser = argparse.ArgumentParser(prog='csyren.py', description='CSyren project tool.')
    commands = parser.add_subparsers(dest='command', required=True)
    command = commands.add_parser('new', help='create a project folder')
    command.add_argument('folder', help='project folder (created; must be empty if it exists)')
    command.add_argument('name', help='project name (file names, solution and executable name)')
    command.add_argument('--engine-id', default='', help='engine id written into the .csyrenproj (CSyrenHub)')
    command.add_argument('--bare', action='store_true', help='no example component; the start scene has a camera, a light and the ground')
    command = commands.add_parser('generate', help='write the solution and build/engine.props of a project for this machine')
    command.add_argument('project', help='.csyrenproj file or its folder')
    commands.add_parser('restore', help="download the engine's NuGet packages")
    for name, text in (('build', 'generate and build a project'), ('run', 'build a project and start its editor')):
        command = commands.add_parser(name, help=text)
        command.add_argument('project', help='.csyrenproj file or its folder')
        command.add_argument('--config', choices=CONFIGURATIONS, default='Debug')
        if name == 'run':
            command.add_argument('arguments', nargs=argparse.REMAINDER, help='extra editor arguments')
    commands.add_parser('info', help='print engine.json as JSON (used by CSyrenHub)')
    args = parser.parse_args(argv)

    try:
        if args.command == 'new':
            new(args.folder, args.name, args.engine_id, example=not args.bare)
        elif args.command == 'generate':
            generate(args.project)
        elif args.command == 'restore':
            restore()
        elif args.command == 'build':
            build(args.project, args.config)
        elif args.command == 'run':
            run(args.project, args.config, args.arguments)
        elif args.command == 'info':
            print(json.dumps(dict(engine_manifest(), root=ENGINE_ROOT), indent=4))
    except ToolError as error:
        print(f'error: {error}', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
