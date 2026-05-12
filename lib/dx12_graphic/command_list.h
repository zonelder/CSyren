#ifndef __COMMAND_LIST_HPP__
#define __COMMAND_LIST_HPP__

#include "dx_main.h"
#include "buffer_view.h"

#include "math/Vector3.h"
#include "math/color.h"

#include "cstdmf/inline_helper.h"
#include "cstdmf/assert_helpler.h"

#include <span>

namespace csyren::render
{
	using Viewport = D3D12_VIEWPORT;
	using Rect = D3D12_RECT;

	class CommandList
	{
	public:
		struct State
		{
			D3D12_CPU_DESCRIPTOR_HANDLE rtvs[D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT];
			uint8_t						rtvCount{ 0 };
			D3D12_CPU_DESCRIPTOR_HANDLE dsv{};
			Viewport					viewport{};
			Rect						scissor{};
			D3D12_PRIMITIVE_TOPOLOGY	topology{ D3D_PRIMITIVE_TOPOLOGY_UNDEFINED };
		};
#ifdef _DEBUG
		struct Statistic
		{
			uint64_t drawCalls{ 0 };
			uint64_t vertices{ 0 };
			uint64_t instances{ 0 };
			uint64_t primitives{ 0 };

			Statistic operator+(const Statistic& other) const noexcept
			{
				return Statistic{ drawCalls + other.drawCalls,
								  vertices + other.vertices,
								  instances + other.instances,
								  primitives + other.primitives };
			}

			// добавление другой статистики к текущей
			Statistic& operator+=(const Statistic& other) noexcept
			{
				drawCalls += other.drawCalls;
				vertices += other.vertices;
				instances += other.instances;
				primitives += other.primitives;
				return *this;
			}

			void reset() noexcept
			{
				drawCalls = 0;
				vertices = 0;
				instances = 0;
				primitives = 0;
			}
		};
#endif

		CommandList() noexcept = default;
		bool init(ID3D12Device* device, D3D12_COMMAND_LIST_TYPE type);

		CS_FORCE_INLINE void reset();
		CS_FORCE_INLINE void close();

		CS_FORCE_INLINE void signalFence(uint64_t value);
		CS_FORCE_INLINE uint64_t lastFenceValue() const noexcept;

		CS_FORCE_INLINE void setRenderTarget(uint8_t slot, D3D12_CPU_DESCRIPTOR_HANDLE rtv);
		CS_FORCE_INLINE void shrinkRenderTargets(uint8_t maxSlot);
		CS_FORCE_INLINE void submitRenderTargets();

		CS_FORCE_INLINE void setDepthStencil(D3D12_CPU_DESCRIPTOR_HANDLE dsv);

		CS_FORCE_INLINE void setPipelineState(ID3D12PipelineState* pso);
		CS_FORCE_INLINE void setRootSignature(ID3D12RootSignature* rootSig);


		CS_FORCE_INLINE void clearRenderTarget(uint8_t slot, const math::Vector3& color);
		CS_FORCE_INLINE void clearRenderTarget(uint8_t slot, const float color[4]);

		CS_FORCE_INLINE void clearRenderTarget(D3D12_CPU_DESCRIPTOR_HANDLE rtv, const math::Vector3& color);
		CS_FORCE_INLINE void clearRenderTarget(D3D12_CPU_DESCRIPTOR_HANDLE rtv, const float color[4]);
		CS_FORCE_INLINE void clearDepthStencil(float depth = 1.0f, uint8_t stencil = 0);

		// Resource barrier / transitions
		CS_FORCE_INLINE void resourceBarrier(const D3D12_RESOURCE_BARRIER& barrier);
		CS_FORCE_INLINE void transition(D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after, ID3D12Resource* resource);


		CS_FORCE_INLINE void setViewport(const Viewport&					viewport);
		CS_FORCE_INLINE void setViewports(std::span<const Viewport>			viewports);
		CS_FORCE_INLINE void setViewports(std::initializer_list<Viewport >	viewports);

		CS_FORCE_INLINE void setScissorRect(const Rect&						rect);
		CS_FORCE_INLINE void setScissorRects(std::span<const Rect>			rects);
		CS_FORCE_INLINE void setScissorRects(std::initializer_list< Rect >	rects);

		CS_FORCE_INLINE void setDescriptorHeap(ID3D12DescriptorHeap* heap);
		CS_FORCE_INLINE void setDescriptorHeaps(std::span<ID3D12DescriptorHeap*> heaps);
		CS_FORCE_INLINE void setDescriptorHeaps(std::initializer_list< ID3D12DescriptorHeap* > heaps);

		CS_FORCE_INLINE void setVertexBuffer(const VertexBufferView& view);
		CS_FORCE_INLINE void setVertexBuffers(std::span< const VertexBufferView > views, size_t startSlot = 0);
		CS_FORCE_INLINE void setVertexBuffers(std::initializer_list< VertexBufferView > views, size_t startSlot = 0);

		CS_FORCE_INLINE void resetVertexBuffers();

		CS_FORCE_INLINE void setIndexBuffer(const IndexBufferView& view);

		CS_FORCE_INLINE void resetIndexBuffer();
		CS_FORCE_INLINE void setPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY topology);

		CS_FORCE_INLINE void setGraphicsRootDescriptorTable(UINT slot, D3D12_GPU_DESCRIPTOR_HANDLE handle);
		CS_FORCE_INLINE void setGraphicsRootShaderResourceView(UINT slot, D3D12_GPU_VIRTUAL_ADDRESS addr);
		CS_FORCE_INLINE void setGraphicsRootConstantBufferView(UINT slot, D3D12_GPU_VIRTUAL_ADDRESS addr);
		CS_FORCE_INLINE void setGraphicsRootUnorderedAccessView(UINT slot, D3D12_GPU_VIRTUAL_ADDRESS addr);
		CS_FORCE_INLINE void setGraphicsRoot32BitConstants(UINT slot, UINT num32BitValues, const void* pData, UINT offset = 0);

		CS_FORCE_INLINE void drawInstanced(size_t vertexCount,
			size_t instanceCount = 1,
			size_t startVertex = 0,
			size_t startInstance = 0);

		CS_FORCE_INLINE void drawIndexedInstanced(
			size_t indexCount, size_t instanceCount = 1, size_t startIndex = 0, size_t baseVertex = 0, size_t startInstance = 0);


#ifdef _DEBUG
		CS_FORCE_INLINE const Statistic& getStatistic() const noexcept;
#endif

		CS_FORCE_INLINE ID3D12GraphicsCommandList* raw() const;
		CS_FORCE_INLINE D3D12_COMMAND_LIST_TYPE type() const noexcept;
	private:
#ifdef _DEBUG

		CS_FORCE_INLINE uint64_t estimatePrimitives(size_t vertexCount,
			D3D12_PRIMITIVE_TOPOLOGY topology,
			size_t instanceCount = 1);

		CS_FORCE_INLINE void updateStats(size_t elementCount, size_t instanceCount);

		Statistic statistic_;
#endif

		State state_{};
		D3D12_COMMAND_LIST_TYPE type_;
		details::ComPtr< ID3D12CommandAllocator > pAllocator_;
		details::ComPtr< ID3D12GraphicsCommandList > pCmdList_;
		// TODO(dx12) небезопастно использовать это так. может такое случиться что fence выставлет QueueA а мы пытаемся его
		// execute в QueueB. неявный баг синхронизации будет, лучше шифровать еще и queue_id и ругаться\явно падать при несоответсвии.
		uint64_t fenceValue_{ 0 };
	};
}


#include "command_list.ipp"

#endif