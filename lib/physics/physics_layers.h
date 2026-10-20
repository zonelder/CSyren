#pragma once
// Collision layers are an engine mechanism; their names and the collision
// matrix belong to the project. The registry holds up to 32 named layers and
// a symmetric "collides" matrix. Bodies reference layers by name through
// CollisionLayer (RigidBody::layer, CharacterController::layer); queries use
// bit masks (bit i = layer index i). Whether a body is static or moving is
// decided by its BodyType, never by the layer.
//
// A project usually declares its layers as a reflected enum and hands it to
// the registry in a system's init(), before the first scene creates bodies:
//
//   CS_ENUM(Layer, uint8_t, Default, Player, Projectile)
//   layers.defineEnum<Layer>();
//   layers.setCollision(Layer::Player, Layer::Projectile, false);
//   filter.layerMask = PhysicsLayers::toMask(MaskOf<Layer>{ Layer::Default });
//
// Data-driven projects can load the same from JSON (additive):
//   { "layers": ["Default", "Player", "Projectile"],
//     "ignore": [["Player", "Projectile"], ["Projectile", "Projectile"]] }
#include <array>
#include <cstdint>
#include <initializer_list>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#include "core/reflected_enum.h"

namespace csyren::physics
{
	class PhysicsLayers
	{
	public:
		static constexpr uint32_t MaxLayers = 32;
		static constexpr uint32_t Default = 0;
		static constexpr uint32_t All = 0xFFFFFFFFu;

		PhysicsLayers() { reset(); s_active = this; }
		~PhysicsLayers() { if (s_active == this) s_active = nullptr; }
		PhysicsLayers(const PhysicsLayers&) = delete;
		PhysicsLayers& operator=(const PhysicsLayers&) = delete;

		// The registry the engine runs with (tools list layer names from it).
		static const PhysicsLayers* active() noexcept { return s_active; }

		// Removes every layer except "Default" and makes everything collide again.
		void reset()
		{
			_names.clear();
			_names.emplace_back("Default");
			_matrix.fill(All);
			++_revision;
		}

		// Returns the layer index; an existing name keeps its index.
		uint32_t define(std::string_view name)
		{
			if (const int existing = find(name); existing >= 0) return static_cast<uint32_t>(existing);
			if (_names.size() >= MaxLayers) throw std::length_error("PhysicsLayers: at most 32 layers are supported");
			_names.emplace_back(name);
			++_revision;
			return static_cast<uint32_t>(_names.size() - 1);
		}

		// -1 when the name is unknown.
		int find(std::string_view name) const noexcept
		{
			for (size_t i = 0; i < _names.size(); ++i)
				if (_names[i] == name) return static_cast<int>(i);
			return -1;
		}
		// Index of the name, or Default when unknown.
		uint32_t indexOf(std::string_view name) const noexcept
		{
			const int index = find(name);
			return index < 0 ? Default : static_cast<uint32_t>(index);
		}
		const std::string& name(uint32_t index) const noexcept
		{
			static const std::string invalid = "?";
			return index < _names.size() ? _names[index] : invalid;
		}
		uint32_t count() const noexcept { return static_cast<uint32_t>(_names.size()); }
		const std::vector<std::string>& names() const noexcept { return _names; }

		void setCollision(uint32_t a, uint32_t b, bool collide)
		{
			if (a >= MaxLayers || b >= MaxLayers) return;
			const uint32_t bitA = 1u << a, bitB = 1u << b;
			if (collide) { _matrix[a] |= bitB; _matrix[b] |= bitA; }
			else { _matrix[a] &= ~bitB; _matrix[b] &= ~bitA; }
			++_revision;
		}
		void setCollision(std::string_view a, std::string_view b, bool collide)
		{
			const int ia = find(a), ib = find(b);
			if (ia >= 0 && ib >= 0) setCollision(static_cast<uint32_t>(ia), static_cast<uint32_t>(ib), collide);
		}
		bool collides(uint32_t a, uint32_t b) const noexcept
		{
			return a < MaxLayers && b < MaxLayers && (_matrix[a] & (1u << b)) != 0;
		}
		// Layers a body on `layer` collides with, as a mask (useful for queries "what this body would hit").
		uint32_t collisionMask(uint32_t layer) const noexcept { return layer < MaxLayers ? _matrix[layer] : 0; }

		static constexpr uint32_t bit(uint32_t index) noexcept { return index < MaxLayers ? 1u << index : 0u; }
		uint32_t mask(std::initializer_list<std::string_view> layers) const noexcept
		{
			uint32_t result = 0;
			for (const auto& layer : layers) if (const int index = find(layer); index >= 0) result |= bit(static_cast<uint32_t>(index));
			return result;
		}
		uint32_t maskExcept(std::initializer_list<std::string_view> layers) const noexcept
		{
			return All & ~mask(layers);
		}

		// ---- reflected enums as layer sets ------------------------------------
		// Defines every enumerator of E (by its name, in declaration order) and
		// remembers the resulting indices, so index/bit/toMask need no registry.
		template<core::enums::ReflectedEnum E> void defineEnum()
		{
			auto& table = enumTable<E>();
			for (auto value : core::enums::enumValues<E>())
				table[core::enums::enumIndex(value)] = define(core::enums::enumName(value));
		}
		template<core::enums::ReflectedEnum E> static uint32_t index(E value) noexcept
		{
			const auto i = core::enums::enumIndex(value);
			return i < 64 ? enumTable<E>()[i] : Default;
		}
		template<core::enums::ReflectedEnum E> static uint32_t bit(E value) noexcept { return bit(index(value)); }
		template<core::enums::ReflectedEnum E> static uint32_t toMask(core::MaskOf<E> layers) noexcept
		{
			uint32_t result = 0;
			layers.forEach([&](E value) { result |= bit(value); });
			return result;
		}
		template<core::enums::ReflectedEnum E> void setCollision(E a, E b, bool collide)
		{
			setCollision(index(a), index(b), collide);
		}
		template<core::enums::ReflectedEnum E> bool collides(E a, E b) const noexcept { return collides(index(a), index(b)); }

		// Loads layers + ignored pairs from a JSON asset (see the header comment). Additive.
		bool load(const std::string& assetPath);

		// Changes every time layers or the matrix change (bodies can refresh cached indices).
		uint64_t revision() const noexcept { return _revision; }
	private:
		template<class E> static std::array<uint32_t, 64>& enumTable() noexcept
		{
			static std::array<uint32_t, 64> table{}; // unknown -> Default (0)
			return table;
		}
		inline static const PhysicsLayers* s_active = nullptr;
		std::vector<std::string> _names;
		std::array<uint32_t, MaxLayers> _matrix{};
		uint64_t _revision = 0;
	};

	// Reference to a collision layer by name. Serialized as the name; the
	// inspector offers the layers registered in PhysicsLayers. A name that is
	// not registered falls back to "Default" at runtime but is kept on save.
	struct CollisionLayer
	{
		std::string name = "Default";

		CollisionLayer() = default;
		CollisionLayer(std::string layer) : name(std::move(layer)) {}
		CollisionLayer(const char* layer) : name(layer) {}
		template<core::enums::ReflectedEnum E> static CollisionLayer from(E value) { return CollisionLayer(std::string(core::enums::enumName(value))); }
		bool operator==(const CollisionLayer&) const = default;

		static void describe(); // reflection (physics_layers.cpp)
	};
}
