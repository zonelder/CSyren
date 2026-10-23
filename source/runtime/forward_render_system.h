#pragma once
// Forward pass on top of the lit HDR image: every material whose shader is not
// a GBuffer shader (single SV_TARGET). Opaque forward materials draw first with
// depth writes, blended (transparent) materials draw back-to-front with the
// depth test only. Lit forward shaders read the same light/cluster buffers as
// the deferred light pass (see lighting.hlsli).
#include <algorithm>
#include <array>
#include <unordered_map>
#include <vector>
#include "core/typed_system.h"
#include "core/transform.h"
#include "dx12_graphic/renderer.h"
#include "dx12_graphic/recorder.h"
#include "dx12_graphic/resource_manager.h"
#include "dx12_graphic/camera_render_target.h"
#include "dx12_graphic/mesh.h"
#include "dx12_graphic/lighting/scene_lights.h"
#include "mesh_filter.h"
#include "core/registration.h"

CS_STRICT_SYSTEMS_BEGIN
namespace csyren::render
{
    class ForwardRenderSystem : public core::TypedSystem<ForwardRenderSystem>
    {
    public:
        static constexpr auto phase = "render";
        static constexpr std::array after{ "LightPassSystem" };
        static constexpr core::GpuTaskScope gpuScope = core::GpuTaskScope::View;
        static constexpr std::array gpu{
            core::GpuUse{ "scene.hdr", D3D12_RESOURCE_STATE_RENDER_TARGET, core::AccessMode::Write },
            core::GpuUse{ "scene.depth", D3D12_RESOURCE_STATE_DEPTH_WRITE, core::AccessMode::Write },
            core::GpuUse{ "scene.clusterGrid", D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, core::AccessMode::Read },
            core::GpuUse{ "scene.clusterIndices", D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, core::AccessMode::Read },
        };

        // World matrices come from the renderer's per-frame copy of the Transforms.
        using Meshes = core::Select<core::Transform, MeshFilter, MeshRenderer>;

        void execute(Recorder recorder, Meshes meshes, core::Res<const ResourceManager> resourceManager, core::Res<const SceneLights> sceneLights)
        {
            auto& renderer = *recorder;
            auto& resources = *resourceManager;
            const auto& lights = *sceneLights;
            const auto* target = renderer.currentTarget();
            const auto& worlds = renderer.worldTransforms();
            const auto& engine = *renderer.getEngineVariableBuffer();
            const DirectX::XMVECTOR eye = DirectX::XMLoadFloat4(&engine.cameraPosition);

            struct Draw { core::Entity::ID entity; MaterialHandle material; MeshHandle mesh; float distance; };
            std::vector<Draw> opaque, transparent;
            for (auto [id, transform, filter, meshRenderer] : meshes)
            {
                const auto* material = resources.getMaterial(meshRenderer.material);
                if (!material) continue;
                const auto* shader = resources.getShader(material->getShader());
                if (!shader || shader->isDeferred()) continue; // GBuffer materials were drawn by MeshRenderSystem
                const bool blended = material->getStates().blendState.RenderTarget[0].BlendEnable;
                float distance = 0.0f;
                if (blended)
                {
                    const auto position = worlds.matrix(id).translation();
                    distance = DirectX::XMVectorGetX(DirectX::XMVector3LengthSq(DirectX::XMVectorSubtract(
                        DirectX::XMVectorSet(position.x, position.y, position.z, 1.0f), eye)));
                }
                (blended ? transparent : opaque).push_back({ id, meshRenderer.material, filter.mesh, distance });
            }
            if (opaque.empty() && transparent.empty()) return;

            // Opaque: group by material/mesh to limit state changes. Transparent: far to near.
            std::sort(opaque.begin(), opaque.end(), [](const Draw& a, const Draw& b) {
                return a.material < b.material || (a.material == b.material && a.mesh < b.mesh);
            });
            std::sort(transparent.begin(), transparent.end(), [](const Draw& a, const Draw& b) { return a.distance > b.distance; });

            const auto lightsAddress = lights.upload(renderer);
            auto entityParams = renderer.getEntityVariableBuffer();
            MaterialHandle boundMaterial; MeshHandle boundMesh; const SemanticBufferLayout* entityLayout = nullptr;
            auto draw = [&](const Draw& item)
            {
                const auto* mesh = resources.getMesh(item.mesh);
                if (!mesh) return;
                if (!(item.material == boundMaterial) || !(item.mesh == boundMesh))
                {
                    const auto* material = resources.getMaterial(item.material);
                    const auto* shader = material ? resources.getShader(material->getShader()) : nullptr;
                    if (!shader || !renderer.bindMaterial(resources, item.material, mesh->getLayout())) { boundMaterial = {}; boundMesh = {}; return; }
                    entityLayout = shader->getSemanticBuffer(details::CBufferUpdateType::Entity);
                    // Lit forward shaders declare these buffers; unlit ones simply do not.
                    renderer.setShaderBuffer(*shader, "lights", lightsAddress);
                    if (target)
                    {
                        renderer.setShaderBuffer(*shader, "clusterGrid", target->address(ViewAttachment::ClusterGrid));
                        renderer.setShaderBuffer(*shader, "clusterIndices", target->address(ViewAttachment::ClusterIndices));
                    }
                    mesh->bind(renderer);
                    boundMaterial = item.material; boundMesh = item.mesh;
                }
                entityParams->setWorld(worlds.matrix(item.entity));
                entityParams->entityID = core::Entity::slot(item.entity);
                if (!renderer.bindEntity(entityLayout)) return;
                mesh->draw(renderer);
            };
            for (const auto& item : opaque) draw(item);
            for (const auto& item : transparent) draw(item);
        }
    };
    CS_SYSTEM(ForwardRenderSystem);
}
CS_STRICT_SYSTEMS_END
