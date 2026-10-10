#include "meta_common.h"
#include "math/math.h"
#include "core/meta_archive.h"

namespace
{
    // Existing math values keep their compact array representation in every archive.
    template<class T, size_t N>
    csyren::core::reflection::MetaArchiveOps vectorArchive()
    {
        using namespace csyren::core;
        return {
            [](ArchiveWriter& writer, SerializationContext&, const reflection::MetaType&, const void* value) -> bool {
                DirectX::XMFLOAT4 components;
                DirectX::XMStoreFloat4(&components, static_cast<DirectX::XMVECTOR>(*static_cast<const T*>(value)));
                const float values[] = { components.x, components.y, components.z, components.w };
                if (!writer.sequence(N)) return false;
                for (size_t i = 0; i < N; ++i)
                    if (!writer.beginElement(i) || !writer.floating(values[i]) || !writer.endElement()) return false;
                return writer.endSequence();
            },
            [](ArchiveReader& reader, SerializationContext&, const reflection::MetaType&, void* value) -> bool {
                size_t count{}; float values[4]{};
                if (!reader.sequence(count) || count != N) return false;
                for (size_t i = 0; i < N; ++i) {
                    double component{};
                    if (!reader.beginElement(i) || !reader.floating(component) || !reader.endElement() || !std::isfinite(component) || std::abs(component) > (std::numeric_limits<float>::max)()) return false;
                    values[i] = static_cast<float>(component);
                }
                if (!reader.endSequence()) return false;
                *static_cast<T*>(value) = DirectX::XMVectorSet(values[0], values[1], values[2], values[3]); return true;
            }
        };
    }
}

void MetaCommonDescriber::describe()
{
    namespace meta = csyren::core::reflection;
    using namespace csyren::math;
    meta::MetaFactory<float>{}.primitiveType("float"_hs).archive(meta::scalarArchive<float>());
    meta::MetaFactory<int>{}.primitiveType("int"_hs).archive(meta::scalarArchive<int>());
    meta::MetaFactory<bool>{}.primitiveType("bool"_hs).archive(meta::scalarArchive<bool>());
    meta::MetaFactory<std::string>{}.primitiveType("string"_hs).archive(meta::scalarArchive<std::string>());
    meta::MetaFactory<Vector2>{}.primitiveType("Vector2"_hs).archive(vectorArchive<Vector2, 2>());
    meta::MetaFactory<Vector3>{}.primitiveType("Vector3"_hs).archive(vectorArchive<Vector3, 3>());
    meta::MetaFactory<Vector4>{}.primitiveType("Vector4"_hs).archive(vectorArchive<Vector4, 4>());
    meta::MetaFactory<Color>{}.primitiveType("Color"_hs).archive(vectorArchive<Color, 4>());
    meta::MetaFactory<Quaternion>{}.primitiveType("Quaternion"_hs).archive(vectorArchive<Quaternion, 4>());
}
