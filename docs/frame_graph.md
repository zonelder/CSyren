# Systems and frame scheduling

Systems implement one `execute()` method. `init()` and `shutdown()` run on the
owning main thread outside the frame graph. Describe accesses explicitly:

```cpp
TaskDescription describe() const override {
    TaskDescription d;
    d.phase = "simulation";
    d.read<Velocity>().write<Position>().readResource<Time>();
    d.after = {"InputSystem"};
    return d;
}

void execute() override {
    const auto* time = Services::get<const Time>();
    for (auto [id, position, velocity] :
         Services::get<Scene>()->view<Position, const Velocity>()) {
        position.x += velocity.x * time->deltaTime();
    }
}
```

`system.config` declares arbitrary string phases and their dependency graph:

```json
"phases": [
    { "name": "input" },
    { "name": "simulation", "after": ["input"] }
]
```

There is no built-in phase enumeration. Unrelated phases may overlap. A phase
dependency includes every task in the predecessor, including structural commits.
`before` and `after` system dependencies take precedence over registration order.
Registration order breaks otherwise unordered read/write conflicts. Read/read
accesses and disjoint component/resource accesses can run concurrently. Moving a
system in the editor changes this tie-breaking priority, not its explicit
dependencies or phase.

CPU resource state names are strings. `readResource<T>("ready")` requires that
state; `writeResource<T>("empty", "ready")` describes a state transition. Declare
external initial states with `SystemManager::resourceState<T>("empty")`. A unique
state producer becomes a dependency even when registered later. Unknown names,
cycles, duplicate accesses and missing/ambiguous producers are configuration
errors before execution starts. States are logical scheduling contracts; the
system must actually establish the state it declares. Initial CPU states describe
the externally established condition at the start of each execution.

## Scene changes

Set `structuralChanges = true` when creating/destroying entities or adding/removing
components. Keep using `createEntity`, `addComponent`, `removeComponent` and
`destroyEntity`; no command-buffer API is needed in system code. The returned
entity ID is final, and a returned component reference is immediately writable.
`getComponent` and `hasComponent` see that task's staged component changes.

New views also include that task's staged components. Each view keeps a stable
candidate snapshot during iteration; structural edits cannot relocate its live
storage. Other tasks observe changes after the automatic commit. The graph inserts this commit and
includes it in an `after` dependency on the producing system. Calling `flush()`
inside a task cannot relocate storage under a live view. Disjoint structural
tasks can prepare changes concurrently. Applying changes requires a safe point
for all scene users because the existing dense storage and pool registry can
relocate. Raw component pointers/references and views must not escape a task;
retain `ComponentRef` or entity IDs instead. `ComponentRef` checks the component's
incarnation and cannot retarget a reused ID.

Read components through `view<const T>` / `getComponent<const T>`, and read services
through `Services::get<const T>()`. Undeclared mutable accesses are rejected.
Declare `readResource<SceneEntities>()` to enumerate entity metadata. Reflection
and editor tasks use `exclusiveScene` and `dynamicResources` because their actual
types are known at runtime; these flags introduce the necessary exclusive edges.
They are not the default for ordinary systems.

## GPU recording and submission

```cpp
d.gpu("frame.color", D3D12_RESOURCE_STATE_RENDER_TARGET, AccessMode::Write);
d.gpu("frame.depth", D3D12_RESOURCE_STATE_DEPTH_WRITE, AccessMode::Write);
```

GPU access ordering is separate from CPU recording dependencies. Each GPU task
gets its own command list, allocator, upload memory, material constants and engine
parameters. `Renderer` routes recording calls to this task-owned storage; it does
not expose frame/output ownership. GPU barriers and interqueue fence waits are
generated from declarations. CPU completion does not imply GPU completion.
Allocator/upload reuse waits for the corresponding retired frame slot only.
Graph edits drain old GPU storage before replacing it.

Use `GpuResources::import` during initialization for persistent resources, supplying
their actual initial and exported final D3D12 states. The registry retains COM
ownership. One-time initialization establishes the final state, and each frame's
plan returns resources to that state. Imports and queue declarations are sealed
when the graph starts. `gpuQueue` selects a named queue registered with
`GpuResources::queue`; `graphics` is provided by the backend. This backend
currently accepts **direct queues**. Compute/copy queues need explicit capability
and ownership-transfer rules before being enabled. Resource states currently
cover the whole resource, not individual mip/array subresources.

`FrameAcquireSystem` owns frame-slot/mailbox acquisition. Its private recording
state is automatically added as a dependency of GPU tasks. `PresentationSystem`
submits the GPU plan and publishes a completed render fence to the mailbox.
The output worker retains the swap-chain-associated presentation queue and never
moves backbuffer writes to another queue. ImGui and window systems use
`TaskAffinity::MainThread`. Jolt currently retains its own four worker threads;
the engine job pool reserves a bounded CPU budget alongside them.

## Completion and failures

All dependencies are linked before any root is submitted. The scheduler waits for
one explicit terminal job covering the whole graph, then clears wrappers. It does
not use `WaitforAll`. Exceptions are contained inside jobs; remaining jobs drain
before the exception returns to the owner. Unapplied scene changes are discarded.
Writes already made to existing component values are not rolled back.

Do not start uncovered asynchronous/nested jobs from a system, keep thread-local
scratch across a scheduler wait, or mutate service/phase/system registries while
a graph is running. Job-local scopes are restored with RAII. Existing services
may hold internal worker pools, but those operations must finish before their
system callback completes.

## Validation

`core_tests` covers graph compilation, state producers, hazards, affinity,
exception draining, scene commits, component generations and GPU planning.
After building Debug libraries and the application, run
`python test/render_tests/run_frame_graph_smoke.py` for the Windows/D3D12 test.
It checks parallel recording, two actual queues, GPU readback, background-color
changes and recompilation while previous GPU work is in flight, with the debug
layer and GPU validator enabled.
