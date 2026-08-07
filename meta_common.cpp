#include "meta_common.h"
#include "math/math.h"
#include "core/meta.h"


void MetaCommonDescriber::describe()
{
    namespace meta = csyren::core::reflection;
    using namespace csyren::math;

    meta::MetaFactory<float>{}.primitiveType("float"_hs);
    meta::MetaFactory<int>{}.primitiveType("int"_hs);
    meta::MetaFactory<bool>{}.primitiveType("bool"_hs);

    meta::MetaFactory<Vector3>{}.type("Vector3"_hs);
    meta::MetaFactory<Vector2>{}.type("Vector2"_hs);
    meta::MetaFactory<Vector4>{}.type("Vector4"_hs);
    meta::MetaFactory<Color>{}.type("Color"_hs);
    meta::MetaFactory<Quaternion>{}.type("Quaternion"_hs);
}