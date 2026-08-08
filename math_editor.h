#pragma once
#include "editor_base.h"
#include "math/math.h"

namespace csyren::editor
{
	class FloatEditor : public IEditor
	{
	public:
		void draw(core::reflection::MetaAny& any, const char* label) override
		{
			auto& value = any.get<float>();
			if (ImGui::DragFloat(label, &value, 0.1f))
			{

			}
		}
	};

	class IntEditor : public IEditor
	{
	public:
		void draw(core::reflection::MetaAny& any, const char* label) override
		{
			auto& value = any.get<int>();
			if (ImGui::DragInt(label, &value))
			{
			}

		}
	};

	class BoolEditor : public IEditor
	{
		void draw(core::reflection::MetaAny& any,const char* label) override
		{
			auto& value = any.get<bool>();
			if (ImGui::Checkbox(label, &value))
			{

			}
		}
	};

	class Vector2Editor : public IEditor
	{
	public:
		void draw(core::reflection::MetaAny& any, const char* label) override
		{
			auto& value = any.get<math::Vector2>();
			if (ImGui::DragFloat2(label, &value.x, 0.1f))
			{

			}
		}
	};

	class Vector3Editor : public IEditor
	{
	public:
		void draw(core::reflection::MetaAny& any, const char* label) override
		{
			auto& value = any.get<math::Vector3>();
			if (ImGui::DragFloat3(label, &value.x, 0.1f))
			{

			}
		}
	};

	class Vector4Editor : public IEditor
	{
	public:
		void draw(core::reflection::MetaAny& any, const char* label) override
		{
			auto& value = any.get<math::Vector4>();
			if (ImGui::DragFloat4(label, &value.x, 0.1f))
			{
				
			}
		}
	};

	class QuaternionEditor : public IEditor
	{
	public:
		void draw(core::reflection::MetaAny& any, const char* label) override
		{
			auto& q = any.get<math::Quaternion>();
			auto angles = q.eulerAngles();
			if (ImGui::DragFloat3(label, &angles.x, 0.01f))
			{
				
				q = math::Quaternion::euler(angles);
			}
		}
	};

	class ColorEditor : public IEditor
	{
	public:
		void draw(core::reflection::MetaAny& any, const char* label) override
		{
			auto& c = any.get<math::Color>();
			if (ImGui::ColorEdit4(label, &c[0]))
			{
			}
		}
	};
}