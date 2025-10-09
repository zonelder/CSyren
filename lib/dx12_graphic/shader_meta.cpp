#include "pch.h"
#include "shader_meta.h"
#include <regex>
#include "cstdmf/string_utils.h"

namespace csyren::render::details
{
    void registerAttributes(AttributeRegistry& registry)
    {
        registry.registerAttribute("semantic");
        registry.registerAttribute("tooltip");
        registry.registerAttribute("update");
    }

    const AttributeRegistry& AttributeRegistry::instance()
    {
        static AttributeRegistry registry;
        registerAttributes(registry);
        return registry;
    }

}

namespace csyren::render
{


    ShaderMeta::AttributeMap ShaderMetaBuilder::parseAttributes(const std::string& annotationBlock, const std::string& contextName)
    {
        ShaderMeta::AttributeMap attributes;
        const auto& registry = details::AttributeRegistry::instance();
        std::regex attr_regex("^\\s*//\\s*@(\\w+)\\s*(.*)");
        std::istringstream iss(annotationBlock);
        std::string line;

        while (std::getline(iss, line))
        {
            std::smatch attr_match;

            if (std::regex_search(line, attr_match, attr_regex))
            {
                std::string attr_name = attr_match[1].str();
                std::string attr_value = cstdmf::trim(attr_match[2].str());

                if (registry.isAttributeValid(attr_name, attr_value))
                {
                    if (attributes.count(attr_name) > 0)
                    {
                        log::warning("ShaderMeta: Dublicated attribute '@{}' for {}.Using last definition.", attr_name, contextName);
                    }
                    attributes[attr_name] = attr_value;
                }
                else
                {
                    log::warning("Ignoring unregistered attribute '@{}' for {}.", attr_name, contextName);
                }
            }
        }

        return attributes;

    }

	ShaderMetaPtr ShaderMetaBuilder::build(const std::string& shaderCode)
	{
		auto meta = std::make_unique<ShaderMeta>();
        const auto& registry = details::AttributeRegistry::instance();
        // Regex for blocks: catch (1:annotations), (2:type), (3:name)
        std::regex block_regex(
            "((?:^\\s*//\\s*@.*(?:\\r\\n|\\n))+)"  // 1: Annotation block
            "\\s*([\\w:]+)\\s+"                      // 2: Type or "cbuffer" keyword
            "(\\w+)\\s*"                             // 3: Name
        );

        for (auto it = std::sregex_iterator(shaderCode.begin(), shaderCode.end(), block_regex); it != std::sregex_iterator(); ++it)
        {
            std::smatch match = *it;
            std::string annotation_block = match[1].str();
            std::string keyword_or_type = match[2].str();
            std::string name = match[3].str();

            if (keyword_or_type == "cbuffer")
            {
                ShaderMeta::CBufferMeta cbuffer_meta;
                cbuffer_meta.attributes = parseAttributes(annotation_block, name);
                if (!cbuffer_meta.attributes.empty())
                {
                    cbuffer_meta.name = name;
                    meta->_cbuffers.emplace_back(std::move(cbuffer_meta));
                }
            }
            else
            {
                ShaderMeta::VariableMeta var_meta;
                var_meta.type = keyword_or_type;
                var_meta.name = name;
                var_meta.attributes = parseAttributes(annotation_block, name);

                if (!var_meta.attributes.empty())
                {
                    meta->_variables.emplace_back(std::move(var_meta));
                }
            }
        }
            

        return meta;
	}

