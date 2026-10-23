"""Compile-time contract of typed systems (lib/core/typed_system.h):
    python -m unittest discover -s tools/tests

Each case compiles a small translation unit against the real engine headers and
checks that it is rejected with the expected diagnostic (the control case must
compile). This is how a wrong system reads under your compiler.

Compiler: CSYREN_CXX (cl, clang++ or g++), otherwise the first of cl, clang++,
g++ found on PATH (cl needs a Developer Command Prompt); the cases are skipped
when there is none, or when a header the engine includes is missing (Windows SDK,
DirectXMath). CSYREN_CXXFLAGS adds flags (searched before lib/), e.g. include
directories for DirectXMath outside the Windows SDK."""
import os
import shlex
import shutil
import subprocess
import tempfile
import textwrap
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
LIB = os.path.normpath(os.path.join(HERE, '..', '..', 'lib'))

PRELUDE = '''
#include <array>
#include "core/typed_system.h"
#include "core/spawn.h"
using namespace csyren::core;
struct A { int x = 0; };
struct B { int x = 0; };
struct Token {};
'''


def find_compiler():
    configured = os.environ.get('CSYREN_CXX')
    if configured:
        return configured
    for candidate in ('cl', 'clang++', 'g++'):
        if shutil.which(candidate):
            return candidate
    return None


def compile_source(compiler, source, defines=()):
    """Returns (ok, diagnostics) for a syntax-only compile of `source`."""
    extra = shlex.split(os.environ.get('CSYREN_CXXFLAGS', ''), posix=os.name != 'nt')
    with tempfile.TemporaryDirectory() as directory:
        path = os.path.join(directory, 'case.cpp')
        with open(path, 'w', encoding='utf-8') as handle:
            handle.write(PRELUDE + textwrap.dedent(source))
        if os.path.splitext(os.path.basename(compiler))[0].lower() == 'cl':
            forced = os.path.join(LIB, 'cstdmf', 'window_clean.h')
            command = [compiler, '/nologo', '/std:c++20', '/EHsc', '/permissive-', '/Zs', '/W3',
                       '/DNOMINMAX', '/D_DEBUG', *['/D' + d for d in defines], *extra, '/I' + LIB, '/FI' + forced, path]
        else:
            command = [compiler, '-std=c++20', '-fsyntax-only', '-D_DEBUG', *['-D' + d for d in defines], *extra, '-I' + LIB, path]
        result = subprocess.run(command, capture_output=True, text=True, errors='replace')
        return result.returncode == 0, result.stdout + result.stderr


COMPILER = find_compiler()

# Diagnostics of a missing include file (clang/gcc, cl).
MISSING_HEADER = ('file not found', 'No such file or directory', 'C1083')

VALID_SYSTEMS = '''
            CS_STRICT_SYSTEMS_BEGIN
            class Good : public TypedSystem<Good> {
            public:
                static constexpr auto phase = "simulation";
                static constexpr std::array states{ StateUse::write<Token>("", "ready") };
                void execute(Select<A>::Where<B> as, Update<B> bs, Insert<A> spawn, Delete remove, Res<const A> config) {
                    for (auto [id, a] : as) { if (auto b = bs.get<B>(id)) b->x += a.x; bs.patch<B>(id); }
                    (void)spawn.add<A>(spawn.create()); remove.destroy(0); (void)config.get();
                }
            };
            class World : public TypedSystem<World> {
            public:
                void execute(Exclusive) {}
            };
            class Setup : public OnceSystem {};
            class Spawner : public TypedSystem<Spawner> {
            public:
                void execute(Select<A> as, Spawn spawn) {
                    for (auto [id, a] : as)
                        spawn(PrefabRef{ "prefabs/x.prefab" }, { csyren::math::Vector3(float(a.x), 0, 0) })
                            .edit<B>([x = a.x](B& b) { b.x = x; })
                            .with(A{ 1 });
                }
            };
            CS_STRICT_SYSTEMS_END
            void use() { Good g; (void)g.describe(); World w; (void)w.describe(); Setup s; (void)s.scheduled(); Spawner p; (void)p.describe(); }
        '''


