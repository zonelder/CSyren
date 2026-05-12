#ifndef __COMMAND_LIST_IPP__
#define __COMMAND_LIST_IPP__

namespace csyren::render
{
	CS_FORCE_INLINE void CommandList::reset()
	{
		CS_ASSERT(SUCCEEDED(pAllocator_->Reset()));
		CS_ASSERT(SUCCEEDED(pCmdList_->Reset(pAllocator_.Get(), nullptr)));

		state_ = {};
		fenceValue_ = 0;
#ifndef CONSUMER_CLIENT

		statistic_.reset();
#endif

	}
	CS_FORCE_INLINE void CommandList::close()
	{
		CS_ASSERT(SUCCEEDED(pCmdList_->Close()));
	}

	CS_FORCE_INLINE void CommandList::setRenderTarget(uint8_t slot, D3D12_CPU_DESCRIPTOR_HANDLE rtv)
	{
		CS_ASSERT(slot < D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT);
		CS_ASSERT(rtv.ptr != NULL);
		CS_ASSERT(slot <= state_.rtvCount);

		state_.rtvs[slot] = rtv;
		state_.rtvCount = std::max(slot, state_.rtvCount);

	}


	CS_FORCE_INLINE void CommandList::shrinkRenderTargets(uint8_t maxSlot)
	{
		std::memset(&state_.rtvs[maxSlot], 0, (D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT - maxSlot) * sizeof(D3D12_CPU_DESCRIPTOR_HANDLE));
		state_.rtvCount = maxSlot;
	}

	CS_FORCE_INLINE void CommandList::submitRenderTargets()
	{
		//TODO(dx12) не нравиться мне это. но может и не плохо.

		uint32_t mask = 0;

		for (size_t i = 0; i < D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i)
		{
			mask |= (state_.rtvs[i].ptr != NULL) << i;
		}
		uint32_t count = std::countr_one(mask);
		pCmdList_->OMSetRenderTargets(static_cast<UINT>(count), state_.rtvs, FALSE, state_.dsv.ptr ? &state_.dsv : nullptr);
	}

	CS_FORCE_INLINE void CommandList::setDepthStencil(D3D12_CPU_DESCRIPTOR_HANDLE dsv)
	{
		state_.dsv = dsv;
	}

	CS_FORCE_INLINE void CommandList::setPipelineState(ID3D12PipelineState* pso)
	{
		pCmdList_->SetPipelineState(pso);
	}

	CS_FORCE_INLINE void CommandList::setRootSignature(ID3D12RootSignature* rootSig)
	{
		pCmdList_->SetGraphicsRootSignature(rootSig);
	}

	CS_FORCE_INLINE void CommandList::clearRenderTarget(uint8_t slot, const math::Vector3& color)
	{
		float col[4] = { color.x, color.y, color.z, 1.0f };
		clearRenderTarget(slot, col);
	}

	CS_FORCE_INLINE void CommandList::clearRenderTarget(uint8_t slot, const float color[4])
	{
		CS_ASSERT(state_.rtvs[slot].ptr != NULL);
		pCmdList_->ClearRenderTargetView(state_.rtvs[slot], color, 0, nullptr);
	}

	CS_FORCE_INLINE void CommandList::clearRenderTarget(D3D12_CPU_DESCRIPTOR_HANDLE rtv, const math::Vector3 & color)
	{
		float col[4] = { color.x, color.y, color.z, 1.0f };
		clearRenderTarget(rtv, col);
	}


	CS_FORCE_INLINE void CommandList::clearRenderTarget(D3D12_CPU_DESCRIPTOR_HANDLE rtv, const float color[4])
	{
		pCmdList_->ClearRenderTargetView(rtv, color, 0, nullptr);
	}


	CS_FORCE_INLINE void CommandList::clearDepthStencil(float depth, uint8_t stencil)
	{
		CS_ASSERT(state_.dsv.ptr != NULL);
		pCmdList_->ClearDepthStencilView(state_.dsv,
			D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL,
			depth,
			stencil,
			0,
			nullptr);
	}

	CS_FORCE_INLINE void CommandList::resourceBarrier(const D3D12_RESOURCE_BARRIER& barrier)
	{
		pCmdList_->ResourceBarrier(1, &barrier);
	}

	CS_FORCE_INLINE void CommandList::transition(D3D12_RESOURCE_STATES before,
		D3D12_RESOURCE_STATES after,
		ID3D12Resource* resource)
	{
		D3D12_RESOURCE_BARRIER barrier{};
		barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barrier.Transition.pResource = resource;
		barrier.Transition.StateBefore = before;
		barrier.Transition.StateAfter = after;
		barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		resourceBarrier(barrier);
	}

