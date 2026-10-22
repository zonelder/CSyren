#!/usr/bin/env python3
"""Creates a CSyren project folder:

    python tools/new_project.py <folder> <Name> [--engine <path to CSyren>]

    <folder>/
      <Name>.csyrenproj      project settings (systems files, start scene)
      <Name>.sln             engine projects + the project executable
      <Name>.vcxproj         imports CSyren.project.props / .targets; compiles source/**
      <Name>.vcxproj.user    Debug: --project <Name>.csyrenproj --console
      source/                components, systems, services with CS_* markers
      assets/                app.systems, editor.systems, scenes/main.scene ("project:" mount)
      .gitignore

Build <Name>.sln in Visual Studio (x64). Everything in source/ is compiled
(wildcard) and registered (tools/csyren_registry.py runs before each build).
The engine is found through CSyrenRoot: the path given here (stored relative
when possible), overridable with the CSYREN_ROOT environment variable.
"""
import argparse
import json
import os
import re
import sys
import uuid

CPP_PROJECT_TYPE = '8BC9CEB8-8B4A-11D0-8D11-00A0C91BC942'
FOLDER_TYPE = '2150E333-8FDC-42A3-9474-1A3956D46DE8'
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
SUN_ROTATION = [0.45128136651016376, -0.3798879351514666, 0.2207144081588125, 0.7767338250637598]


def windows(path):
    return path.replace('/', '\\')


def project_guid(vcxproj):
    with open(vcxproj, encoding='utf-8-sig', errors='replace') as handle:
        match = re.search(r'<ProjectGuid>\s*\{([0-9A-Fa-f-]+)\}\s*</ProjectGuid>', handle.read())
    if not match:
        raise SystemExit(f'error: no ProjectGuid in {vcxproj}')
    return match.group(1).upper()


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


def write(path, text, bom=False):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, 'w', encoding='utf-8-sig' if bom else 'utf-8', newline='\r\n') as handle:
        handle.write(text)


# ---------------------------------------------------------------------------
# File templates

def vcxproj(name, guid, engine_relative):
    configurations = ''.join(f'''    <ProjectConfiguration Include="{c}|x64">
      <Configuration>{c}</Configuration>
      <Platform>x64</Platform>
    </ProjectConfiguration>
''' for c in ('Debug', 'Release'))
    groups = ''.join(f'''  <PropertyGroup Condition="'$(Configuration)|$(Platform)'=='{c}|x64'" Label="Configuration">
    <ConfigurationType>Application</ConfigurationType>
    <UseDebugLibraries>{'true' if c == 'Debug' else 'false'}</UseDebugLibraries>
    <PlatformToolset>v143</PlatformToolset>
    <CharacterSet>Unicode</CharacterSet>
  </PropertyGroup>
''' for c in ('Debug', 'Release'))
    definitions = ''.join(f'''  <ItemDefinitionGroup Condition="'$(Configuration)|$(Platform)'=='{c}|x64'">
    <ClCompile>
      <PreprocessorDefinitions>{'_DEBUG' if c == 'Debug' else 'NDEBUG'};%(PreprocessorDefinitions)</PreprocessorDefinitions>
    </ClCompile>
  </ItemDefinitionGroup>
''' for c in ('Debug', 'Release'))
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
    <!-- Engine location: CSYREN_ROOT environment variable, otherwise the path given to new_project.py. -->
    <CSyrenRoot Condition="'$(CSyrenRoot)'=='' and '$(CSYREN_ROOT)'!=''">$([MSBuild]::EnsureTrailingSlash('$(CSYREN_ROOT)'))</CSyrenRoot>
    <CSyrenRoot Condition="'$(CSyrenRoot)'==''">$([System.IO.Path]::GetFullPath('$(MSBuildThisFileDirectory){engine_relative}'))</CSyrenRoot>
  </PropertyGroup>
  <Import Project="$(VCTargetsPath)\\Microsoft.Cpp.Default.props" />
{groups}  <Import Project="$(VCTargetsPath)\\Microsoft.Cpp.props" />
  <ImportGroup Label="PropertySheets">
    <Import Project="$(UserRootDir)\\Microsoft.Cpp.$(Platform).user.props" Condition="exists('$(UserRootDir)\\Microsoft.Cpp.$(Platform).user.props')" Label="LocalAppDataPlatform" />
    <Import Project="$(CSyrenRoot)CSyren.project.props" />
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
  <Import Project="$(CSyrenRoot)CSyren.project.targets" />
