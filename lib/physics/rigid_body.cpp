#include "pch.h"
#include "rigid_body.h"
#include "core/meta.h"


namespace csyren::physics
{
	void RigidBody::describe()
	{
        core::reflection::MetaFactory<RigidBody>{}.type("RigidBody"_hs)
            //.data<&RigidBody::type>("type"_hs)
            .data<&RigidBody::mass>("mass"_hs)
            .data<&RigidBody::useGravity>("useGravity"_hs)
            .data<&RigidBody::friction>("friction"_hs)
            .data<&RigidBody::restitution>("restitution"_hs)
            .data<&RigidBody::linearVelocity>("linearVelocity"_hs)
            .data<&RigidBody::angularVelocity>("angularVelocity"_hs);
	}

	REFLECT(RigidBody);
}