	CS_FORCE_INLINE ID3D12GraphicsCommandList* CommandList::raw() const
	{
		return pCmdList_.Get();
	}
	CS_FORCE_INLINE D3D12_COMMAND_LIST_TYPE CommandList::type() const noexcept
	{
		return type_;
	}

	CS_FORCE_INLINE void CommandList::signalFence(uint64_t fence)
	{
		fenceValue_ = fence;
	}

	CS_FORCE_INLINE uint64_t CommandList::lastFenceValue() const noexcept
	{
		return fenceValue_;
	}

	CS_FORCE_INLINE void CommandList::setViewport(const Viewport& viewport)
	{
		pCmdList_->RSSetViewports(1u, &viewport);
	}
	CS_FORCE_INLINE void CommandList::setViewports(std::span< const Viewport >			viewports)
	{
		pCmdList_->RSSetViewports(static_cast<UINT>(viewports.size()), viewports.data());
	}

	CS_FORCE_INLINE void CommandList::setViewports(std::initializer_list< Viewport >	viewports)
	{
		pCmdList_->RSSetViewports(static_cast<UINT>(viewports.size()), viewports.begin());
	}

	CS_FORCE_INLINE void CommandList::setScissorRect(const Rect& rect)
	{
		pCmdList_->RSSetScissorRects(1u, &rect);
	}

	CS_FORCE_INLINE void CommandList::setScissorRects(std::span< const Rect > rects)
	{
		pCmdList_->RSSetScissorRects(static_cast<UINT>(rects.size()), rects.data());
	}

	CS_FORCE_INLINE void CommandList::setScissorRects(std::initializer_list< Rect > rects)
	{
		pCmdList_->RSSetScissorRects(static_cast<UINT>(rects.size()), rects.begin());
	}

	CS_FORCE_INLINE void CommandList::setDescriptorHeap(ID3D12DescriptorHeap* heap)
	{
		pCmdList_->SetDescriptorHeaps(1, &heap);
	}

	CS_FORCE_INLINE void CommandList::setDescriptorHeaps(std::span<ID3D12DescriptorHeap*> heaps)
	{
		pCmdList_->SetDescriptorHeaps(static_cast<UINT>(heaps.size()), heaps.data());
	}

	CS_FORCE_INLINE void CommandList::setDescriptorHeaps(std::initializer_list< ID3D12DescriptorHeap* > heaps)
	{
		pCmdList_->SetDescriptorHeaps(static_cast<UINT>(heaps.size()), heaps.begin());
	}

	CS_FORCE_INLINE void CommandList::setVertexBuffer(const VertexBufferView& view)
	{
		pCmdList_->IASetVertexBuffers(0, 1, &view.dx_);
	}

	CS_FORCE_INLINE void CommandList::setVertexBuffers(std::span< const VertexBufferView > views, size_t startSlot)
	{
		CS_ASSERT(!views.empty() && startSlot + views.size() <= D3D12_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT);
		pCmdList_->IASetVertexBuffers(static_cast<UINT>(startSlot),
			static_cast<UINT>(views.size()),
			reinterpret_cast<const D3D12_VERTEX_BUFFER_VIEW*>(&views[0].dx_));
	}

	CS_FORCE_INLINE void CommandList::setVertexBuffers(std::initializer_list< VertexBufferView > views,
		size_t startSlot)
	{

		CS_ASSERT(views.size() != 0);
		CS_ASSERT(startSlot + views.size() <= D3D12_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT);

		pCmdList_->IASetVertexBuffers(
			static_cast<UINT>(startSlot),
			static_cast<UINT>(views.size()),
			reinterpret_cast<const D3D12_VERTEX_BUFFER_VIEW*>(views.begin())  // через приватное dx_ внутри
		);
	}

	CS_FORCE_INLINE void CommandList::resetVertexBuffers()
	{
		D3D12_VERTEX_BUFFER_VIEW emptyVB{};
		pCmdList_->IASetVertexBuffers(0, 1, &emptyVB);
	}

	CS_FORCE_INLINE void CommandList::setIndexBuffer(const IndexBufferView& view)
	{
		CS_ASSERT(view.dx_.SizeInBytes > 0);
		pCmdList_->IASetIndexBuffer(&view.dx_);
	}

	CS_FORCE_INLINE void CommandList::resetIndexBuffer()
	{
		D3D12_INDEX_BUFFER_VIEW emptyIB{};
		pCmdList_->IASetIndexBuffer(&emptyIB);
	}

