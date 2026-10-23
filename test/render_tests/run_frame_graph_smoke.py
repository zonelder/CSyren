"""Build libraries first, then run this Windows/D3D12 integration test."""
import os
from pathlib import Path
import subprocess
import sys

root = Path(__file__).resolve().parents[2]
os.chdir(root)
env = {key.upper(): value for key, value in os.environ.items()}
program_files = Path(os.environ.get('ProgramFiles(x86)', r'C:\Program Files (x86)'))
vswhere = program_files / 'Microsoft Visual Studio/Installer/vswhere.exe'
vs = Path(subprocess.check_output([str(vswhere), '-latest', '-products', '*', '-requires',
    'Microsoft.VisualStudio.Component.VC.Tools.x86.x64', '-property', 'installationPath'], text=True).strip())
msvc = max((vs / 'VC/Tools/MSVC').iterdir(), key=lambda p: tuple(map(int, p.name.split('.'))))
kits = program_files / 'Windows Kits/10'
version = max((p for p in (kits/'Include').iterdir() if (p/'um').is_dir()), key=lambda p: tuple(map(int, p.name.split('.')))).name
env['INCLUDE'] = ';'.join(str(p) for p in [msvc/'include'] + [kits/'Include'/version/p for p in ['ucrt','shared','um','winrt','cppwinrt']])
env['LIB'] = ';'.join(str(p) for p in [msvc/'lib/x64', kits/'Lib'/version/'ucrt/x64', kits/'Lib'/version/'um/x64'])
env['PATH'] = str(msvc/'bin/Hostx64/x64') + ';' + env['PATH']
# Compile options of CSyren.engine.props: /utf-8, no RTTI, window_clean.h forced, NOMINMAX.
args = [str(msvc/'bin/Hostx64/x64/cl.exe'), '/nologo', '/std:c++20', '/EHsc', '/bigobj', '/MDd',
    '/utf-8', '/GR-', '/FIlib/cstdmf/window_clean.h',
    '/D_DEBUG', '/DNOMINMAX', '/DUNICODE', '/D_UNICODE', '/Ilib',
    '/Ipackages/directxtex_uwp.2025.7.10.1/include', '/Ipackages/directxtk12_uwp.2025.7.10.1/include',
    '/Ipackages/Microsoft.Direct3D.D3D12.1.616.1/build/native/include',
    'test/render_tests/frame_graph_smoke.cpp', '/FoBuild/frame_graph_smoke.obj', '/FeBuild/Debug/frame_graph_smoke.exe',
    '/link', '/SUBSYSTEM:CONSOLE', 'Build/csyren_host/Intermediate/Debug/resource_manager.obj',
    'Build/Debug/core.lib', 'Build/Debug/math.lib', 'Build/Debug/dx12_graphic.lib',
    'packages/directxtex_uwp.2025.7.10.1/native/lib/x64/Debug/DirectXTex.lib',
    'packages/directxtk12_uwp.2025.7.10.1/native/lib/x64/Debug/DirectXTK12.lib',
    'd3d12.lib','dxgi.lib','dxguid.lib','d3dcompiler.lib','user32.lib','gdi32.lib','ole32.lib','windowscodecs.lib']
executable = root/'Build/Debug/frame_graph_smoke.exe'
if '--application' in sys.argv:
    for include in ('/Isource/host', '/Isource/editor', '/Isource/runtime', '/I.'):
        args.insert(args.index('/Ilib'), include)
    args[args.index('test/render_tests/frame_graph_smoke.cpp')] = 'test/render_tests/application_graph_smoke.cpp'
    args[args.index('/FoBuild/frame_graph_smoke.obj')] = '/FoBuild/application_graph_smoke.obj'
    args[args.index('/FeBuild/Debug/frame_graph_smoke.exe')] = '/FeBuild/Debug/application_graph_smoke.exe'
    args.remove('Build/csyren_host/Intermediate/Debug/resource_manager.obj')
    # Engine host objects (without WinMain) + the executable's own objects: the generated registry
    # and game/. Listed by name: Build/CSyren may still hold objects of files that moved to source/.
    args += [str(p.relative_to(root)) for p in (root/'Build/csyren_host/Intermediate/Debug').glob('*.obj') if p.stem.lower() != 'winmain']
    args += ['Build/CSyren/Intermediate/Debug/registry.gen.obj', 'Build/CSyren/Intermediate/Debug/game_components.obj']
    args += ['Build/Debug/physics.lib', 'Build/Debug/Jolt.lib', 'Build/Debug/imGui.lib', 'Build/Debug/cstdmf.lib']
    executable = root/'Build/Debug/application_graph_smoke.exe'
log = root/'Build/frame_graph_smoke_build.log'
with log.open('w') as output:
    result = subprocess.run(args, env=env, stdout=output, stderr=subprocess.STDOUT)
if result.returncode:
    print(log.read_text(errors='replace'))
    raise SystemExit(result.returncode)
command = [str(executable)]
if "--application" in sys.argv: command.append(str(root/"CSyren.csyrenproj"))
result = subprocess.run(command, cwd=root/'Build/Debug', env=env,
    timeout=60, capture_output=True, text=True)
(root/'Build/frame_graph_smoke_result.log').write_text(result.stdout+result.stderr)
print(result.stdout[-6000:]+result.stderr[-6000:])
raise SystemExit(result.returncode)
