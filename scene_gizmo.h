#pragma once
#include "editor_transform_tools.h"
#include "imGui/imgui.h"

namespace csyren::editor
{
    class MoveGizmo
    {
    public:
        bool active() const { return _gesture.active(); }
        void cancel(SceneSession& session) { _gesture.cancel(session); }
        // Returns true when the pointer belongs to a handle or an active gesture.
        bool render(SceneSession& session, core::Entity::ID selected, const core::Camera& camera,
            DirectX::FXMMATRIX viewProjection, ImVec2 origin, ImVec2 size, bool allowMouse)
        {
            using namespace DirectX;
            if (_gesture.active() && !_gesture.valid(session, selected)) _gesture.cancel(session);
            auto transform = session.editorScene().getComponent<core::Transform>(selected);
            if (session.playing() || !transform || size.x <= 0 || size.y <= 0) return false;
            const auto mouse = ImGui::GetIO().MousePos;
            const auto ray = viewportRay(2*(mouse.x-origin.x)/size.x-1, 1-2*(mouse.y-origin.y)/size.y, viewProjection);
            const bool wasActive = active();
            if (active())
            {
                ImGui::SetNextFrameWantCaptureMouse(true);
                if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) { _gesture.cancel(session); return true; }
                float parameter;
                if (rayAxisParameter(ray, _pivot, _axis, parameter))
                    _gesture.move(session, _axis * (parameter - _anchor));
                if (ImGui::IsMouseReleased(ImGuiMouseButton_Left) || !ImGui::IsMouseDown(ImGuiMouseButton_Left))
                    _gesture.finish(session);
            }
            const auto pivot = transform->position;
            XMFLOAT4 centerClip;
            XMStoreFloat4(&centerClip, XMVector4Transform(XMVectorSet(pivot.x, pivot.y, pivot.z, 1), viewProjection));
            if (centerClip.w <= 0 || centerClip.z < 0 || centerClip.z > centerClip.w) return wasActive;
            const auto inv = XMMatrixInverse(nullptr, viewProjection);
            const auto nearPixel = XMVector3TransformCoord(XMVectorSet(centerClip.x/centerClip.w + 2/size.x,
                centerClip.y/centerClip.w, centerClip.z/centerClip.w, 1), inv);
            const float length = XMVectorGetX(XMVector3Length(XMVectorSubtract(nearPixel, pivot))) * 80.f;
            if (!std::isfinite(length) || length < 1e-6f) return wasActive;
            const auto project = [&](const XMFLOAT4& p) { return ImVec2(origin.x+(p.x/p.w+1)*size.x*.5f,
                origin.y+(1-p.y/p.w)*size.y*.5f); };
            const auto center = project(centerClip);
            int hovered = -1;
            float closest = 9.f;
            ImVec2 ends[3]; bool visible[3]{};
            const math::Vector3 axes[] = { {1,0,0}, {0,1,0}, {0,0,1} };
            for (int i = 0; i < 3; ++i)
            {
                const auto endpoint = pivot + axes[i]*length;
                XMFLOAT4 clip;
                XMStoreFloat4(&clip, XMVector4Transform(XMVectorSet(endpoint.x, endpoint.y, endpoint.z, 1), viewProjection));
                if (clip.w <= 0 || clip.z < 0 || clip.z > clip.w) continue;
                ends[i] = project(clip);
                const float dx = ends[i].x-center.x, dy = ends[i].y-center.y;
                const float squaredLength = dx*dx+dy*dy;
                if (squaredLength < 225) continue; // axis almost aligned with the viewing ray
                visible[i] = true;
                const float t = std::clamp(((mouse.x-center.x)*dx+(mouse.y-center.y)*dy)/squaredLength, .15f, 1.f);
                const float mx = mouse.x-center.x-t*dx, my = mouse.y-center.y-t*dy;
                const float distance = std::sqrt(mx*mx+my*my);
                if (distance < closest) { closest = distance; hovered = i; }
            }
            if (allowMouse && hovered >= 0 && !wasActive && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            {
                float anchor;
                if (rayAxisParameter(ray, pivot, axes[hovered], anchor) && _gesture.begin(session, selected))
                { _pivot = pivot; _axis = axes[hovered]; _anchor = anchor; _dragAxis = hovered; }
            }
            auto* draw = ImGui::GetBackgroundDrawList(ImGui::GetMainViewport());
            draw->PushClipRect(origin, ImVec2(origin.x+size.x, origin.y+size.y), true);
            const ImU32 colors[] = { IM_COL32(245,80,80,255), IM_COL32(90,225,110,255), IM_COL32(90,150,255,255) };
            const char* labels[] = { "X", "Y", "Z" };
            for (int i = 0; i < 3; ++i) if (visible[i])
            {
                const bool highlight = active() ? _dragAxis == i : allowMouse && hovered == i;
                const auto color = highlight ? IM_COL32(255,220,80,255) : colors[i];
                draw->AddLine(center, ends[i], IM_COL32(20,20,20,220), 6.f);
                draw->AddLine(center, ends[i], color, highlight ? 4.f : 3.f);
                draw->AddCircleFilled(ends[i], 5.f, color);
                draw->AddText(ImVec2(ends[i].x+7, ends[i].y-7), color, labels[i]);
            }
            draw->PopClipRect();
            return wasActive || active() || (allowMouse && hovered >= 0);
        }
    private:
        TranslationGesture _gesture;
        math::Vector3 _pivot, _axis;
        float _anchor = 0;
        int _dragAxis = -1;
    };
}
