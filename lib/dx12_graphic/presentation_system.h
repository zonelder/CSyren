#pragma once
#include "core/system_base.h"
#include "swap_chain.h"
#include "render_queue.h"
#include "base_resource.h"


namespace csyren::render
{
	class PresentationSystem : public core::System
	{
		struct PresentResource
		{
			uint64_t		fence{ 0 };
			BaseResource	resource;
			CommandList		cmdList;
		};
	public:
		void init(core::ServiceContext& ctx) override;
		void startFrame(core::ServiceContext& ctx) override;

		void endFrame(core::ServiceContext& ctx) override;

	private:
		bool present(RenderQueue& mainContext);
		bool resize(size_t width, size_t height, bool windowed);
		bool rebuildResources();
		SwapChain _swapChain;
		RenderQueue _presentationQueue;
		std::vector< PresentResource > _renderTargets;
		size_t _currentIdx{ 0 };

	};
}