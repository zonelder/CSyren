# JobSystem completion contract

The scheduler is ported from `E:/src/lib/cstdmf/job_system2.hpp`; the accompanying
enkiTS implementation and its license are vendored under `lib/third_party/enkiTS`.
The port adapts engine logging/asserts and namespace, fixes a zero thread count,
adds pinned jobs to the same dependency graph, and explicitly shuts workers down.

The source scheduler's integration contract requires a terminal job depending on
all work being waited for. Link dependencies first, submit roots, wait for that
terminal handle, then call `clear()`. Job objects and dependency storage must stay
alive until this wait completes. `clear()` does not wait. `fini()` requires all
previous work to have reached its terminal completion point.

Do not substitute `WaitforAll`: the source implementation documents an observed
idle-scan race where a newly awakened dependent can still be running when that
scan returns. This is the source project's reproduction, not a claim of an
upstream-confirmed defect. The upstream discussion recommends explicit completion
dependencies: https://github.com/dougbinks/enkiTS/issues/86#issuecomment-1487335696

Waiting helps execute other jobs on the calling thread. Consequently, a waiting
job must not keep shared thread-local scratch/recording state in use. FrameGraph
owns recording state per task, pins main-thread jobs to scheduler thread zero,
contains callback exceptions, and waits once at its terminal node. Nested work
must finish within the parent callback or be covered by the terminal dependency.
