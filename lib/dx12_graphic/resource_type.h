#ifndef __CSYREN_DX12_RESOURCE_TYPE__
#define __CSYREN_DX12_RESORUCE_TYPE__

namespace csyren::render
{
	using raw_resource_id = uitn64_t;
	template<class T>
	struct UniqueResourceID
	{
		raw_resource_id raw;
		bool operator==(UniqueResourceID other) const noexcept { return raw == other.raw; }
		bool operator!=(UniqueResourceID other) const noexcept { return raw != other.raw; }
	}
}

#endif