   ShaderMetaPtr ShaderMetaBuilder::build(const nlohmann::json& jsonData)
    {
        auto meta = std::make_unique<ShaderMeta>();
        const auto& registry = details::AttributeRegistry::instance();

        if (jsonData.contains("variables") && jsonData["variables"].is_object())
        {
            for (auto& [var_name, meta_json] : jsonData["variables"].items())
            {
                if (!meta_json.is_object() ||
                    !meta_json.contains("type") || !meta_json["type"].is_string() ||
                    !meta_json.contains("attributes") || !meta_json["attributes"].is_object())
                {
                    log::warning("ShaderMeta: Invalid or incomplete structure for variable '{}' in JSON. Skipping.", var_name);
                    continue;
                }

                ShaderMeta::VariableMeta var_meta;
                var_meta.name = var_name;
                var_meta.type = meta_json["type"].get<std::string>();

                for (auto& [attr_name, attr_value] : meta_json["attributes"].items())
                {
                    if (attr_value.is_string())
                    {
                        const std::string& value_str = attr_value.get<std::string>();
                        if (registry.isAttributeValid(attr_name, value_str))
                        {
                            var_meta.attributes[attr_name] = value_str;
                        }
                        else
                        {
                            log::warning("ShaderMeta: Ignoring unregistered attribute '@{}' for variable '{}'.", attr_name, var_name);
                        }
                    }
                }
                meta->_variables.emplace_back(std::move(var_meta));
            }
        }
        if (jsonData.contains("cbuffers") && jsonData["cbuffers"].is_object())
        {
            for (auto& [cbuffer_name, meta_json] : jsonData["cbuffers"].items())
            {
                if (!meta_json.is_object() ||
                    !meta_json.contains("attributes") || !meta_json["attributes"].is_object())
                {
                    log::warning("ShaderMeta: Invalid or incomplete structure for cbuffer '{}' in JSON. Skipping.", cbuffer_name);
                    continue;
                }

                ShaderMeta::CBufferMeta cbuffer_meta;
                cbuffer_meta.name = cbuffer_name;

                for (auto& [attr_name, attr_value] : meta_json["attributes"].items())
                {
                    if (attr_value.is_string())
                    {
                        const std::string& value_str = attr_value.get<std::string>();
                        if (registry.isAttributeValid(attr_name, value_str))
                        {
                            cbuffer_meta.attributes[attr_name] = value_str;
                        }
                        else
                        {
                            log::warning("ShaderMeta: Ignoring unregistered attribute '@{}' for cbuffer '{}'.", attr_name, cbuffer_name);
                        }
                    }
                }
                meta->_cbuffers.emplace_back(std::move(cbuffer_meta));
            }
        }
        return meta;
   }

   bool ShaderMetaBuilder::save(const ShaderMeta& meta, const std::string& filepath)
   {
       nlohmann::json jsonData;
       auto variableJson = nlohmann::json::object();
       auto cbufferJson = nlohmann::json::object();

       for (const auto& variable_meta : meta.variableView())
       {
           nlohmann::json attributesJson;
           for (const auto& [attr_name, attr_value] : variable_meta.attributes) 
           {
               attributesJson[attr_name] = attr_value;
           }

           variableJson[variable_meta.name] =
           {
               {"type", variable_meta.type},
               {"attributes", attributesJson}
           };
       }
       for (const auto& cbuffer_meta : meta.cbufferView())
       {
           nlohmann::json attributesJson;
           for (const auto& [attr_name, attr_value] : cbuffer_meta.attributes)
           {
               attributesJson[attr_name] = attr_value;
           }
           cbufferJson[cbuffer_meta.name] = {
               {"attributes", attributesJson}
           };
       }

       jsonData["variables"] = variableJson;
       jsonData["cbuffers"] = cbufferJson;

       std::ofstream file(filepath);
       if (!file.is_open()) 
       {
           log::error("ShaderMetaBuilder::save:Could not open file for writing: {}", filepath);
           return false;
       }

       try 
       {
           file << jsonData.dump(4);
       }
       catch (const nlohmann::json::exception& e) 
       {
           log::error("ShaderMetaBuilder::save: JSON serialization failed: {}", e.what());
           return false;
       }
       log::info("ShaderMetaBuilder::save: shader meta data saved successfully to {}", filepath);
       return true;
   }
}

