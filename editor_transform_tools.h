#pragma once
#include "core/transform_hierarchy.h"
#include "editor_viewport_state.h"
#include "scene_session.h"
#include "scene_picking.h"

namespace csyren::editor
{
    inline math::MeshBounds transformedBounds(const math::MeshBounds& local, DirectX::FXMMATRIX world)
    {
        math::MeshBounds result;
        if (!local.valid) return result;
        for (int i = 0; i < 8; ++i)
            result.include(math::Vector3(DirectX::XMVector3TransformCoord(DirectX::XMVectorSet(
                (i&1) ? local.max.x : local.min.x, (i&2) ? local.max.y : local.min.y,
                (i&4) ? local.max.z : local.min.z, 1), world)));
        return result;
    }

    inline bool focusBounds(EditorViewportState& view, const math::MeshBounds& bounds)
    {
        using namespace DirectX;
        if (!bounds.valid || !std::isfinite(view.camera.aspectRatio) || view.camera.aspectRatio <= 0 ||
            !std::isfinite(view.camera.fov)) return false;
        const math::Vector3 center((bounds.min + bounds.max) * .5f);
        const float radius = std::max(.1f, (bounds.max - bounds.min).magnitude() * .5f);
        const float nearDistance = std::min(view.camera.near, view.camera.far);
        const float farDistance = std::max(view.camera.near, view.camera.far);
        float distance;
        if (view.camera.projection == core::ProjectionType::Orthographic)
        {
            view.camera.fov = 2.4f * radius / std::min(1.f, view.camera.aspectRatio);
            distance = 2 * radius + nearDistance;
        }
        else
        {
            view.camera.fov = std::clamp(view.camera.fov, 1.f, 179.f);
            const float vertical = XMConvertToRadians(view.camera.fov) * .5f;
            const float horizontal = std::atan(std::tan(vertical) * view.camera.aspectRatio);
            distance = std::max(1.2f * radius / std::sin(std::min(vertical, horizontal)), radius + nearDistance * 2);
        }
        const math::Vector3 forward(XMVector3Rotate(XMVectorSet(0, 0, 1, 0), view.transform.rotation));
        view.transform.position = center - forward * distance;
        view.camera.near = std::max(.001f, nearDistance);
        view.camera.far = std::max(farDistance, distance + radius * 2);
        return true;
    }

    inline bool rayAxisParameter(const PickRay& ray, const math::Vector3& pivot,
        const math::Vector3& axis, float& parameter)
    {
        const auto w = ray.origin - pivot;
        const float alignment = axis.dot(ray.direction);
        const float denominator = 1 - alignment * alignment;
        if (denominator < 1e-4f) return false;
        parameter = (axis.dot(w) - alignment * ray.direction.dot(w)) / denominator;
        return std::isfinite(parameter);
    }

    inline bool rotationPlaneVector(const PickRay& ray, const math::Vector3& pivot,
        const math::Vector3& axis, math::Vector3& vector)
    {
        const float denominator = axis.dot(ray.direction);
        if (std::abs(denominator) < 1e-4f) return false;
        const float distance = axis.dot(pivot - ray.origin) / denominator;
        if (distance < 0 || distance > ray.length) return false;
        vector = ray.origin + ray.direction * distance - pivot;
        const float length = vector.magnitude();
        if (!std::isfinite(length) || length < 1e-6f) return false;
        vector = vector / length; return true;
    }

    inline float rotationStep(const math::Vector3& before, const math::Vector3& after, const math::Vector3& axis)
    { return std::atan2(axis.dot(before.cross(after)), std::clamp(before.dot(after), -1.f, 1.f)); }

