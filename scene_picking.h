#pragma once
#include "math/mesh_bounds.h"
#include <span>
#include <cstdint>

namespace csyren::editor
{
    inline math::MeshBounds selectionBounds(math::MeshBounds bounds)
    {
        if (!bounds.valid) return bounds;
        // Visual clearance only; picking continues to use the original mesh bounds.
        constexpr float relativePadding = 0.03f;
        for (int i = 0; i < 3; ++i)
        {
            const float padding = std::max((bounds.max[i] - bounds.min[i]) * relativePadding, 0.01f);
            bounds.min[i] -= padding;
            bounds.max[i] += padding;
        }
        return bounds;
    }
    struct PickRay { math::Vector3 origin, direction; float length; };
    inline math::Vector3 creationPosition(const PickRay& ray)
    {
        const float distance = std::min(5.f, ray.length);
        return ray.origin + ray.direction * distance;
    }
    inline PickRay viewportRay(float x, float y, DirectX::FXMMATRIX viewProjection)
    {
        using namespace DirectX;
        const auto inverse = XMMatrixInverse(nullptr, viewProjection);
        const auto nearPoint = XMVector3TransformCoord(XMVectorSet(x, y, 1, 1), inverse);
        const auto farPoint = XMVector3TransformCoord(XMVectorSet(x, y, 0, 1), inverse);
        const auto delta = XMVectorSubtract(farPoint, nearPoint);
        return { math::Vector3(nearPoint), math::Vector3(XMVector3Normalize(delta)),
            XMVectorGetX(XMVector3Length(delta)) };
    }
    inline bool intersectBounds(const PickRay& ray, const math::MeshBounds& bounds,
        DirectX::FXMMATRIX world, float& distance)
    {
        using namespace DirectX;
        if (!bounds.valid) return false;
        XMVECTOR determinant;
        const auto inverse = XMMatrixInverse(&determinant, world);
        if (std::abs(XMVectorGetX(determinant)) < 1e-12f) return false;
        const math::Vector3 origin(XMVector3TransformCoord(ray.origin, inverse));
        // Preserve world-distance units, including under nonuniform scaling.
        const math::Vector3 direction(XMVector3TransformNormal(ray.direction, inverse));
        float enter = 0, leave = ray.length;
        for (int i = 0; i < 3; ++i)
        {
            if (std::abs(direction[i]) < 1e-12f)
            { if (origin[i] < bounds.min[i] || origin[i] > bounds.max[i]) return false; continue; }
            float a = (bounds.min[i] - origin[i]) / direction[i];
            float b = (bounds.max[i] - origin[i]) / direction[i];
            if (a > b) std::swap(a, b);
            enter = std::max(enter, a); leave = std::min(leave, b);
            if (enter > leave) return false;
        }
        distance = enter; return true;
    }
    inline bool intersectMesh(const PickRay& ray, const math::MeshBounds& bounds,
        std::span<const math::Vector3> positions, std::span<const uint16_t> indices,
        DirectX::FXMMATRIX world, float& distance)
    {
        using namespace DirectX;
        float broadDistance;
        if (!intersectBounds(ray, bounds, world, broadDistance)) return false;
        const auto inverse = XMMatrixInverse(nullptr, world);
        const auto origin = XMVector3TransformCoord(ray.origin, inverse);
        const auto direction = XMVector3TransformNormal(ray.direction, inverse);
        float nearest = ray.length;
        bool hit = false;
        for (size_t i = 0; i + 2 < indices.size(); i += 3)
        {
            if (indices[i] >= positions.size() || indices[i+1] >= positions.size() || indices[i+2] >= positions.size()) continue;
            const XMVECTOR a = positions[indices[i]];
            const auto e1 = XMVectorSubtract(positions[indices[i+1]], a);
            const auto e2 = XMVectorSubtract(positions[indices[i+2]], a);
            const auto p = XMVector3Cross(direction, e2);
            const float determinant = XMVectorGetX(XMVector3Dot(e1, p));
            if (std::abs(determinant) < 1e-10f) continue;
            const auto t = XMVectorSubtract(origin, a);
            const float u = XMVectorGetX(XMVector3Dot(t, p)) / determinant;
            constexpr float edgeTolerance = 1e-6f;
            if (u < -edgeTolerance || u > 1 + edgeTolerance) continue;
            const auto q = XMVector3Cross(t, e1);
            const float v = XMVectorGetX(XMVector3Dot(direction, q)) / determinant;
            if (v < -edgeTolerance || u+v > 1 + edgeTolerance) continue;
            const float d = XMVectorGetX(XMVector3Dot(e2, q)) / determinant;
            if (d >= 0 && d <= nearest) { nearest = d; hit = true; }
        }
        if (hit) distance = nearest;
        return hit;
    }
    // Clip before dividing by w: edges crossing the camera must stay finite.
    inline bool clipBoundsEdge(DirectX::XMFLOAT4& a, DirectX::XMFLOAT4& b)
    {
        const auto plane = [](const DirectX::XMFLOAT4& p, int i) {
            switch (i) { case 0: return p.w+p.x; case 1: return p.w-p.x;
            case 2: return p.w+p.y; case 3: return p.w-p.y;
            case 4: return p.z; default: return p.w-p.z; }
        };
        for (int i = 0; i < 6; ++i)
        {
            const float da = plane(a, i), db = plane(b, i);
            if (da < 0 && db < 0) return false;
            if (da < 0 || db < 0)
            {
                const float t = da / (da-db);
                DirectX::XMFLOAT4 p{ a.x+t*(b.x-a.x), a.y+t*(b.y-a.y),
                    a.z+t*(b.z-a.z), a.w+t*(b.w-a.w) };
                if (da < 0) a = p; else b = p;
            }
        }
        return a.w > 1e-6f && b.w > 1e-6f;
    }
}
