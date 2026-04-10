#pragma once

#if defined(_MSC_VER)
	#define CS_FORCE_INLINE __forceinline
#elif defined(__GNUC__)
	#define CS_FORCE_INLINE [[gpu::always_inline]] inline
#else
	static_assert(false, "Unimplemented forceinline macro");
#endif