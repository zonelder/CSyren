#include "pch.h"
#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>
#include <gtest/gtest.h>
#include "cstdmf/static_vector.h"
#include "cstdmf/page_view.h"
#include "cstdmf/sparse_set.h"
#include "cstdmf/string_utils.h"
#include "core/scene.h"

using namespace csyren::cstdmf;
using namespace csyren::core;

namespace {
struct Tracked {
    static inline int live = 0;
    Tracked() { ++live; }
    Tracked(const Tracked&) { ++live; }
    Tracked(Tracked&&) noexcept { ++live; }
    ~Tracked() { --live; }
};
struct Throws {
    explicit Throws(bool fail = false) { if (fail) throw std::runtime_error("test"); }
};
struct AuditA { int value = 0; };
struct AuditB { int value = 0; };
struct AuditC { int value = 0; };
void registerAudit() {
    REGISTER_COMPONENT(AuditA);
    REGISTER_COMPONENT(AuditB);
    REGISTER_COMPONENT(AuditC);
}
}

TEST(Audit, FixedClearRestoresCapacity) {
    FixedSparseSet<int, 2> s;
    s.emplace(1); s.emplace(2); s.clear();
    EXPECT_NE(s.emplace(3), decltype(s)::invalidID);
    EXPECT_NE(s.emplace(4), decltype(s)::invalidID);
    EXPECT_EQ(s.size(), 2u);
}
TEST(Audit, FixedEraseDestroysMovedSource) {
    Tracked::live = 0;
    { FixedSparseSet<Tracked, 2> s;
      auto id = s.emplace(); s.emplace(); s.erase(id);
      EXPECT_EQ(Tracked::live, 1); }
    EXPECT_EQ(Tracked::live, 0);
}
TEST(Audit, FixedThrowLeavesIDAvailable) {
    FixedSparseSet<Throws, 1> s;
    EXPECT_THROW(s.emplace(true), std::runtime_error);
    EXPECT_FALSE(s.contains(0));
    EXPECT_NE(s.emplace(false), decltype(s)::invalidID);
}
TEST(Audit, StaticCopyConstructsElements) {
    Tracked::live = 0;
    { StaticVector<Tracked, 2> a; a.emplace_back();
      { auto b = a; EXPECT_EQ(Tracked::live, 2); }
      EXPECT_EQ(Tracked::live, 1); }
    EXPECT_EQ(Tracked::live, 0);
}
TEST(Audit, StaticMoveDestroysSourceElements) {
    Tracked::live = 0;
    { StaticVector<Tracked, 2> a; a.emplace_back();
      StaticVector<Tracked, 2> b(std::move(a)); }
    EXPECT_EQ(Tracked::live, 0);
}
TEST(Audit, SparseThrowDoesNotPublishKey) {
    SparseSet<Throws> s;
    EXPECT_THROW(s.emplace(7, true), std::runtime_error);
    EXPECT_FALSE(s.contains(7));
    EXPECT_EQ(s.key_begin(), s.key_end());
    EXPECT_NO_THROW(s.emplace(7, false));
}
TEST(Audit, PageReserveIsUsedByEmplace) {
    PageView<int, 2> s; s.reserve(4);
    for (int i = 0; i < 4; ++i) s.emplace(i);
    EXPECT_EQ(s.capacity(), 4u);
}
TEST(Audit, PageClearAllowsReuse) {
    PageView<int, 2> s; s.emplace(1); s.clear();
    auto id = s.emplace(2);
    ASSERT_NE(s.get(id), nullptr);
    EXPECT_EQ(*s.get(id), 2);
    EXPECT_EQ(s.size(), 1u);
}
TEST(Audit, PagePostIncrementReturnsValue) {
    using It = PageView<int>::iterator;
    EXPECT_FALSE((std::is_reference_v<decltype(std::declval<It&>()++)>));
}
TEST(Audit, InvalidParentFallsBackConsistently) {
    EntityManager em; em.init();
    auto id = em.createEntity("child", 999);
    EXPECT_EQ(em.tryGet(id)->parent, EntityManager::ROOT_PARENT);
    em.queueDestroy(id);
    em.finalizeDestroy(id);
    EXPECT_TRUE(em.tryGet(EntityManager::ROOT_PARENT)->children.empty());
}
TEST(Audit, DestroyEntityRemovesEveryPoolEntry) {
    registerAudit();
    auto bus = std::make_unique<EventBus2>(); Scene s(bus.get()); s.init();
    auto id = s.createEntity();
    auto a = s.addComponent<AuditA>(id);
    auto b = s.addComponent<AuditB>(id);
    auto c = s.addComponent<AuditC>(id);
    s.destroyEntity(id); s.flush();
    EXPECT_FALSE(bool(a)); EXPECT_FALSE(bool(b)); EXPECT_FALSE(bool(c));
    EXPECT_FALSE(s.view<AuditB>().contains(id));
}
TEST(Audit, ComponentRefDoesNotRetargetReusedEntityID) {
    registerAudit();
    auto bus = std::make_unique<EventBus2>(); Scene s(bus.get()); s.init();
    auto id = s.createEntity(); auto old = s.addComponent<AuditA>(id);
    s.destroyEntity(id); s.flush();
    auto next = s.createEntity(); s.addComponent<AuditA>(next);
    EXPECT_FALSE(bool(old));
}
TEST(Audit, DestroyEventMustNotResolveReplacementComponent) {
    registerAudit();
    auto bus = std::make_unique<EventBus2>(); Scene s(bus.get()); s.init();
    bool replacementSeen = false; int delivered = 0;
    bus->subscribe<ComponentDestroyEvent<AuditA>>([&](const auto& e) {
        ++delivered; replacementSeen = e.comp && e.comp->value == 99;
    });
    auto id = s.createEntity(); s.addComponent<AuditA>(id, 1);
    s.removeComponent<AuditA>(id); s.flush(); s.addComponent<AuditA>(id, 99);
    bus->commit_batch();
    EXPECT_EQ(delivered, 1); EXPECT_FALSE(replacementSeen);
}
