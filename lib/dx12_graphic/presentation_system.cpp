#include "pch.h"
#include "presentation_system.h"

namespace csyren::render
{
	/*
	void PresentationSystem::init(core::ServiceContext& ctx)
	{
		DXGI_SWAP_CHAIN_DESC1 desc;


		size_t presentationBuffersCount = desc.BufferCount + 2;

		D3D12_COMMAND_QUEUE_DESC qDesc = {};
		qDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
		qDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
		qDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;

		if (!_presentationQueue.init(device, qDesc))
		{
			return false;
		}

		if (!_swapChain.init(factory, device, _presentationQueue.raw(), hwnd, desc))
		{
			return false;
		}

		_presentationCommandLists.resize(desc.BufferCount);
		for (size_t i = 0; i < desc.BufferCount; ++i)
		{
			auto& cmdList = _presentationCommandLists[i];
			if (!cmdList.init(device, qDesc.Type))
				return false;
		}

		_renderTargets.resize(presentationBuffersCount);
		if (!rebuildResources())
		{
			return false;
		}
		return true;
	}

	BaseResource& PresentationSystem::currentWriteTarget()
	{
		size_t minFence = std::numeric_limits< size_t >::max();
		for (size_t i = 0; i < _renderTargets.size(); ++i)
		{
			auto& rt = _renderTargets[i];
			if (rt.fence == 0)
			{
				currentIdx_ = i;
				return rt.resource;
			}
			if (minFence > rt.fence)
			{
				currentIdx_ = i;
				minFence = rt.fence;
			}
		}

		auto& optimalRt = _renderTargets[_currentIdx];
		_presentationQueue.syncAwaitComplete(optimalRt.fence);

		optimalRt.fence = 0;

		return optimalRt.resource;
	}
	RenderTargetPtr PresentationSystem::lastWriteTarget() const noexcept
	{
		return _renderTargets[_currentIdx].resource;
	}


	bool PresentationSystem::present(RenderQueue& mainContext)
	{
		/// this only if driver is ready.if not ->discard frame.
		// how to be sure that swapChainBuffer_i are ready?
		auto idx = _swapChain.backBufferIndex();
		auto& cmdList = _presentationCommandLists[idx];
		if (!_presentationQueue.isComplete(cmdList.lastFenceValue()))
		{
			return false;
		}
		auto& currentResource = _renderTargets[_currentIdx];
		auto descResource = _swapChain.currentBackBuffer();
		_presentationQueue.awaitComplete(mainContext);
		cmdList.reset();
		cmdList.transition(D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_COPY_DEST, descResource);
		cmdList.raw()->CopyResource(descResource, currentResource.resource->raw());
		cmdList.transition(D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PRESENT, descResource);
		cmdList.close();

		_presentationQueue.addCommands(&cmdList);
		_presentationQueue.execute();
		currentResource.fence = cmdList.lastFenceValue();
		_swapChain.present(0, 0);
		cmdList.signalFence(_presentationQueue.signal());
		return true;
	}

	bool PresentationSystem::rebuildResources()
	{
		RenderTargetDesc desc;
		auto meta = swapChain_.meta();
		desc.width = meta.width;
		desc.height = meta.height;
		desc.format = meta.format;
		for (uint32 i = 0; i < _renderTargets.size(); ++i)
		{
			renderTargets_[i] = { 0, new RenderTarget(fmt::format("swap chain backBuffer {}", i)) };
			renderTargets_[i].resource->create(device_, desc);
		}

		return true;
	}

	bool PresentationSystem::resize(size_t width, size_t height, bool windowed)
	{
		_presentationQueue.syncAwaitLastComplete();

		if (!_swapChain.resize(width, height, windowed))
		{
			return false;
		}
		if (!rebuildResources())
		{
			return false;
		}
		return true;
	}
	*/
} 