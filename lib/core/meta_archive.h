#pragma once
#include "meta.h"
#include <limits>
#include <cmath>

namespace csyren::core::reflection
{
    // Registration helpers generate operations; dispatch always uses MetaType.
    template<class T> MetaArchiveOps scalarArchive()
    {
        return {
            [](ArchiveWriter& w, SerializationContext&, const MetaType&, const void* p) -> bool {
                const T value = *static_cast<const T*>(p);
                if constexpr (std::is_enum_v<T>) return w.signedInteger(static_cast<int64_t>(value));
                else if constexpr (std::is_same_v<T, bool>) return w.boolean(value);
                else if constexpr (std::is_integral_v<T> && std::is_signed_v<T>) return w.signedInteger(value);
                else if constexpr (std::is_integral_v<T>) return w.unsignedInteger(value);
                else if constexpr (std::is_floating_point_v<T>) return w.floating(value);
                else return w.string(value);
            },
            [](ArchiveReader& r, SerializationContext&, const MetaType&, void* p) -> bool {
                if constexpr (std::is_enum_v<T>) {
                    using U = std::underlying_type_t<T>; int64_t value{};
                    if (!r.signedInteger(value) || value < (std::numeric_limits<U>::min)() || value > (std::numeric_limits<U>::max)()) return false;
                    *static_cast<T*>(p) = static_cast<T>(value);
                } else if constexpr (std::is_same_v<T, bool>) { bool value{}; if (!r.boolean(value)) return false; *static_cast<T*>(p) = value;
                } else if constexpr (std::is_integral_v<T> && std::is_signed_v<T>) { int64_t value{}; if (!r.signedInteger(value) || value < (std::numeric_limits<T>::min)() || value > (std::numeric_limits<T>::max)()) return false; *static_cast<T*>(p) = static_cast<T>(value);
                } else if constexpr (std::is_integral_v<T>) { uint64_t value{}; if (!r.unsignedInteger(value) || value > (std::numeric_limits<T>::max)()) return false; *static_cast<T*>(p) = static_cast<T>(value);
                } else if constexpr (std::is_floating_point_v<T>) { double value{}; if (!r.floating(value) || !std::isfinite(value) || value < -(std::numeric_limits<T>::max)() || value > (std::numeric_limits<T>::max)()) return false; *static_cast<T*>(p) = static_cast<T>(value);
                } else { std::string value; if (!r.string(value)) return false; *static_cast<T*>(p) = std::move(value); }
                return true;
            }
        };
    }
}
