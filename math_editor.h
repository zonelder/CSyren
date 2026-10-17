#pragma once
#include "editor_base.h"
#include "math/math.h"
#include "core/camera.h"

namespace csyren::editor
{
    class CameraRenderTypeEditor : public IEditor
    {
    public:
        void draw(core::reflection::MetaAny& any,const char* label,ValueEditContext& context) override
        {
            auto& value = any.get<core::CameraRenderType>();
            context.draw(value,[&] {
                int selected = static_cast<int>(value);
                if (!ImGui::Combo(label,&selected,"Each frame\0Once\0")) return false;
                value = static_cast<core::CameraRenderType>(selected); return true;
            });
        }
    };
    class FloatEditor : public IEditor
    {
    public:
        void draw(core::reflection::MetaAny& any, const char* label, ValueEditContext& context) override
        { auto& v = any.get<float>(); context.draw(v, [&] { return ImGui::DragFloat(label, &v, 0.1f); }); }
    };
    class IntEditor : public IEditor
    {
    public:
        void draw(core::reflection::MetaAny& any, const char* label, ValueEditContext& context) override
        { auto& v = any.get<int>(); context.draw(v, [&] { return ImGui::DragInt(label, &v); }); }
    };
    class BoolEditor : public IEditor
    {
    public:
        void draw(core::reflection::MetaAny& any, const char* label, ValueEditContext& context) override
        { auto& v = any.get<bool>(); context.draw(v, [&] { return ImGui::Checkbox(label, &v); }); }
    };
    class Vector2Editor : public IEditor
    {
    public:
        void draw(core::reflection::MetaAny& any, const char* label, ValueEditContext& context) override
        { auto& v = any.get<math::Vector2>(); context.draw(v, [&] { return ImGui::DragFloat2(label, &v.x, 0.1f); }); }
    };
    class Vector3Editor : public IEditor
    {
    public:
        void draw(core::reflection::MetaAny& any, const char* label, ValueEditContext& context) override
        { auto& v = any.get<math::Vector3>(); context.draw(v, [&] { return ImGui::DragFloat3(label, &v.x, 0.1f); }); }
    };
    class Vector4Editor : public IEditor
    {
    public:
        void draw(core::reflection::MetaAny& any, const char* label, ValueEditContext& context) override
        { auto& v = any.get<math::Vector4>(); context.draw(v, [&] { return ImGui::DragFloat4(label, &v.x, 0.1f); }); }
    };
    class QuaternionEditor : public IEditor
    {
    public:
        void draw(core::reflection::MetaAny& any, const char* label, ValueEditContext& context) override
        {
            auto& q = any.get<math::Quaternion>(); auto angles = q.eulerAngles();
            context.draw(q, [&] {
                if (!ImGui::DragFloat3(label, &angles.x, 0.01f)) return false;
                q = math::Quaternion::euler(angles); return true;
            });
        }
    };
    class ColorEditor : public IEditor
    {
    public:
        void draw(core::reflection::MetaAny& any, const char* label, ValueEditContext& context) override
        { auto& v = any.get<math::Color>(); context.draw(v, [&] { return ImGui::ColorEdit4(label, &v[0]); }); }
    };
}
