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
		friend class ShaderMetaBuilder;
	public:
		using AttributeName_t = std::string;
		using AttributeParams_t = std::string;
		using AttributeMap = std::unordered_map< AttributeName_t, AttributeParams_t>;
		struct VariableMeta
		{
			std::string type;
			std::string name;
			AttributeMap attributes;
		};
		using VariableMetaMap = std::vector<VariableMeta>;


		struct CBufferMeta
		{
			std::string name;
			AttributeMap attributes;
		};
		using CBufferVector = std::vector<CBufferMeta>;

		/**
		 * @class variableView.
		 * @brief save read-only access to variable meta data;
		 */
		class VariableView
		{
		public:
			using data_type = const VariableMeta;
			using const_iterator = VariableMetaMap::const_iterator;
			explicit VariableView(const VariableMetaMap& vars) : _vars(vars) {};

			const_iterator begin() const { return _vars.get().begin(); }
			const_iterator end() const { return _vars.get().end(); }

			size_t size() const { return _vars.get().size(); }
			bool empty() const { return _vars.get().empty(); }

			data_type& operator[](size_t index) const { return _vars.get()[index]; }
		private:
			std::reference_wrapper<const VariableMetaMap> _vars;
		};


		/**
		 * @class CbufferView.
		 * @brief save read-only access to cbuffer meta data;
		 */
		class CBufferView
		{
		public:
			using data_type = const CBufferMeta;
			using const_iterator = CBufferVector::const_iterator;

			explicit CBufferView(const CBufferVector& buffs) : _buffs(buffs){}

			const_iterator begin() const { return _buffs.get().begin(); }
			const_iterator end() const { return _buffs.get().end();}

			size_t size() const { return _buffs.get().size(); }
			bool empty() const { return _buffs.get().empty(); }
			data_type& operator[](size_t index) const { return _buffs.get()[index]; }
		private:
			std::reference_wrapper<const CBufferVector> _buffs;
		};

		VariableView variableView() const { return VariableView(_variables); }

		CBufferView cbufferView() const { return CBufferView(_cbuffers); }
	private:
		CBufferVector	_cbuffers;
		VariableMetaMap _variables;
	};
	using ShaderMetaPtr = std::unique_ptr<ShaderMeta>;


	class ShaderMetaBuilder
	{
	public:
		static ShaderMetaPtr build(const std::string& shaderCode);

		static ShaderMetaPtr build(const nlohmann::json& jsonData);

		static bool save(const ShaderMeta& meta, const std::string& filepath);

		static ShaderMeta::AttributeMap parseAttributes(const std::string& annotationBlock, const std::string& contextName);
	};


}
