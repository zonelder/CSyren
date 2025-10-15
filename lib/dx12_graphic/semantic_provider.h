#pragma once

#include "core/entity.h"
#include <unordered_map>
#include <string>
#include "shader.h"

namespace csyren::render
{
	class SemanticProvider
	{
	public:
		using CopyFunc = void(*)(core::Entity::ID,core::Scene&,uint8_t* dst, size_t size);

		void registerProvider(const std::string& semantic, CopyFunc func)
		{
			_providers[semantic] = func;
		}

		void fillBuffer(core::Entity::ID& entity,core::Scene& scene, const LinkedBuffer& layout, std::vector<uint8_t>& cpuData)
		{
			for (auto& var : layout.variables)
			{
				auto it = _providers.find(var.semantic);
				if (it == _providers.end()) continue;

				it->second(entity,scene,cpuData.data() + var.offset, var.size);
			}
		}

	private:
		std::unordered_map<std::string, CopyFunc> _providers;
	};
}
