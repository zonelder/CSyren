#pragma once
#include <cmath>
#include "scene_picking.h"
#include "editor_selection.h"
#include "editor_viewport_state.h"
#include "scene_session.h"
#include "mesh_filter.h"
#include "dx12_graphic/resource_manager.h"
#include "dx12_graphic/renderer.h"
#include "core/window.h"
#include "imGui/imgui.h"
#include "scene_gizmo.h"
#include "dx12_graphic/camera_render_target.h"
#include "dx12_graphic/lighting/light_components.h"

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
        static math::MeshBounds localBounds(core::Scene& scene, render::ResourceManager& resources, core::Entity::ID id)
        {
            math::MeshBounds bounds;
            if (scene.hasComponent<render::MeshFilter>(id))
            {
                auto filter = scene.getComponent<render::MeshFilter>(id);
                const auto* mesh = resources.getMesh(filter->mesh);
                if (mesh && mesh->bounds().valid) return mesh->bounds();
            }
            bounds.include({ -.1f, -.1f, -.1f }); bounds.include({ .1f, .1f, .1f });
            return bounds;
        }
        static math::MeshBounds focusTarget(core::Scene& scene, render::ResourceManager& resources, core::Entity::ID id)
        {
            math::MeshBounds result;
            const auto& entities = scene.entities();
            if (!entities.contains(id)) return result;
            core::WorldTransforms worlds; worlds.update(scene);
            cstdmf::tree::preorder(id, [&](auto key) -> const auto& { return entities.try_get(key)->children; }, [&](auto key) {
                if (!scene.hasComponent<core::Transform>(key)) return;
                const auto bounds = transformedBounds(localBounds(scene, resources, key), worlds.matrix(key));
                if (bounds.valid) { result.include(bounds.min); result.include(bounds.max); }
            });
            return result;
        }
        struct Hit { core::Entity::ID entity = core::Entity::invalidID; float distance = 0; };
        static Hit raycast(core::Scene& scene, render::ResourceManager& resources, const PickRay& ray)
        {
            Hit result{ core::Entity::invalidID, ray.length };
            core::WorldTransforms worlds; worlds.update(scene);
            for (auto [id, transform, filter, renderer] : scene.view<core::Transform, render::MeshFilter, render::MeshRenderer>())
            {
                const auto* mesh = resources.getMesh(filter.mesh);
                float distance;
                if (!mesh || !resources.getMaterial(renderer.material)) continue;
                if (intersectMesh(ray, mesh->bounds(), mesh->positions(), mesh->indices(), worlds.matrix(id), distance)
                    && (distance < result.distance || result.entity == core::Entity::invalidID))
                    result = { id, distance };
            }
            return result;
        }
    public:
    void cameraView(std::shared_ptr<render::details::CameraRenderView> view) { _cameraView = std::move(view); }
    bool manipulating() const { return _gizmo.active(); }
    void render(bool* open = nullptr,ViewportCamera mode = ViewportCamera::Scene)
    {
        auto& session = *core::Services::get<SceneSession>();
        auto& view = *core::Services::get<EditorViewportState>();
        const bool game = mode == ViewportCamera::Game;
        auto& viewport = game ? view.gameViewport : view.viewport;
        viewport = {};
        if (open && !*open) { if (!game) view.navigation = {}; _gizmo.cancel(session); return; }
        const bool visible = ImGui::Begin(game ? "Game" : "Scene",open,ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse);
        struct EndWindow { ~EndWindow() { ImGui::End(); } } endWindow;
        if (!visible) { if (!game) view.navigation = {}; _gizmo.cancel(session); return; }
        auto* renderer = core::Services::get<render::Renderer>();
        const auto target = game ? renderer->primaryCameraTarget() : (_cameraView ? _cameraView->target : nullptr);
        if (game && ImGui::Button("Render again")) renderer->requestPrimaryCameraRender();
        if (game && !target) { ImGui::TextUnformatted("No game camera in the active scene."); return; }
        const auto available = ImGui::GetContentRegionAvail();
        if (!target || !target->color() || available.x < 1 || available.y < 1)
        { if (!game) view.navigation = {}; _gizmo.cancel(session); return; }
        target->request(static_cast<UINT>(std::clamp(available.x,1.f,8192.f)),static_cast<UINT>(std::clamp(available.y,1.f,8192.f)));
        const auto start = ImGui::GetCursorScreenPos();
        auto image = ViewportImage::fit(start.x,start.y,available.x,available.y,float(target->width())/target->height());
        ImGui::SetCursorScreenPos(ImVec2(image.x,image.y));
        ImGui::Image(ImTextureRef(static_cast<ImTextureID>(target->srv().ptr)),ImVec2(image.width,image.height));
        image.hovered = ImGui::IsItemHovered(); image.focused = ImGui::IsWindowFocused();
        viewport = image;
        const auto& io = ImGui::GetIO();
        const bool popup = ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopupId);
        if (game) return;
        if (image.hovered && !popup && !_gizmo.active() && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) view.navigation.captured = true;
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Right)) view.navigation.captured = false;
        view.navigation.forward = float(ImGui::IsKeyDown(ImGuiKey_W))-float(ImGui::IsKeyDown(ImGuiKey_S));
        view.navigation.right = float(ImGui::IsKeyDown(ImGuiKey_D))-float(ImGui::IsKeyDown(ImGuiKey_A));
        view.navigation.active = view.navigation.captured && !popup && !io.WantTextInput &&
            (ImGui::IsMouseDragging(ImGuiMouseButton_Right) || view.navigation.forward != 0 || view.navigation.right != 0);
        view.navigation.dx = view.navigation.active ? io.MouseDelta.x : 0;
        view.navigation.dy = view.navigation.active ? io.MouseDelta.y : 0;
        renderInteraction(image);
    }
    private:
    void renderInteraction(const ViewportImage& image)
    {
        using namespace DirectX;
        auto& session = *core::Services::get<SceneSession>();
        auto& selection = *core::Services::get<EditorSelection>();
        auto& resources = *core::Services::get<render::ResourceManager>();
        const auto* frame = &_cameraView->engine;
        const auto vp = XMLoadFloat4x4(&frame->viewProjectionMatrix);
        const auto mouse = ImGui::GetIO().MousePos;
        const auto ndc = image.ndc(mouse.x,mouse.y);
        const bool viewportMouse = image.hovered && !ImGui::IsAnyItemActive();
        auto& view = *core::Services::get<EditorViewportState>();
        const auto& io = ImGui::GetIO();
        view.camera.aspectRatio = image.width/image.height;
        if (image.focused && !_gizmo.active() && !io.WantTextInput && !io.KeyCtrl && !io.KeyAlt && !ImGui::IsAnyItemActive() &&
            !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId) && !ImGui::IsMouseDown(ImGuiMouseButton_Right))
        {
            if (ImGui::IsKeyPressed(ImGuiKey_W, false)) view.gizmoMode = GizmoMode::Move;
            if (ImGui::IsKeyPressed(ImGuiKey_E, false)) view.gizmoMode = GizmoMode::Rotate;
            if (ImGui::IsKeyPressed(ImGuiKey_R, false)) view.gizmoMode = GizmoMode::Scale;
        }
        if (image.focused && !_gizmo.active() && !io.WantTextInput && !io.KeyCtrl && !io.KeyAlt && !ImGui::IsAnyItemActive() &&
            !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId) && !ImGui::IsMouseDown(ImGuiMouseButton_Right) &&
            ImGui::IsKeyPressed(ImGuiKey_F, false))
            focusBounds(view, focusTarget(session.editorScene(), resources, selection.selected()));
        if (session.playing()) { _gizmo.cancel(session); return; }
        const bool gizmoMouse = _gizmo.render(session, selection.selected(), view.camera, vp,
            ImVec2(image.x,image.y),ImVec2(image.width,image.height),
            viewportMouse && !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId) && !ImGui::IsMouseDown(ImGuiMouseButton_Right), view.gizmoMode,ImGui::GetWindowDrawList());
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Right))
        {
            _rightClick = viewportMouse;
            _dragged = false;
        }
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Right) || view.navigation.active) _dragged = true;
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Right))
        {
            if (_rightClick && !_dragged && viewportMouse)
            {
                auto ray = viewportRay(ndc.x,ndc.y,vp);
                constexpr float creationRayDistance = 100.f;
                ray.length = std::min(ray.length, creationRayDistance);
                const auto hit = raycast(session.editorScene(), resources, ray);
                _position = hit.entity != core::Entity::invalidID ?
                    ray.origin + ray.direction * hit.distance : creationPosition(ray);
                _document = session.documentId();
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
        if (!gizmoMouse && viewportMouse && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            const auto ray = viewportRay(ndc.x,ndc.y,vp);
            const auto hit = raycast(session.editorScene(), resources, ray);
            session.finishEdit(); selection.select(hit.entity);
        }
        auto& scene = session.editorScene();
        drawLightGizmos(scene, vp, image, selection.selected());
        if (!scene.hasComponent<core::Transform>(selection.selected())) return;
        const auto bounds = selectionBounds(localBounds(scene, resources, selection.selected()));
        XMFLOAT4 corners[8];
        const auto matrix = XMMatrixMultiply(core::worldMatrix(scene, selection.selected()), vp);
        for (int i = 0; i < 8; ++i)
            XMStoreFloat4(&corners[i], XMVector4Transform(XMVectorSet(
                (i&1) ? bounds.max.x : bounds.min.x, (i&2) ? bounds.max.y : bounds.min.y,
                (i&4) ? bounds.max.z : bounds.min.z, 1), matrix));
        auto* draw = ImGui::GetWindowDrawList();
        draw->PushClipRect(ImVec2(image.x,image.y),ImVec2(image.x+image.width,image.y+image.height),true);
        const auto project = [&](const XMFLOAT4& p) { return ImVec2(
            image.x+(p.x/p.w+1)*.5f*image.width, image.y+(1-p.y/p.w)*.5f*image.height); };
        for (int i = 0; i < 8; ++i)
            for (int bit : { 1, 2, 4 }) if (!(i&bit))
            {
                auto a = corners[i], b = corners[i|bit];
                if (clipBoundsEdge(a, b)) draw->AddLine(project(a), project(b), IM_COL32(255, 190, 50, 255), 2.f);
            }
        draw->PopClipRect();
    }
    // Light markers: a dot per point light (wire sphere of its radius when
    // selected) and an arrow per directional light along the light direction.
    static void drawLightGizmos(core::Scene& scene, DirectX::CXMMATRIX vp, const ViewportImage& image, core::Entity::ID selected)
    {
        using namespace DirectX;
        auto* draw = ImGui::GetWindowDrawList();
        draw->PushClipRect(ImVec2(image.x,image.y),ImVec2(image.x+image.width,image.y+image.height),true);
        const auto project = [&](const math::Vector3& p, ImVec2& out) {
            XMFLOAT4 c; XMStoreFloat4(&c, XMVector4Transform(XMVectorSet(p.x, p.y, p.z, 1), vp));
            if (c.w <= 1e-4f) return false;
            out = ImVec2(image.x + (c.x / c.w + 1) * .5f * image.width, image.y + (1 - c.y / c.w) * .5f * image.height);
            return true;
        };
        const auto circle = [&](const math::Vector3& center, float radius, const math::Vector3& a, const math::Vector3& b, ImU32 color) {
            ImVec2 previous{}; bool hasPrevious = false;
            for (int i = 0; i <= 48; ++i) {
                const float t = float(i) / 48 * 6.2831853f;
                const math::Vector3 p = center + a * (std::cos(t) * radius) + b * (std::sin(t) * radius);
                ImVec2 current;
                if (!project(p, current)) { hasPrevious = false; continue; }
                if (hasPrevious) draw->AddLine(previous, current, color, 1.f);
                previous = current; hasPrevious = true;
            }
        };
        for (auto [id, transform, light] : scene.view<const core::Transform, const render::PointLight>())
        {
            const auto position = core::worldMatrix(scene, id).translation();
            ImVec2 center;
            if (!project(position, center)) continue;
            XMFLOAT4 rgb; XMStoreFloat4(&rgb, light.color);
            const ImU32 color = IM_COL32(int(rgb.x * 255), int(rgb.y * 255), int(rgb.z * 255), light.enabled ? 255 : 90);
            draw->AddCircleFilled(center, 5.f, color);
            draw->AddCircle(center, 7.f, IM_COL32(0, 0, 0, 160), 0, 1.5f);
            if (id == selected) {
                circle(position, light.radius, math::Vector3::right, math::Vector3::up, color);
                circle(position, light.radius, math::Vector3::right, math::Vector3::forward, color);
                circle(position, light.radius, math::Vector3::up, math::Vector3::forward, color);
            }
        }
        for (auto [id, transform, light] : scene.view<const core::Transform, const render::DirectionalLight>())
        {
            const auto world = core::worldMatrix(scene, id);
            const auto position = world.translation();
            auto direction = world.multiplyVector(math::Vector3::forward);
            if (direction.sqrMagnitude() < 1e-8f) continue;
            direction.normalize();
            ImVec2 from, to;
            if (!project(position, from) || !project(position + direction * 3.f, to)) continue;
            XMFLOAT4 rgb; XMStoreFloat4(&rgb, light.color);
            const ImU32 color = IM_COL32(int(rgb.x * 255), int(rgb.y * 255), int(rgb.z * 255), light.enabled ? 255 : 90);
            draw->AddLine(from, to, color, id == selected ? 3.f : 2.f);
            draw->AddCircleFilled(to, 4.f, color);
            draw->AddCircle(from, 8.f, color, 0, 2.f);
        }
        draw->PopClipRect();
    }
    private:
    std::shared_ptr<render::details::CameraRenderView> _cameraView;
        TransformGizmo _gizmo;
        math::Vector3 _position{};
        uint64_t _document{};
        bool _rightClick = false, _dragged = false;
    };
}
