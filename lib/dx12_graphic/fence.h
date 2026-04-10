#pragma once
#include "cstdmf/inline_helper.h"
#include "cstdmf/assert_helper.h"
#include <cstdint>


namespace csyren::render
{
	struct FenceMask
	{
#if defined (_DEBUG)
		//variable for knowing queue that make this fence
		//usefull for searching syncronization bugs in render pipeline
		const uint32_t queueID;
#endif
		const uint64_t value;
		CS_FORCE_INLINE FenceMask next() const noexcept
		{
#if defined (_DEBUG)
			return { queueID,value };
#else
			return { value };
#endif
		}

#if defined(_DEBUG)
		CS_FORCE_INLINE bool validate(const FenceMask& other) const noexcept
		{
			CS_ASSERT(other.queueID == queueID);
			return true;
		}
#endif
	};
}