	CS_FORCE_INLINE void CommandList::setPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY topology)
	{
		state_.topology = topology;
		pCmdList_->IASetPrimitiveTopology(topology);
	}

	CS_FORCE_INLINE void CommandList::setGraphicsRootDescriptorTable(UINT slot, D3D12_GPU_DESCRIPTOR_HANDLE handle)
	{
		CS_ASSERT(slot < D3D12_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT);
		pCmdList_->SetGraphicsRootDescriptorTable(slot, handle);
	}

	CS_FORCE_INLINE void CommandList::setGraphicsRootShaderResourceView(UINT slot, D3D12_GPU_VIRTUAL_ADDRESS addr)
	{
		CS_ASSERT(slot < D3D12_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT);
		pCmdList_->SetGraphicsRootShaderResourceView(slot, addr);
	}

	CS_FORCE_INLINE void CommandList::setGraphicsRootConstantBufferView(UINT slot, D3D12_GPU_VIRTUAL_ADDRESS addr)
	{
		CS_ASSERT(slot < D3D12_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT);
		pCmdList_->SetGraphicsRootConstantBufferView(slot, addr);
	}

	CS_FORCE_INLINE void CommandList::setGraphicsRootUnorderedAccessView(UINT slot, D3D12_GPU_VIRTUAL_ADDRESS addr)
	{
		CS_ASSERT(slot < D3D12_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT);
		pCmdList_->SetGraphicsRootUnorderedAccessView(slot, addr);
	}

	CS_FORCE_INLINE void CommandList::setGraphicsRoot32BitConstants(UINT slot,
		UINT num32BitValues,
		const void* pData,
		UINT offset)
	{
		CS_ASSERT(slot < D3D12_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT);
		CS_ASSERT(num32BitValues > 0);
		pCmdList_->SetGraphicsRoot32BitConstants(slot, num32BitValues, pData, offset);
	}


	CS_FORCE_INLINE void CommandList::drawInstanced(
		size_t vertexCount,
		size_t instanceCount,
		size_t startVertex,
		size_t startInstance)
	{
		CS_ASSERT(vertexCount > 0);
		CS_ASSERT(instanceCount > 0);

		pCmdList_->DrawInstanced(static_cast<UINT>(vertexCount),
			static_cast<UINT>(instanceCount),
			static_cast<UINT>(startVertex),
			static_cast<UINT>(startInstance));
#ifdef _DEBUG
		updateStats(vertexCount, instanceCount);
#endif
	}

	CS_FORCE_INLINE void CommandList::drawIndexedInstanced(
		size_t indexCount,
		size_t instanceCount,
		size_t startIndex,
		size_t baseVertex,
		size_t startInstance)
	{
		CS_ASSERT(indexCount > 0);
		CS_ASSERT(instanceCount > 0);

		pCmdList_->DrawIndexedInstanced(static_cast<UINT>(indexCount),
			static_cast<UINT>(instanceCount),
			static_cast<UINT>(startIndex),
			static_cast<UINT>(baseVertex),
			static_cast<UINT>(startInstance));

#ifdef _DEBUG
		updateStats(indexCount, instanceCount);
#endif
	}

#ifdef _DEBUG
	CS_FORCE_INLINE uint64_t CommandList::estimatePrimitives(size_t vertexCount,
		D3D12_PRIMITIVE_TOPOLOGY topology,
		size_t instanceCount)
	{
		uint64_t prims = 0;
		switch (topology)
		{
		case D3D_PRIMITIVE_TOPOLOGY_POINTLIST:
			prims = vertexCount;
			break;
		case D3D_PRIMITIVE_TOPOLOGY_LINELIST:
			prims = vertexCount / 2;
			break;
		case D3D_PRIMITIVE_TOPOLOGY_LINESTRIP:
			prims = vertexCount > 0 ? vertexCount - 1 : 0;
			break;
		case D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST:
			prims = vertexCount / 3;
			break;
		case D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP:
			prims = vertexCount > 2 ? vertexCount - 2 : 0;
			break;
		default:
			prims = 0;	// остальные патчи/adjacency топологии можно добавить при необходимости
		}

		return prims * instanceCount;
	}

	CS_FORCE_INLINE void CommandList::updateStats(size_t elementCount, size_t instanceCount)
	{
		statistic_.drawCalls++;
		statistic_.vertices += elementCount;
		statistic_.instances += instanceCount;
		statistic_.primitives += estimatePrimitives(elementCount, state_.topology, instanceCount);

	}

	CS_FORCE_INLINE const CommandList::Statistic& CommandList::getStatistic() const noexcept
	{
		return statistic_;
	}
#endif
}

#endif