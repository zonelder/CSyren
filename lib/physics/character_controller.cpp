#include "pch.h"
#include "character_controller.h"
#include "core/meta.h"

namespace csyren::physics
{
	void CharacterController::describe()
	{
		using namespace core::reflection;
		CollisionLayer::describe();
		MetaFactory<CharacterController>{}.type("CharacterController"_hs)
			.data<&CharacterController::radius>("radius"_hs)
			.data<&CharacterController::height>("height"_hs)
			.data<&CharacterController::maxSlopeDegrees>("maxSlopeDegrees"_hs)
			.data<&CharacterController::stepHeight>("stepHeight"_hs)
			.data<&CharacterController::stickToFloor>("stickToFloor"_hs)
			.data<&CharacterController::mass>("mass"_hs)
			.data<&CharacterController::layer>("layer"_hs)
			.data<&CharacterController::velocity>("velocity"_hs, {}, FieldFlags::EditorVisible | FieldFlags::Transient | FieldFlags::ReadOnly)
			.data<&CharacterController::grounded>("grounded"_hs, {}, FieldFlags::EditorVisible | FieldFlags::Transient | FieldFlags::ReadOnly)
			.data<&CharacterController::groundNormal>("groundNormal"_hs, {}, FieldFlags::EditorVisible | FieldFlags::Transient | FieldFlags::ReadOnly)
			.data<&CharacterController::currentHeight>("currentHeight"_hs, {}, FieldFlags::EditorVisible | FieldFlags::Transient | FieldFlags::ReadOnly)
			.data<&CharacterController::wallContact>("wallContact"_hs, {}, FieldFlags::EditorVisible | FieldFlags::Transient | FieldFlags::ReadOnly);
	}
}
