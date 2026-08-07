#pragma once
#include <cstdint>
#include <xhash>
#include <string_view>

namespace csyren::cstdmf
{
    struct LiteralID {
        uint64_t id;
        std::string_view str;
        bool operator==(const LiteralID& other) const { return id == other.id; }
        bool operator!=(const LiteralID& other) const { return id != other.id; }
    };

    struct LiteralIDHash {
        size_t operator()(const LiteralID& lid) const {
            return std::hash<uint64_t>{}(lid.id);
        }
    };

    constexpr LiteralID fnv1a_32(const char* str, size_t len) {
        uint64_t hash = 0x811c9dc5u;
        for (size_t i = 0; i < len; ++i) {
            hash ^= static_cast<uint64_t>(str[i]);
            hash *= 0x01000193u;
        }
        return LiteralID{ hash,str };
    }
}

namespace literal {
    constexpr csyren::cstdmf::LiteralID operator""_hs(const char* str, size_t len) noexcept {
        return csyren::cstdmf::fnv1a_32(str, len);
    }
}