# Profiler

`cstdmf/profiler.h` records where CPU time goes: named marks on every thread,
collected into frames. The editor shows them in the *profiler* window.

## Marks

```cpp
#include "cstdmf/profiler.h"

void PhysicsEngine::Impl::frame(...)
{
    CS_PROFILE_SCOPE("Physics step");          // from here to the end of the block
    ...
    CS_PROFILE_VALUE("Physics bodies", bodies.size()); // a sample of a counter
}
```

Code that marks knows nothing about threads. Each thread writes to its own ring
buffer, created on its first mark: a mark takes no lock and never waits, and no
two threads write to the same memory. When a ring is full the mark is dropped
and counted (the window shows the count). Names are interned once per call site
(`CS_PROFILE_SCOPE` keeps the id in a static); for names built at run time call
`profiler::name(text)` once and keep the id for `profiler::Scope`.

Marked automatically:

- every frame graph task, named like its node (`WeaponSystem`, `WeaponSystem/apply`);
  per-view GPU passes (`TonemapSystem@2`) inside their task;
- `Frame graph` (all systems of a frame), `Frame graph rebuild`, `Message pump`,
  `Scene session` (editor requests between frames);
- frame acquisition (`Wait GPU frame slot`, `Prepare camera views`, `Resource uploads`),
  `GPU submit`, the presentation thread (`Present`, `Output copy`), upload batches;
- physics (`Physics push transforms`, `Physics characters`, `Physics step`,
  `Physics pull transforms`, `Physics apply changes`, counters `Physics steps`,
  `Physics bodies`, `Physics rebuilt bodies`);
- the editor UI: `ImGui new frame`, `Editor UI` with `Scene workflow`, `Dockspace`,
  every window (`Window: <title>`: `Window: Scene`, `Window: Game`, `Window: inspector`,
  `Window: profiler`, ...), `Editor overlays`, and `ImGui draw`.

Threads are named tracks: `Main`, `Worker N` (job system), `Jolt N` (physics),
`Present`; other threads appear as `Thread N` (`profiler::setThreadName` names one).

## Frames and metrics

The main loop calls `profiler::beginFrame()` at the start of a frame and
`profiler::collect()` at its end. The collector (main thread) drains every ring
and assembles the frames: which mark, on which thread, when and for how long. A
mark belongs to the frame in which it started. The last 300 frames are kept.

Metrics cover the last 120 frames. A frame is counted only when the frame after
it has closed too, so marks of other threads that end up to a frame late are
included:

- per mark: time per frame, longest call, calls per frame, threads;
- per counter: last and average value;
- per thread: busy share of the frame (top-level marks).

The metrics are running totals: counting a frame adds it and retires the oldest
one, and the longest call comes from a per-mark queue of candidates, so no query
walks the 120 frames. `stats()` is assembled at most once per counted frame, when
first asked for, and `stat(id)` is a lookup into it; both return references that
stay valid until the next `beginFrame()` or `reset()`. Windows may query them
every frame.

## Editor

- *profiler* window: frame time histogram; the timeline of the newest closed frame
  (a track per thread, nested marks, Ctrl+wheel zooms, hover for details); the
  marks table (sortable, filter); thread load. *Pause* freezes the kept frames;
  pick one with the slider or *Slowest*. *Save capture* writes the kept frames
  (while paused: the frozen ones) as a Chrome trace
  (`build/profiler_capture.json` by default) that opens in
  https://ui.perfetto.dev or `chrome://tracing`.
- *systems* window: each task's time per frame in its box (current schedule).
- *statistic* window: FPS, frame time history and the CPU time of the frame graph.

The editor UI is drawn in the middle of a frame, so it always shows a closed
(previous) frame. A window that is collapsed or a tab behind another one builds
nothing (its `onFrame` is not called); the Scene and Game views are not rendered
while their windows are not visible (see [frame_graph.md](frame_graph.md),
camera views).

## Cost and switches

While enabled a scope costs two clock reads and one write to the thread's ring.
*Enabled* in the window (`profiler::setEnabled`) turns marking off at run time;
then a mark is one relaxed load. `CSYREN_PROFILER=0` compiles all marks out (the
collector stays, empty).

Not covered yet: GPU timestamps (the places are the pass command lists in
`FrameGraphBackend::recordPass`).

Tests: `test/core_tests/profiler_test.cpp` (concurrent writers while the
collector drains, frames, stats window and its longest call, exited threads,
export).