    class TransformGesture
    {
    public:
        bool active() const noexcept { return _active; }
        bool valid(SceneSession& session, core::Entity::ID selected) const
        { return _active && !session.playing() && _document == session.documentId() &&
            _instance == session.editorContext().instance && session.resolve(_key) == selected &&
            session.editorScene().hasComponent<core::Transform>(selected); }
        bool begin(SceneSession& session, core::Entity::ID id, GizmoMode mode = GizmoMode::Move)
        {
            if (_active || session.playing()) return false;
            auto transform = session.editorScene().getComponent<core::Transform>(id);
            const auto key = session.keyFor(id);
            if (!transform || !key) return false;
            session.finishEdit();
            _before = *transform; _mode = mode; _key = key; _document = session.documentId();
            _instance = session.editorContext().instance; _active = true; return true;
        }
        bool move(SceneSession& session, const math::Vector3& delta)
        {
            if (_mode != GizmoMode::Move || !valid(session, session.resolve(_key))) return false;
            auto transform = session.editorScene().getComponent<core::Transform>(session.resolve(_key));
            if (!transform) return false;
            math::Matrix4x4 inverseParent;
            if (!math::Matrix4x4::inverse3DAffine(core::worldMatrix(session.editorScene(),
                core::parentOf(session.editorScene(), session.resolve(_key))), inverseParent)) return false;
            const auto next = _before.position + inverseParent.multiplyVector(delta);
            if (!finite(next)) return false;
            transform->position = next;
            session.preview(_key, core::reflection::ComponentFamily::getID<core::Transform>(),
                { "position"_hs.id }, _before.position, next, false);
            return true;
        }
        bool rotate(SceneSession& session, const math::Vector3& worldAxis, float angle)
        {
            if (_mode != GizmoMode::Rotate || !valid(session, session.resolve(_key)) || !std::isfinite(angle) ||
                !finite(worldAxis) || worldAxis.sqrMagnitude() < 1e-8f) return false;
            auto transform = session.editorScene().getComponent<core::Transform>(session.resolve(_key));
            const auto parentRotation = core::worldRotation(session.editorScene(), core::parentOf(session.editorScene(), session.resolve(_key)));
            const math::Vector3 localAxis = parentRotation.inverse() * worldAxis;
            const math::Quaternion next = angle == 0 ? _before.rotation : math::Quaternion(DirectX::XMQuaternionNormalize(
                DirectX::XMQuaternionMultiply(_before.rotation, DirectX::XMQuaternionRotationAxis(localAxis, angle))));
            if (!std::isfinite(next.x) || !std::isfinite(next.y) || !std::isfinite(next.z) || !std::isfinite(next.w)) return false;
            transform->rotation = next;
            session.preview(_key, core::reflection::ComponentFamily::getID<core::Transform>(),
                { "rotation"_hs.id }, _before.rotation, next, false);
            return true;
        }
        bool scale(SceneSession& session, int axis, float factor)
        {
            if (_mode != GizmoMode::Scale || !valid(session, session.resolve(_key)) ||
                axis < 0 || axis > 3 || !std::isfinite(factor) || factor <= 0) return false;
            auto next = _before.scale;
            if (factor != 1) for (int i = 0; i < 3; ++i) if (axis == 3 || axis == i)
            {
                const float value = _before.scale[i] * factor;
                if (!std::isfinite(value)) return false;
                next[i] = std::copysign(std::max(.001f, std::abs(value)), _before.scale[i]);
            }
            if (!finite(next)) return false;
            auto transform = session.editorScene().getComponent<core::Transform>(session.resolve(_key));
            transform->scale = next;
            session.preview(_key, core::reflection::ComponentFamily::getID<core::Transform>(),
                { "scale"_hs.id }, _before.scale, next, false);
            return true;
        }
        void finish(SceneSession& session)
        { if (sameDocument(session)) session.finishEdit(); _active = false; }
        void cancel(SceneSession& session)
        { if (sameDocument(session)) session.cancelEdit(); _active = false; }
    private:
        bool sameDocument(SceneSession& session) const
        { return _active && _document == session.documentId() && _instance == session.editorContext().instance; }
        static bool finite(const math::Vector3& value)
        { return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z); }
        core::Transform _before;
        GizmoMode _mode = GizmoMode::Move;
        EntityKey _key{};
        uint64_t _document{}, _instance{};
        bool _active = false;
    };
}
