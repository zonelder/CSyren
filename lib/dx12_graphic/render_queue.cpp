#include "pch.h"
#include "render_queue.h"


namespace csyren::render
{
	void RenderQueue::execute()
	{
#ifndef _DEBUG
		statistic_.reset();
#endif
		std::vector< ID3D12CommandList* > cmdLists;
		cmdLists.reserve(frameContexts_.size());

		for (auto* frame : frameContexts_)
		{
			cmdLists.push_back(frame->raw());
		}
		if (!cmdLists.empty())
		{
			_pQueue->ExecuteCommandLists(static_cast<UINT>(cmdLists.size()), cmdLists.data());
		}
		//*
		// Сигналим fence и раскидываем значение в кадры
		UINT64 fenceValue = signal();
		for (auto* frame : frameContexts_)
		{
			frame->signalFence(fenceValue);
			fenceValue = fenceValue;
#ifndef _DEBUG
			statistic_ += frame->getStatistic();
#endif
		}


		frameContexts_.clear();
	}
}