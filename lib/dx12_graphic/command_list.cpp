#include "pch.h"
#include "command_list.h"


namespace csyren::render
{
	bool CommandList::init(ID3D12Device* device, D3D12_COMMAND_LIST_TYPE type)
	{
		if (DX_FAILED(device->CreateCommandAllocator(type, IID_PPV_ARGS(&pAllocator_))))
			return false;

		if (DX_FAILED(device->CreateCommandList(0, type, pAllocator_.Get(), nullptr, IID_PPV_ARGS(&pCmdList_))))
			return false;
		type_ = type;

		return DX_SUCCEEDED(pCmdList_->Close());
	}
}