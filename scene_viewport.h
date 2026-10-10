#pragma once
#include "scene_picking.h"
#include "editor_selection.h"
#include "editor_view.h"
#include "scene_session.h"
#include "mesh_filter.h"
#include "dx12_graphic/resource_manager.h"
#include "dx12_graphic/renderer.h"
#include "core/window.h"
#include "imGui/imgui.h"

namespace csyren::editor
{
    inline void synchronizeSelection()
    {
        auto& session = *core::Services::get<SceneSession>();
        auto& selection = *core::Services::get<EditorSelection>();
        selection.synchronize(session.editorContext().instance);
        const auto created = session.takeCreatedEntity();
        if (created != core::Entity::invalidID) selection.select(created);
        if (selection.hasSelection() && !session.editorScene().entities().try_get(selection.selected()))
            selection.clearSelection();
    }
    class SceneViewport
    {
        struct Hit { core::Entity::ID entity = core::Entity::invalidID; float distance = 0; };
        static Hit raycast(core::Scene& scene, render::ResourceManager& resources, const PickRay& ray, bool trace = false)
        {
            Hit result{ core::Entity::invalidID, ray.length };
            for (auto [id, transform, filter, renderer] : scene.view<core::Transform, render::MeshFilter, render::MeshRenderer>())
            {
                const auto* mesh = resources.getMesh(filter.mesh);
                float distance;
                if (!mesh || !resources.getMaterial(renderer.material)) continue;
                if (intersectMesh(ray, mesh->bounds(), mesh->positions(), mesh->indices(), transform.world(), distance))
                {
                    if (trace) log::info("Viewport ray hit entity={} distance={}", id, distance);
                    if (distance < result.distance || result.entity == core::Entity::invalidID) result = { id, distance };
                }
                else if (trace && intersectBounds(ray, mesh->bounds(), transform.world(), distance))
                    log::info("Viewport ray bounds only entity={} distance={} vertices={} indices={}",
                        id, distance, mesh->positions().size(), mesh->indices().size());
            }
            return result;
        }
    public:
    void render()
    {
        using namespace DirectX;
        auto& session = *core::Services::get<SceneSession>();
        if (session.playing()) return;
        auto& selection = *core::Services::get<EditorSelection>();
        auto& resources = *core::Services::get<render::ResourceManager>();
        auto& window = *core::Services::get<core::Window>();
        if (window.width() <= 0 || window.height() <= 0) return;
        const auto* frame = core::Services::get<render::Renderer>()->getEngineVariableBuffer();
        const auto vp = XMLoadFloat4x4(&frame->viewProjectionMatrix);
        const auto mouse = ImGui::GetIO().MousePos;
        POINT client{ static_cast<LONG>(mouse.x), static_cast<LONG>(mouse.y) };
        ScreenToClient(window.hwnd(), &client);
        const bool viewportMouse = !ImGui::GetIO().WantCaptureMouse &&
            !ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow) && !ImGui::IsAnyItemActive() &&
            client.x >= 0 && client.y >= 0 && client.x < window.width() && client.y < window.height();
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Right))
        {
            _rightClick = viewportMouse;
            _dragged = false;
        }
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Right)) _dragged = true;
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Right))
        {
            if (_rightClick && !_dragged && viewportMouse)
            {
                auto ray = viewportRay(2.f*client.x/window.width()-1.f,
                    1.f-2.f*client.y/window.height(), vp);
                constexpr float creationRayDistance = 100.f;
                ray.length = std::min(ray.length, creationRayDistance);
                const auto hit = raycast(session.editorScene(), resources, ray, true);
                _position = hit.entity != core::Entity::invalidID ?
                    ray.origin + ray.direction * hit.distance : creationPosition(ray);
                _document = session.documentId();
                log::info("Viewport creation entity={} distance={} point=({}, {}, {}) ray=({}, {}, {}) direction=({}, {}, {})",
                    hit.entity, hit.distance, _position.x, _position.y, _position.z,
                    ray.origin.x, ray.origin.y, ray.origin.z, ray.direction.x, ray.direction.y, ray.direction.z);
                ImGui::OpenPopup("Viewport object");
            }
            _rightClick = false;
        }
        if (ImGui::BeginPopup("Viewport object"))
        {
            if (_document != session.documentId()) ImGui::CloseCurrentPopup();
            else if (ImGui::MenuItem("Create empty object here"))
            {
                session.finishEdit();
                SceneRequest request{ SceneOperation::CreateEntity, _document };
                request.position = _position;
                session.enqueue(std::move(request));
            }
            ImGui::EndPopup();
        }
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::GetIO().WantCaptureMouse &&
            !ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow) && !ImGui::IsAnyItemActive() &&
            client.x >= 0 && client.y >= 0 && client.x < window.width() && client.y < window.height())
        {
            const auto ray = viewportRay(2.f*client.x/window.width()-1.f,
                1.f-2.f*client.y/window.height(), vp);
            const auto hit = raycast(session.editorScene(), resources, ray);
            session.finishEdit(); selection.select(hit.entity);
        }
        auto& scene = session.editorScene();
        if (!scene.hasComponent<core::Transform>(selection.selected())) return;
        auto transform = scene.getComponent<core::Transform>(selection.selected());
        // Empty entities have a small editor marker around their pivot.
        math::MeshBounds bounds;
        bounds.include({ -.1f, -.1f, -.1f }); bounds.include({ .1f, .1f, .1f });
        if (scene.hasComponent<render::MeshFilter>(selection.selected()))
        {
            auto filter = scene.getComponent<render::MeshFilter>(selection.selected());
            const auto* mesh = resources.getMesh(filter->mesh);
            if (mesh && mesh->bounds().valid) bounds = selectionBounds(mesh->bounds());
        }
        XMFLOAT4 corners[8];
        const auto matrix = XMMatrixMultiply(transform->world(), vp);
        for (int i = 0; i < 8; ++i)
            XMStoreFloat4(&corners[i], XMVector4Transform(XMVectorSet(
                (i&1) ? bounds.max.x : bounds.min.x, (i&2) ? bounds.max.y : bounds.min.y,
                (i&4) ? bounds.max.z : bounds.min.z, 1), matrix));
        POINT screen{}; ClientToScreen(window.hwnd(), &screen);
        auto* draw = ImGui::GetBackgroundDrawList(ImGui::GetMainViewport());
        const auto project = [&](const XMFLOAT4& p) { return ImVec2(
            screen.x+(p.x/p.w+1)*.5f*window.width(), screen.y+(1-p.y/p.w)*.5f*window.height()); };
        for (int i = 0; i < 8; ++i)
            for (int bit : { 1, 2, 4 }) if (!(i&bit))
            {
                auto a = corners[i], b = corners[i|bit];
                if (clipBoundsEdge(a, b)) draw->AddLine(project(a), project(b), IM_COL32(255, 190, 50, 255), 2.f);
            }
    }
    private:
        math::Vector3 _position{};
        uint64_t _document{};
        bool _rightClick = false, _dragged = false;
    };
}
