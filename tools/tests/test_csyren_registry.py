"""Tests for tools/csyren_registry.py:  python -m unittest discover -s tools/tests

The compile test builds the generated registry against small stand-ins of the
engine headers with a C++20 compiler on PATH (g++/clang++), and is skipped
when none is available (a compiler without -std=c++20 counts as none)."""
import os
import shutil
import subprocess
import sys
import tempfile
import textwrap
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
sys.path.insert(0, HERE)
import csyren_registry as registry  # noqa: E402
import cxx_compiler  # noqa: E402

REGISTRATION_H = os.path.normpath(os.path.join(HERE, '..', '..', 'lib', 'core', 'registration.h'))

STUBS = {
    'core/component_registry.h': '''
        #pragma once
        #include <string>
        #include <vector>
        namespace csyren::core::reflection {
            inline std::vector<std::string>& registeredComponents() { static std::vector<std::string> v; return v; }
            template<class T> struct ComponentRegistrar { explicit ComponentRegistrar(const char* name) { registeredComponents().push_back(name); } };
        }
    ''',
    'core/meta.h': '''
        #pragma once
        namespace csyren::core::reflection {
            struct MetaType {};
            template<class T> MetaType* resolve() noexcept { static MetaType type; return &type; }
            namespace details { template<class T> void tryDescribe() { if constexpr (requires { T::describe(); }) T::describe(); } }
        }
    ''',
    'core/services.h': '''
        #pragma once
        #include <vector>
        #include <string>
        namespace csyren::core::details {
            struct ServiceRegistry {
                static std::vector<std::string>& created() { static std::vector<std::string> v; return v; }
                template<class T> static T& create() { static T value; created().push_back(T::serviceName); return value; }
            };
        }
    ''',
    'core/system_base.h': '''
        #pragma once
        #include <functional>
        #include <map>
        #include <memory>
        #include <string>
        namespace csyren::core {
            struct System { virtual ~System() = default; };
            namespace details {
                struct SystemRegistry {
                    static SystemRegistry& instance() { static SystemRegistry r; return r; }
                    void registerSystem(const char* name, std::function<std::shared_ptr<System>()> f) { creators[name] = std::move(f); }
                    std::map<std::string, std::function<std::shared_ptr<System>()>> creators;
                };
            }
        }
    ''',
    'editor_base.h': '''
        #pragma once
        #include "core/meta.h"
        #include <vector>
        namespace csyren::editor {
            struct IEditor { virtual ~IEditor() = default; };
            struct EditorRegistry {
                static std::vector<void(*)()>& pending() { static std::vector<void(*)()> p; return p; }
                static std::vector<IEditor*>& added() { static std::vector<IEditor*> a; return a; }
                static void addPending(void (*fn)()) { pending().push_back(fn); }
                static void add(core::reflection::MetaType*, IEditor* editor) { added().push_back(editor); }
            };
        }
    ''',
}

GAME_H = '''
    #pragma once
    #include "core/registration.h"
    #include "core/system_base.h"
    #include "editor_base.h"
    // CS_COMPONENT(Commented) must be ignored, as well as strings:
    inline const char* text = "CS_COMPONENT(InString) { }";
    inline const char* raw = R"x( } { CS_SYSTEM(InRaw) )x";
    namespace game
    {
        struct Health { float value = 100; static void describe() {} };
        CS_COMPONENT(Health);
        namespace ai
        {
            struct Brain { };
            CS_COMPONENT(Brain);
            struct BrainSystem : csyren::core::System { };
            CS_SYSTEM(BrainSystem);
        }
        struct Score { static constexpr const char* serviceName = "Score"; };
        CS_SERVICE(Score);
        struct HealthEditor : csyren::editor::IEditor { };
        CS_EDITOR(Health, HealthEditor);
    }
    struct GlobalType { static void describe() {} };
    CS_REFLECT(GlobalType);
'''


def write(root, relative, text):
    path = os.path.join(root, relative)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, 'w', encoding='utf-8') as handle:
        handle.write(textwrap.dedent(text))
    return path