</Project>
'''


def vcxproj_user(name):
    return f'''<?xml version="1.0" encoding="utf-8"?>
<Project ToolsVersion="Current" xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
  <PropertyGroup Condition="'$(Configuration)|$(Platform)'=='Debug|x64'">
    <DebuggerFlavor>WindowsLocalDebugger</DebuggerFlavor>
    <LocalDebuggerCommandArguments>--project {name}.csyrenproj --console</LocalDebuggerCommandArguments>
    <LocalDebuggerWorkingDirectory>$(ProjectDir)</LocalDebuggerWorkingDirectory>
  </PropertyGroup>
  <PropertyGroup Condition="'$(Configuration)|$(Platform)'=='Release|x64'">
    <DebuggerFlavor>WindowsLocalDebugger</DebuggerFlavor>
    <LocalDebuggerCommandArguments>--project {name}.csyrenproj</LocalDebuggerCommandArguments>
    <LocalDebuggerWorkingDirectory>$(ProjectDir)</LocalDebuggerWorkingDirectory>
  </PropertyGroup>
</Project>
'''


def solution(name, guid, engine_projects):
    folder = str(uuid.uuid4()).upper()
    lines = ['', 'Microsoft Visual Studio Solution File, Format Version 12.00', '# Visual Studio Version 17',
             'VisualStudioVersion = 17.12.35506.116', 'MinimumVisualStudioVersion = 10.0.40219.1',
             f'Project("{{{CPP_PROJECT_TYPE}}}") = "{name}", "{name}.vcxproj", "{{{guid}}}"', 'EndProject',
             f'Project("{{{FOLDER_TYPE}}}") = "CSyren", "CSyren", "{{{folder}}}"', 'EndProject']
    for project_name, relative, project_guid_value in engine_projects:
        lines += [f'Project("{{{CPP_PROJECT_TYPE}}}") = "{project_name}", "{windows(relative)}", "{{{project_guid_value}}}"', 'EndProject']
    lines += ['Global', '\tGlobalSection(SolutionConfigurationPlatforms) = preSolution',
              '\t\tDebug|x64 = Debug|x64', '\t\tRelease|x64 = Release|x64', '\tEndGlobalSection',
              '\tGlobalSection(ProjectConfigurationPlatforms) = postSolution']
    for value in [guid] + [g for _, _, g in engine_projects]:
        for configuration in ('Debug', 'Release'):
            lines += [f'\t\t{{{value}}}.{configuration}|x64.ActiveCfg = {configuration}|x64',
                      f'\t\t{{{value}}}.{configuration}|x64.Build.0 = {configuration}|x64']
    lines += ['\tEndGlobalSection', '\tGlobalSection(SolutionProperties) = preSolution', '\t\tHideSolutionNode = FALSE',
              '\tEndGlobalSection', '\tGlobalSection(NestedProjects) = preSolution']
    lines += [f'\t\t{{{g}}} = {{{folder}}}' for _, _, g in engine_projects]
    lines += ['\tEndGlobalSection', '\tGlobalSection(ExtensibilityGlobals) = postSolution',
              f'\t\tSolutionGuid = {{{str(uuid.uuid4()).upper()}}}', '\tEndGlobalSection', 'EndGlobal', '']
    return '\n'.join(lines)


def spinner_header(namespace):
    return f'''#pragma once
// Example component and system (made by tools/new_project.py). Every header in
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


def systems_files():
    include = lambda path: {'include': path}
    app = {'systems': [include('engine:/pipelines/begin.systems'), {'class': 'SpinnerSystem'},
                       include('engine:/pipelines/simulation.systems'), include('engine:/pipelines/render.systems')]}
    editor = {'systems': [include('engine:/pipelines/editor.systems')]}
    return json.dumps(app, indent=4) + '\n', json.dumps(editor, indent=4) + '\n'