@unittest.skipUnless(COMPILER, 'no C++ compiler found (set CSYREN_CXX)')
class TypedContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        # The prelude alone must build; otherwise a rejected case proves nothing
        # (test_prelude_compiles reports why).
        cls.prelude_ok, cls.prelude_diagnostics = compile_source(COMPILER, 'void prelude() {}')
        if not cls.prelude_ok:
            missing = [line.strip() for line in cls.prelude_diagnostics.splitlines()
                       if any(marker in line for marker in MISSING_HEADER)]
            if missing:
                raise unittest.SkipTest('a header the engine includes is missing (Windows SDK / DirectXMath; '
                                        'CSYREN_CXXFLAGS adds include directories): ' + missing[0])

    def expect_ok(self, source, defines=()):
        ok, diagnostics = compile_source(COMPILER, source, defines)
        self.assertTrue(ok, diagnostics)

    def expect_error(self, source, *messages, defines=()):
        """Rejected, and the diagnostics contain one of `messages` (the wording
        differs between compilers, e.g. 'unused parameter' / 'C4100')."""
        if not self.prelude_ok:
            self.skipTest('the engine headers do not compile here (see test_prelude_compiles)')
        ok, diagnostics = compile_source(COMPILER, source, defines)
        self.assertFalse(ok, 'compiled, but must be rejected')
        self.assertTrue(any(message in diagnostics for message in messages),
                        f'none of {messages} in:\n{diagnostics}')

    def test_prelude_compiles(self):
        self.assertTrue(self.prelude_ok, self.prelude_diagnostics)

    def test_valid_system_compiles(self):
        self.expect_ok(VALID_SYSTEMS)

    def test_intellisense_view(self):
        # What Visual Studio IntelliSense parses (__INTELLISENSE__): the compile-only
        # machinery is hidden, the per-call checks stay.
        self.expect_ok(VALID_SYSTEMS, defines=('__INTELLISENSE__',))
        self.expect_error('''
            class S : public TypedSystem<S> { public: void execute(Select<A> as) { for (auto [id, a] : as) a.x = 1; } };
        ''', 'const-qualified', 'read-only', 'C3892', 'C2166', defines=('__INTELLISENSE__',))

    def test_get_of_unlisted_type(self):
        self.expect_error('''
            class S : public TypedSystem<S> { public: void execute(Select<A> as) { (void)as.get<B>(0); } };
            void use() { S s; s.run(); }
        ''', 'get<T>: T is not listed')

    def test_write_through_select(self):
        self.expect_error('''
            class S : public TypedSystem<S> { public: void execute(Select<A> as) { for (auto [id, a] : as) a.x = 1; } };
            void use() { S s; s.run(); }
        ''', 'const-qualified', 'read-only', 'C3892', 'C2166')

    def test_add_of_unlisted_type(self):
        self.expect_error('''
            class S : public TypedSystem<S> { public: void execute(Insert<A> spawn) { (void)spawn.add<B>(spawn.create()); } };
            void use() { S s; s.run(); }
        ''', 'add<T>: T is not listed in this Insert')

    def test_patch_without_write(self):
        self.expect_error('''
            class S : public TypedSystem<S> { public: void execute(Select<A> as) { as.patch<A>(0); } };
            void use() { S s; s.run(); }
        ''', 'patch<T>: T must be listed without const in an Update')

    def test_unused_parameter_in_strict_mode(self):
        self.expect_error('''
            CS_STRICT_SYSTEMS_BEGIN
            class S : public TypedSystem<S> { public: void execute(Select<A> as, Res<const B> unused) { for (auto [id, a] : as) (void)a; } };
            CS_STRICT_SYSTEMS_END
            void use() { S s; s.run(); }
        ''', 'unused parameter', 'C4100')

    def test_spawn_edit_takes_the_component(self):
        self.expect_error('''
            class S : public TypedSystem<S> { public: void execute(Spawn spawn) { spawn(PrefabRef{ "x" }).edit<A>([](B&) {}); } };
            void use() { S s; (void)s.describe(); }
        ''', 'edit<T>(fn): fn takes T&')

    def test_parameter_by_reference(self):
        self.expect_error('''
            class S : public TypedSystem<S> { public: void execute(Select<A>& as) { (void)as; } };
            void use() { S s; (void)s.describe(); }
        ''', 'Typed system parameters are passed by value')

    def test_unknown_parameter_kind(self):
        self.expect_error('''
            class S : public TypedSystem<S> { public: void execute(int value) { (void)value; } };
            void use() { S s; (void)s.describe(); }
        ''', 'Typed system parameters must be')

    def test_private_schedule_member(self):
        self.expect_error('''
            class S : public TypedSystem<S> {
                static constexpr auto phase = "hidden";
            public:
                void execute(Select<A> as) { (void)as; }
            };
            void use() { S s; (void)s.describe(); }
        ''', 'private')

    def test_hand_written_describe(self):
        self.expect_error('''
            class S : public TypedSystem<S> {
            public:
                void execute(Select<A> as) { (void)as; }
                TaskDescription describe() const override { return {}; }
            };
        ''', 'final')

    def test_direct_system_subclass(self):
        self.expect_error('''
            class S : public System {
            public:
                bool scheduled() const noexcept override { return true; }
                TaskDescription describe() const override { return {}; }
            private:
                void runTask() override {}
            };
            void use() { S s; (void)s; }
        ''', 'deleted', 'private', 'C2280', 'C2248')


if __name__ == '__main__':
    unittest.main()
