#include "pch.h"
#include "colliders.h"
#include "core/meta.h"


namespace csyren::physics
{
    void BoxCollider::describe()
    {
        core::reflection::MetaFactory<BoxCollider>{}.type("BoxCollider"_hs)
            .data<&BoxCollider::size>("size"_hs)
            .data<&BoxCollider::offset>("offset"_hs)
            .data<&BoxCollider::rotation>("rotation"_hs)
            .data<&BoxCollider::isTrigger>("isTrigger"_hs);
    }

    void SphereCollider::describe()
    {
        core::reflection::MetaFactory<SphereCollider>{}.type("SphereCollider"_hs)
            .data<&SphereCollider::radius>("radius"_hs)
            .data<&SphereCollider::center>("center"_hs)
            .data<&SphereCollider::isTrigger>("isTrigger"_hs);
    }

    void CapsuleCollider::describe()
    {
        core::reflection::MetaFactory<CapsuleCollider>{}.type("CapsuleCollider"_hs)
            .data<&CapsuleCollider::radius>("radius"_hs)
            .data<&CapsuleCollider::height>("height"_hs)
            .data<&CapsuleCollider::offset>("offset"_hs)
            .data<&CapsuleCollider::rotation>("rotation"_hs)
            .data<&CapsuleCollider::isTrigger>("isTrigger"_hs);
    }

    REFLECT(BoxCollider);
    REFLECT(SphereCollider);
    REFLECT(CapsuleCollider);
}