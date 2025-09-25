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
	ShaderMetaPtr ShaderMetaBuilder::build(const std::string& shaderCode)
	{
		auto meta = std::make_unique<ShaderMeta>();
        const auto& registry = details::AttributeRegistry::instance();
        // Regex for blocks: catch (1:annotations), (2:type), (3:name)
        std::regex block_regex(
            "((?:^\\s*//\\s*@.*(?:\\r\\n|\\n))+)" // Group 1: annotations block
            "\\s*(?:[\\w:]+\\s+)*"                // pass modifiers
            "([\\w:]+)\\s+"                       // Group 2: type
            "(\\w+)\\s*;"                         // Group 3: name
        );

        // Regex for attributes: catch (1:attr name), (2:value)
        std::regex attr_regex("^\\s*//\\s*@(\\w+)\\s*(.*)");


        for (auto it = std::sregex_iterator(shaderCode.begin(), shaderCode.end(), block_regex); it != std::sregex_iterator(); ++it)
        {
            std::smatch block_match = *it;
            std::string annotation_block = block_match[1].str();
            std::string variable_type = block_match[2].str();
            std::string variable_name = block_match[3].str();

            ShaderMeta::VariableMeta var_meta;

            var_meta.type = variable_type;

            std::istringstream iss(annotation_block);
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
                        if (var_meta.attributes.count(attr_name) > 0)
                        {
                            log::warning("ShaderMeta:  Duplicated attribute \'@{}\' for variable {}.using last definition.", attr_name, variable_name);
                        }
                        var_meta.attributes[attr_name] = attr_value;
                    }
                    else
                    {
                        log::warning("ShaderMeta:  Ignoring unregistered attribute \'@{}\' for variable {}.", attr_name,variable_name);
                    }
                }
            }

            if (!var_meta.attributes.empty())
            {
                meta->_variables.emplace_back(std::move(var_meta));
            }
        }
	}

   ShaderMetaPtr ShaderMetaBuilder::build(const nlohmann::json& jsonData)
    {
        auto meta = std::make_unique<ShaderMeta>();
        const auto& registry = details::AttributeRegistry::instance();

        for (auto& [var_name, meta_json] : jsonData.items())
        {
            ShaderMeta::VariableMeta var_meta;
            auto& attrs_json = meta_json["attributes"];
            if (!attrs_json.is_object())
                continue;
            for (auto& [attr_name, attr_value] : attrs_json.items()) {
                if (registry.isAttributeValid(attr_name, attr_value)) 
                {
                    if (attr_value.is_string())
                    {
                        if (var_meta.attributes.count(attr_name) > 0)
                        {
                            log::warning("ShaderMeta:  Duplicated attribute \'@{}\' for variable {}.using last definition.", attr_name, var_name);
                        }
                        var_meta.attributes[attr_name] = attr_value.get<std::string>();
                    }
                }
                else 
                {
                    log::warning("ShaderMeta:  Ignoring unregistered attribute \'@{}\' for variable {}.", attr_name, var_name);
                }
            }

            if (!var_meta.attributes.empty())
            {
                meta->_variables.emplace_back(std::move(var_meta));
            }
        }
        return meta;
    }


   bool ShaderMetaBuilder::save(const ShaderMeta& meta, const std::string& filepath)
   {
       nlohmann::json jsonData;

       // Проходим по всем метаданным в объекте ShaderMeta
       for (const auto& variable_meta : meta.getAllMeta())
       {
           nlohmann::json attributesJson;
           for (const auto& [attr_name, attr_value] : variable_meta.attributes) 
           {
               attributesJson[attr_name] = attr_value;
           }

           jsonData[variable_meta.name] = 
           {
               {"type", variable_meta.type},
               {"attributes", attributesJson}
           };
       }

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

