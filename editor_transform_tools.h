#pragma once
#include "editor_view.h"
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

    inline bool focusBounds(EditorView& view, const math::MeshBounds& bounds)
    {
        using namespace DirectX;
        if (!bounds.valid || !std::isfinite(view.camera.aspectRatio) || view.camera.aspectRatio <= 0 ||
            !std::isfinite(view.camera.fov)) return false;
        const math::Vector3 center((bounds.min + bounds.max) * .5f);
        const float radius = std::max(.1f, (bounds.max - bounds.min).magnitude() * .5f);
        const float nearDistance = std::min(view.camera.near, view.camera.far);
        const float farDistance = std::max(view.camera.near, view.camera.far);
        float distance;
        if (view.camera.projection == core::Orthographic)
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

    class TranslationGesture
    {
    public:
        bool active() const noexcept { return _active; }
        bool valid(SceneSession& session, core::Entity::ID selected) const
        { return _active && !session.playing() && _document == session.documentId() &&
            _instance == session.editorContext().instance && session.resolve(_key) == selected &&
            session.editorScene().hasComponent<core::Transform>(selected); }
        bool begin(SceneSession& session, core::Entity::ID id)
        {
            if (_active || session.playing()) return false;
            auto transform = session.editorScene().getComponent<core::Transform>(id);
            const auto key = session.keyFor(id);
            if (!transform || !key) return false;
            session.finishEdit();
            _before = transform->position; _key = key; _document = session.documentId();
            _instance = session.editorContext().instance; _active = true; return true;
        }
        bool move(SceneSession& session, const math::Vector3& delta)
        {
            if (!valid(session, session.resolve(_key))) return false;
            auto transform = session.editorScene().getComponent<core::Transform>(session.resolve(_key));
            if (!transform) return false;
            const auto next = _before + delta;
            transform->position = next;
            session.preview(_key, core::reflection::ComponentFamily::getID<core::Transform>(),
                { "position"_hs.id }, _before, next, false);
            return true;
        }
        void finish(SceneSession& session)
        { if (sameDocument(session)) session.finishEdit(); _active = false; }
        void cancel(SceneSession& session)
        { if (sameDocument(session)) session.cancelEdit(); _active = false; }
    private:
        bool sameDocument(SceneSession& session) const
        { return _active && _document == session.documentId() && _instance == session.editorContext().instance; }
        math::Vector3 _before;
        EntityKey _key{};
        uint64_t _document{}, _instance{};
        bool _active = false;
    };
}
