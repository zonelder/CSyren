#include "animation.h"
#include "core/meta.h"

namespace csyren
{
    void Animator::describe()
    {
        using namespace core::reflection;
        MetaFactory<Animator>{}.type("Animator"_hs)
            .data<&Animator::clip>("clip"_hs)
            .data<&Animator::time>("time"_hs)
            .data<&Animator::speed>("speed"_hs)
            .data<&Animator::loop>("loop"_hs)
            .data<&Animator::playing>("playing"_hs)
            .data<&Animator::finished>("finished"_hs, {}, FieldFlags::EditorVisible | FieldFlags::Transient | FieldFlags::ReadOnly);
    }
}