def main_scene():
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
        {'id': 4, 'name': 'spinner', 'components': {
            'Transform': transform((0, 1, 0)),
            'MeshFilter': {'mesh': 0}, 'MeshRenderer': {'material': 1},
            'Spinner': {'axis': [0.0, 1.0, 0.0], 'speed': 45.0}}},
    ]
    return json.dumps({'$version': 2, 'entities': entities, 'resources': resources}, indent=4) + '\n'


GITIGNORE = '''# Build output, generated registry, editor cache
build/
x64/
.vs/
imgui.ini
*.log
'''


def readme(name, engine):
    return f'''# {name}

CSyren project. Engine: `{engine}` (override with the `CSYREN_ROOT` environment variable).

- `source/` — components, systems and services. Put `CS_COMPONENT(T);`, `CS_SYSTEM(T);`,
  `CS_SERVICE(T);`, `CS_EDITOR(T, Editor);` or `CS_REFLECT(T);` after the type in its header;
  the registry is regenerated before every build.
- `assets/` — scenes, models, `app.systems` / `editor.systems` (they include the engine pipeline
  from `engine:/pipelines/`).
- Open `{name}.sln`, build x64. Debug runs with `--project {name}.csyrenproj --console`.

See `docs/project_structure.md` in the engine.
'''


def main(argv=None):
    parser = argparse.ArgumentParser(description='Creates a CSyren project folder.')
    parser.add_argument('folder', help='project folder (created; must be empty if it exists)')
    parser.add_argument('name', help='project name (file names, solution and executable name)')
    parser.add_argument('--engine', default=os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'),
                        help='CSyren engine root (default: the engine this script belongs to)')
    args = parser.parse_args(argv)

    if not re.fullmatch(r'[A-Za-z_][A-Za-z0-9_]*', args.name):
        raise SystemExit('error: the project name must be an identifier (letters, digits, underscores)')
    folder = os.path.abspath(args.folder)
    engine = os.path.abspath(args.engine)
    if not os.path.isfile(os.path.join(engine, 'CSyren.project.props')):
        raise SystemExit(f'error: {engine} is not a CSyren engine root (no CSyren.project.props)')
    if os.path.exists(folder) and os.listdir(folder):
        raise SystemExit(f'error: {folder} exists and is not empty')

    try:
        engine_relative = os.path.relpath(engine, folder)
    except ValueError:  # another drive
        engine_relative = engine
    engine_relative = windows(engine_relative).rstrip('\\') + '\\'
    engine_projects = []
    for relative in ENGINE_PROJECTS:
        path = os.path.join(engine, relative)
        if not os.path.isfile(path):
            raise SystemExit(f'error: engine project not found: {path}')
        try:
            from_solution = os.path.relpath(path, folder)
        except ValueError:
            from_solution = path
        engine_projects.append((os.path.splitext(os.path.basename(relative))[0], from_solution, project_guid(path)))

    guid = str(uuid.uuid4()).upper()
    namespace = identifier(args.name)
    app_systems, editor_systems = systems_files()
    files = {
        f'{args.name}.csyrenproj': json.dumps({
            'version': 1, 'name': args.name, 'projectPath': '.', 'assetsPath': 'assets',
            'appSystems': 'assets/app.systems', 'editorSystems': 'assets/editor.systems',
            'startScene': 'assets/scenes/main.scene'}, indent=4) + '\n',
        f'{args.name}.vcxproj': vcxproj(args.name, guid, engine_relative),
        f'{args.name}.vcxproj.user': vcxproj_user(args.name),
        'source/spinner.h': spinner_header(namespace),
        'source/spinner.cpp': spinner_source(namespace),
        'assets/app.systems': app_systems,
        'assets/editor.systems': editor_systems,
        'assets/scenes/main.scene': main_scene(),
        '.gitignore': GITIGNORE,
        'README.md': readme(args.name, engine_relative),
    }
    for relative, text in files.items():
        write(os.path.join(folder, relative), text, bom=relative.endswith(('.vcxproj', '.user')))
    write(os.path.join(folder, f'{args.name}.sln'), solution(args.name, guid, engine_projects), bom=True)
    print(f'Created {args.name} in {folder}')
    print(f'  engine: {engine_relative}  ({engine})')
    print(f'  open {os.path.join(folder, args.name + ".sln")} and build x64')
    return 0


if __name__ == '__main__':
    sys.exit(main())