class RegistryTest(unittest.TestCase):
    def setUp(self):
        self.root = tempfile.mkdtemp(prefix='csyren-registry-')
        self.addCleanup(shutil.rmtree, self.root, True)
        os.makedirs(os.path.join(self.root, 'engine', 'core'))
        shutil.copy(REGISTRATION_H, os.path.join(self.root, 'engine', 'core', 'registration.h'))
        for relative, text in STUBS.items():
            write(os.path.join(self.root, 'engine'), relative, text)
        self.out = os.path.join(self.root, 'build', 'generated', 'registry.gen.cpp')

    def run_registry(self, *roots):
        args = ['--out', self.out, '--roots', ';'.join(roots),
                '--include-dirs', ';'.join([os.path.join(self.root, 'engine'), os.path.join(self.root, 'source')])]
        return registry.main(args)

    def read(self):
        with open(self.out, encoding='utf-8') as handle:
            return handle.read()

    def test_markers_namespaces_and_ignored_text(self):
        write(self.root, 'source/game.h', GAME_H)
        self.assertEqual(self.run_registry(os.path.join(self.root, 'source')), 0)
        text = self.read()
        self.assertIn('#include "game.h"', text)
        self.assertIn('ComponentRegistrar<Health> ', text)
        self.assertIn('("Health")', text)
        self.assertIn('namespace game::ai', text)
        self.assertIn('registerSystem("BrainSystem"', text)
        self.assertIn('ServiceRegistry::create<Score>()', text)
        self.assertIn('resolve<Health>()', text)
        self.assertIn('tryDescribe<GlobalType>()', text)
        for ignored in ('Commented', 'InString', 'InRaw'):
            self.assertNotIn(ignored, text)

    def test_shadowed_include_uses_path_from_generated_file(self):
        # engine/ comes first in the include directories, so "game.h" would find its header.
        write(self.root, 'engine/game.h', '#pragma once\n')
        write(self.root, 'source/game.h', GAME_H)
        self.assertEqual(self.run_registry(os.path.join(self.root, 'source')), 0)
        text = self.read()
        self.assertIn('#include "../../source/game.h"', text)
        self.assertNotIn('#include "game.h"', text)

    def test_unchanged_output_is_not_rewritten(self):
        write(self.root, 'source/game.h', GAME_H)
        self.assertEqual(self.run_registry(os.path.join(self.root, 'source')), 0)
        stamp = os.stat(self.out).st_mtime_ns
        os.utime(self.out, ns=(stamp - 10**9, stamp - 10**9))
        self.assertEqual(self.run_registry(os.path.join(self.root, 'source')), 0)
        self.assertEqual(os.stat(self.out).st_mtime_ns, stamp - 10**9)

    def test_errors(self):
        cases = {
            'class scope': 'namespace a { struct S { struct T {}; CS_COMPONENT(T); }; }',
            'function scope': 'namespace a { struct T {}; inline void f() { CS_COMPONENT(T); } }',
            'anonymous namespace': 'namespace { struct T {}; CS_COMPONENT(T); }',
            'duplicate name': 'namespace a { struct T {}; CS_COMPONENT(T); } namespace b { struct T {}; CS_COMPONENT(T); }',
            'argument count': 'namespace a { struct T {}; CS_EDITOR(T); }',
        }
        for name, body in cases.items():
            with self.subTest(name):
                path = write(self.root, f'source/{name.replace(" ", "_")}.h', '#pragma once\n#include "core/registration.h"\n' + body + '\n')
                self.assertEqual(self.run_registry(path), 1)

    def test_missing_root_fails(self):
        self.assertEqual(self.run_registry(os.path.join(self.root, 'nowhere')), 1)

    def test_generated_code_compiles(self):
        candidates = ('g++', 'clang++')
        compiler, passed_over = cxx_compiler.find_cpp20(candidates)
        if not compiler:
            self.skipTest(cxx_compiler.missing_reason(candidates, passed_over))
        write(self.root, 'source/game.h', GAME_H)
        self.assertEqual(self.run_registry(os.path.join(self.root, 'source')), 0)
        main = write(self.root, 'main.cpp', '''
            #include "core/registration.h"
            #include "core/component_registry.h"
            #include "core/services.h"
            #include "core/system_base.h"
            #include "editor_base.h"
            int main()
            {
                csyren::generated::registerTypes();
                csyren::generated::createServices();
                using namespace csyren;
                const auto& components = core::reflection::registeredComponents();
                const bool ok = components.size() == 2 && components[0] == "Health" && components[1] == "Brain"
                    && core::details::SystemRegistry::instance().creators.count("BrainSystem") == 1
                    && core::details::ServiceRegistry::created().size() == 1
                    && editor::EditorRegistry::pending().size() == 1;
                for (auto* fn : editor::EditorRegistry::pending()) fn();
                return ok && editor::EditorRegistry::added().size() == 1 ? 0 : 1;
            }
        ''')
        binary = os.path.join(self.root, 'registry_test')
        result = subprocess.run([compiler, '-std=c++20', '-I', os.path.join(self.root, 'engine'),
                                 '-I', os.path.join(self.root, 'source'), main, self.out, '-o', binary],
                                capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(subprocess.run([binary]).returncode, 0)


if __name__ == '__main__':
    unittest.main()
