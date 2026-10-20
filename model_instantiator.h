#pragma once
// Turns an imported model (ModelData) into an entity hierarchy: one entity per
// node with Transform, plus MeshFilter/MeshRenderer for nodes with a mesh
// (multi-primitive meshes get one child entity per primitive). Node names are
// kept, so AnimationClip tracks can find their targets.
#include <string>
#include <vector>
#include "core/entity_batch.h"
#include "core/transform.h"
#include "dx12_graphic/import/model_data.h"
#include "dx12_graphic/import/model_importer.h"
#include "dx12_graphic/resource_manager.h"
#include "dx12_graphic/primitives.h"
#include "mesh_filter.h"

namespace csyren
{
    struct ModelInstance
    {
        core::Entity::ID root = core::Entity::invalidID;
        std::vector<core::Entity::ID> entities; // every created entity, root first
    };

    // Number of entities instantiateModel will create (size the EntityBatch with it).
    inline size_t modelEntityCount(const render::assets::ModelData& model)
    {
        size_t count = model.nodes.size();
        for (const auto& node : model.nodes)
            if (node.mesh >= 0 && node.mesh < int(model.meshes.size()) && model.meshes[node.mesh].primitives.size() > 1)
                count += model.meshes[node.mesh].primitives.size();
        if (model.roots.size() != 1) ++count; // wrapper entity
        return count;
    }

    inline bool instantiateModel(core::EntityBatch& batch, const render::assets::ModelData& model,
        render::ResourceManager& resources, ModelInstance& out, const std::string& rootName = {})
    {
        using namespace render;
        auto materialFor = [&](int material) {
            if (material >= 0 && material < int(model.materials.size()))
                return resources.get<Material>(assets::ModelReference::makeMaterial(model.source, material));
            return Primitives::getLitMaterial();
        };
        auto attachMesh = [&](core::Entity::ID entity, int meshIndex, int primitive) {
            auto mesh = resources.get<Mesh>(assets::ModelReference::makeMesh(model.source, meshIndex, primitive));
            if (!mesh) return false;
            auto* filter = batch.addComponent<MeshFilter>(entity);
            auto* renderer = batch.addComponent<MeshRenderer>(entity);
            if (!filter || !renderer) return false;
            filter->mesh = mesh;
            renderer->material = materialFor(model.meshes[meshIndex].primitives[primitive].material);
            return true;
        };

        std::vector<core::Entity::ID> created(model.nodes.size(), core::Entity::invalidID);
        core::Entity::ID wrapper = core::Entity::invalidID;
        if (model.roots.size() != 1)
        {
            const auto slash = model.source.find_last_of("/\\");
            wrapper = batch.createEntity(rootName.empty() ? model.source.substr(slash == std::string::npos ? 0 : slash + 1) : rootName);
            if (!batch.addComponent<core::Transform>(wrapper)) return false;
            out.root = wrapper; out.entities.push_back(wrapper);
        }
        // Parents are created before children by walking roots depth-first.
        std::vector<int> stack(model.roots.rbegin(), model.roots.rend());
        while (!stack.empty())
        {
            const int index = stack.back(); stack.pop_back();
            if (index < 0 || index >= int(model.nodes.size()) || created[index] != core::Entity::invalidID) continue;
            const auto& node = model.nodes[index];
            const bool isRoot = node.parent < 0 || created[node.parent] == core::Entity::invalidID;
            const auto entity = batch.createEntity(isRoot && !rootName.empty() && wrapper == core::Entity::invalidID ? rootName : node.name);
            created[index] = entity;
            out.entities.push_back(entity);
            auto* transform = batch.addComponent<core::Transform>(entity);
            if (!transform) return false;
            transform->position = node.translation; transform->rotation = node.rotation; transform->scale = node.scale;
            if (isRoot) { if (wrapper != core::Entity::invalidID) batch.setParent(entity, wrapper); else out.root = entity; }
            else batch.setParent(entity, created[node.parent]);
            if (node.mesh >= 0 && node.mesh < int(model.meshes.size()))
            {
                const auto& mesh = model.meshes[node.mesh];
                if (mesh.primitives.size() == 1)
                {
                    if (!attachMesh(entity, node.mesh, 0)) return false;
                }
                else for (size_t primitive = 0; primitive < mesh.primitives.size(); ++primitive)
                {
                    const auto part = batch.createEntity(node.name + "." + std::to_string(primitive));
                    out.entities.push_back(part);
                    if (!batch.addComponent<core::Transform>(part)) return false;
                    batch.setParent(part, entity);
                    if (!attachMesh(part, node.mesh, int(primitive))) return false;
                }
            }
            for (auto it = node.children.rbegin(); it != node.children.rend(); ++it) stack.push_back(*it);
        }
        return out.root != core::Entity::invalidID;
    }
}
