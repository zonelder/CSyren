#pragma once
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <memory>

#include "third_party/json/json.hpp"

namespace csyren::render::details
{
	class AttributeRegistry
	{
	public:

		static const AttributeRegistry& instance();

		void registerAttribute(const std::string& name)
		{
			_registeredAttributes.insert(name);
		}

		bool isAttributeValid(const std::string& name, const std::string& params) const
		{
			return _registeredAttributes.count(name) > 0;
		}

	private:
		AttributeRegistry() = default;
		~AttributeRegistry() = default;
		AttributeRegistry(const AttributeRegistry&) = delete;
	private:
		std::unordered_set<std::string> _registeredAttributes;
	};
}

namespace csyren::render
{
	class ShaderMeta
	{
		using VariableName_t = std::string;
		friend class ShaderMetaBuilder;
	public:
		struct VariableMeta
		{
			using AttributeName_t = std::string;
			using AttributeParams_t = std::string;
			std::string type;
			VariableName_t name;
			std::unordered_map< AttributeName_t, AttributeParams_t> attributes;
		};
		using VariableMetaMap = std::vector<VariableMeta>;

		VariableMetaMap::iterator begin() { return _variables.begin(); }
		VariableMetaMap::iterator end() { return _variables.end(); }

		VariableMetaMap::const_iterator begin() const { return _variables.begin(); }
		VariableMetaMap::const_iterator end() const { return _variables.end(); }

		const VariableMetaMap& getAllMeta() const { return _variables; }

		bool empty() const noexcept { return _variables.empty(); }
	private:

		VariableMetaMap _variables;
	};
	using ShaderMetaPtr = std::unique_ptr<ShaderMeta>;


	class ShaderMetaBuilder
	{
	public:
		static ShaderMetaPtr build(const std::string& shaderCode);

		static ShaderMetaPtr build(const nlohmann::json& jsonData);

		static bool save(const ShaderMeta& meta, const std::string& filepath);
	};


}
