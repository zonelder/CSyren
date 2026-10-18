#pragma once
#include "editor_transform_tools.h"
#include "imGui/imgui.h"

namespace csyren::editor
{
    class TransformGizmo
    {
    public:
        bool active() const { return _gesture.active(); }
        void cancel(SceneSession& session) { _gesture.cancel(session); }
        // Move/Rotate use world axes. Scale uses the object's local axes.
        bool render(SceneSession& session, core::Entity::ID selected, const core::Camera& camera,
            DirectX::FXMMATRIX vp, ImVec2 origin, ImVec2 size, bool allowMouse, GizmoMode requested = GizmoMode::Move,
            ImDrawList* overlay = nullptr)
        {
            using namespace DirectX;
            if (active() && !_gesture.valid(session, selected)) _gesture.cancel(session);
            auto transform = session.editorScene().getComponent<core::Transform>(selected);
            if (session.playing() || !transform || size.x <= 0 || size.y <= 0) return false;
            const auto mouse = ImGui::GetIO().MousePos;
            const auto ray = viewportRay(2*(mouse.x-origin.x)/size.x-1, 1-2*(mouse.y-origin.y)/size.y, vp);
            const bool wasActive = active();
            const auto mode = wasActive ? _mode : requested;
            if (active())
            {
                ImGui::SetNextFrameWantCaptureMouse(true);
                if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) { _gesture.cancel(session); return true; }
                if (mode == GizmoMode::Move)
                {
                    float parameter;
                    if (rayAxisParameter(ray, _pivot, _axis, parameter)) _gesture.move(session, _axis * (parameter-_anchor));
                }
                else if (mode == GizmoMode::Rotate)
                {
                    math::Vector3 vector;
                    if (rotationPlaneVector(ray, _pivot, _axis, vector))
                    {
                        _angle += rotationStep(_lastVector, vector, _axis); _lastVector = vector;
                        _gesture.rotate(session, _axis, _angle);
                    }
                }
                else
                {
                    const float pixels = (mouse.x-_pressMouse.x)*_scaleDirection.x + (mouse.y-_pressMouse.y)*_scaleDirection.y;
                    _gesture.scale(session, _dragAxis, std::exp(std::clamp(pixels/80.f, -8.f, 8.f)));
                }
                if (ImGui::IsMouseReleased(ImGuiMouseButton_Left) || !ImGui::IsMouseDown(ImGuiMouseButton_Left)) _gesture.finish(session);
            }
            const auto pivot = core::worldMatrix(session.editorScene(), selected).translation();
            const auto clipPoint = [&](const math::Vector3& point) {
                XMFLOAT4 clip; XMStoreFloat4(&clip, XMVector4Transform(XMVectorSet(point.x,point.y,point.z,1),vp)); return clip;
            };
            const auto centerClip = clipPoint(pivot);
            if (centerClip.w <= 0 || centerClip.z < 0 || centerClip.z > centerClip.w) return wasActive;
            const auto nearPixel = XMVector3TransformCoord(XMVectorSet(centerClip.x/centerClip.w+2/size.x,
                centerClip.y/centerClip.w, centerClip.z/centerClip.w,1),XMMatrixInverse(nullptr,vp));
            const float length = XMVectorGetX(XMVector3Length(XMVectorSubtract(nearPixel,pivot)))*80.f;
            if (!std::isfinite(length) || length < 1e-6f) return wasActive;
            const auto project = [&](const XMFLOAT4& p) { return ImVec2(origin.x+(p.x/p.w+1)*size.x*.5f,
                origin.y+(1-p.y/p.w)*size.y*.5f); };
            const auto center = project(centerClip);
            math::Vector3 axes[] = { {1,0,0}, {0,1,0}, {0,0,1} };
            if (mode == GizmoMode::Scale)
                for (auto& axis : axes) axis = math::Vector3(XMVector3Normalize(core::worldMatrix(session.editorScene(), selected).multiplyVector(axis)));
            struct Segment { ImVec2 a,b; int axis; };
            std::vector<Segment> segments;
            ImVec2 ends[3]{}; bool visible[3]{};
            int hovered = -1; float closest = 9.f;
            const auto consider = [&](ImVec2 a, ImVec2 b, int index, float minimumT) {
                const float dx = b.x-a.x, dy = b.y-a.y, squared = dx*dx+dy*dy;
                if (squared < 1e-5f) return;
                const float t = std::clamp(((mouse.x-a.x)*dx+(mouse.y-a.y)*dy)/squared,minimumT,1.f);
                const float mx = mouse.x-a.x-t*dx, my = mouse.y-a.y-t*dy;
                const float distance = std::sqrt(mx*mx+my*my);
                if (distance < closest) { closest = distance; hovered = index; }
            };
            if (mode == GizmoMode::Rotate)
            {
                const auto centerRay = viewportRay(centerClip.x/centerClip.w,centerClip.y/centerClip.w,vp);
                constexpr int resolution = 96;
                for (int axis = 0; axis < 3; ++axis)
                {
                    if (std::abs(axes[axis].dot(centerRay.direction)) < .08f) continue;
                    const auto& u = axes[(axis+1)%3]; const auto& v = axes[(axis+2)%3];
                    for (int i = 0; i < resolution; ++i)
                    {
                        const float a = XM_2PI*i/resolution, b = XM_2PI*(i+1)/resolution;
                        auto first = clipPoint(pivot+(u*std::cos(a)+v*std::sin(a))*length);
                        auto second = clipPoint(pivot+(u*std::cos(b)+v*std::sin(b))*length);
                        if (!clipBoundsEdge(first,second)) continue;
                        const auto start = project(first), end = project(second);
                        segments.push_back({start,end,axis}); consider(start,end,axis,0.f);
                    }
                }
            }
            else
            {
                for (int i = 0; i < 3; ++i)
                {
                    const auto clip = clipPoint(pivot+axes[i]*length);
                    if (clip.w <= 0 || clip.z < 0 || clip.z > clip.w) continue;
                    ends[i] = project(clip);
                    const float dx = ends[i].x-center.x, dy = ends[i].y-center.y;
                    if (dx*dx+dy*dy < 225) continue;
                    visible[i] = true; segments.push_back({center,ends[i],i}); consider(center,ends[i],i,.15f);
                }
                if (mode == GizmoMode::Scale && std::abs(mouse.x-center.x) <= 7 && std::abs(mouse.y-center.y) <= 7) hovered = 3;
            }
            if (allowMouse && hovered >= 0 && !wasActive && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            {
                bool ready = false;
                if (mode == GizmoMode::Move) ready = rayAxisParameter(ray,pivot,axes[hovered],_anchor);
                else if (mode == GizmoMode::Rotate) ready = rotationPlaneVector(ray,pivot,axes[hovered],_lastVector);
                else
                {
                    _scaleDirection = hovered == 3 ? ImVec2(.70710678f,-.70710678f) :
                        ImVec2(ends[hovered].x-center.x,ends[hovered].y-center.y);
                    const float pixels = std::sqrt(_scaleDirection.x*_scaleDirection.x+_scaleDirection.y*_scaleDirection.y);
                    if (pixels > 0) { _scaleDirection.x /= pixels; _scaleDirection.y /= pixels; ready = true; }
                }
                if (ready && _gesture.begin(session,selected,mode))
                {
                    _pivot = pivot; if (hovered < 3) _axis = axes[hovered];
                    _mode = mode; _dragAxis = hovered; _pressMouse = mouse; _angle = 0;
                }
            }
            auto* draw = overlay ? overlay : ImGui::GetBackgroundDrawList(ImGui::GetMainViewport());
            draw->PushClipRect(origin,ImVec2(origin.x+size.x,origin.y+size.y),true);
            const ImU32 colors[] = { IM_COL32(245,80,80,255),IM_COL32(90,225,110,255),IM_COL32(90,150,255,255),IM_COL32_WHITE };
            const auto color = [&](int index) { return (active() ? _dragAxis == index : allowMouse && hovered == index) ? IM_COL32(255,220,80,255) : colors[index]; };
            for (const auto& segment : segments)
            {
                draw->AddLine(segment.a,segment.b,IM_COL32(20,20,20,220),6.f);
                draw->AddLine(segment.a,segment.b,color(segment.axis),3.f);
            }
            if (mode != GizmoMode::Rotate)
            {
                const char* labels[] = { "X","Y","Z" };
                for (int i = 0; i < 3; ++i) if (visible[i])
                {
                    if (mode == GizmoMode::Scale) draw->AddRectFilled(ImVec2(ends[i].x-5,ends[i].y-5),ImVec2(ends[i].x+5,ends[i].y+5),color(i));
                    else draw->AddCircleFilled(ends[i],5.f,color(i));
                    draw->AddText(ImVec2(ends[i].x+7,ends[i].y-7),color(i),labels[i]);
                }
                if (mode == GizmoMode::Scale) draw->AddRectFilled(ImVec2(center.x-6,center.y-6),ImVec2(center.x+6,center.y+6),color(3));
            }
            draw->PopClipRect();
            return wasActive || active() || (allowMouse && hovered >= 0);
        }
    private:
        TransformGesture _gesture;
        GizmoMode _mode = GizmoMode::Move;
        math::Vector3 _pivot,_axis,_lastVector;
        ImVec2 _pressMouse{},_scaleDirection{};
        float _anchor = 0,_angle = 0;
        int _dragAxis = -1;
    };
}
