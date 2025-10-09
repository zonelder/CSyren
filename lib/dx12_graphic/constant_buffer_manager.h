#pragma once
#include <unordered_map>
#include <vector>
#include <string>
#include <d3d12.h>
#include <wrl.h>
#include "shader.h"
#include "material.h"
#include "upload_ring_buffer.h"


namespace csyren::render
{
	class ConstantBufferManager
	{
	public:
		void init(ID3D12Device* device)
		{
			_deivce = device;
		}

		void commit(const Shader& shader, const Material& material, UploadRingBuffer)
		{
			for (auto& lb : shader.getLinkedBuffers())
			{

			}
		}
	private:
		ID3D12Device* _deivce;
	};
}
