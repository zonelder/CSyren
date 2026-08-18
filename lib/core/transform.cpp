#include "pch.h"
#include "core/meta.h"
#include "transform.h"

namespace csyren::core
{
    void Transform::describe()
    {
        csyren::core::reflection::MetaFactory<csyren::core::Transform>{}
        .type("Transform"_hs)
            .data<&csyren::core::Transform::position>("position"_hs)
            .data<&csyren::core::Transform::rotation>("rotation"_hs)
            .data<&csyren::core::Transform::scale>("scale"_hs);
    }
}