#include "pch.h"
#include "core/scene.h"
#include "cstdmf/tree_algorithms.h"

using namespace csyren;
using namespace csyren::core;

TEST(TreeAlgorithms, ProjectionsSupportUnrelatedNodeTypesAndRejectCycles) {
    struct Node { int owner = -1; std::vector<int> links; };
    std::vector<Node> nodes{{-1,{1,2}}, {0,{3}}, {0,{}}, {1,{}}};
    auto lookup = [&](int id) -> Node* { return id >= 0 && id < int(nodes.size()) ? &nodes[id] : nullptr; };
    auto children = [&](int id) -> const auto& { return nodes[id].links; };
    std::vector<int> before, after;
    cstdmf::tree::preorder(0,children,[&](int id) { before.push_back(id); });
    EXPECT_EQ(before,(std::vector<int>{0,1,3,2}));
    EXPECT_FALSE(cstdmf::tree::reparent(1,3,-1,lookup,&Node::owner,&Node::links));
    EXPECT_FALSE(cstdmf::tree::reparent(1,17,-1,lookup,&Node::owner,&Node::links));
    EXPECT_TRUE(cstdmf::tree::reparent(3,2,-1,lookup,&Node::owner,&Node::links));
    EXPECT_TRUE(nodes[1].links.empty()); EXPECT_EQ(nodes[3].owner,2);
    EXPECT_TRUE(cstdmf::tree::reparent(3,2,-1,lookup,&Node::owner,&Node::links));
    EXPECT_EQ(nodes[2].links.size(),1u);
    cstdmf::tree::postorder(0,children,[&](int id) { after.push_back(id); });
    for (int id = 1; id != 4; ++id)
        EXPECT_LT(std::find(after.begin(),after.end(),id),std::find(after.begin(),after.end(),nodes[id].owner));
}

TEST(TreeAlgorithms, DeepBranchesDoNotUseTheCallStack) {
    constexpr int count = 20000;
    std::vector<std::vector<int>> nodes(count);
    for (int i=0;i<count-1;++i) nodes[i].push_back(i+1);
    int visited=0;
    cstdmf::tree::postorder(0,[&](int id)->const auto& { return nodes[id]; },[&](int id) { EXPECT_EQ(id,count-1-visited++); });
    EXPECT_EQ(visited,count);
}

namespace {
struct PatchedValue { int value=0; };
class ScenePatch : public ::testing::Test {
protected:
    EventBus bus; Scene scene{&bus};
    static void SetUpTestCase() { REGISTER_COMPONENT(PatchedValue); }
    void SetUp() override { scene.init(); }
};
TEST_F(ScenePatch, NotifiesTracksRevisionAndRejectsForeignOrStaleRefs) {
    const auto id=scene.createEntity();
    auto component=scene.addComponent<PatchedValue>(id);
    int events=0;
    auto subscription = bus.createEvent<ComponentPatchEvent<PatchedValue>>().subscribe([&](const auto& e) { EXPECT_EQ(e.comp,component); ++events; });
    const auto revision=scene.componentRevision<PatchedValue>();
    EXPECT_TRUE(scene.patch(component,[](auto& c) { c.value=73; }));
    EXPECT_EQ(events,0); EXPECT_EQ(component->value,73);
    EXPECT_GT(scene.componentRevision<PatchedValue>(),revision);
    bus.dispatch(); EXPECT_EQ(events,1);
    auto otherBus=std::make_unique<EventBus>(); Scene other(otherBus.get()); other.init();
    const auto otherId=other.createEntity(); other.addComponent<PatchedValue>(otherId);
    EXPECT_FALSE(other.patch(component));
    scene.removeComponent<PatchedValue>(id); scene.flush();
    scene.addComponent<PatchedValue>(id);
    EXPECT_FALSE(scene.patch(component)); EXPECT_EQ(events,1);
    EXPECT_TRUE(scene.patch(id,reflection::ComponentFamily::getID<PatchedValue>()));
}
TEST_F(ScenePatch, ThrowingCallbackStillInvalidatesPartiallyChangedData) {
    const auto id=scene.createEntity(); scene.addComponent<PatchedValue>(id);
    const auto revision=scene.componentRevision<PatchedValue>();
    EXPECT_THROW(scene.patch<PatchedValue>(id,[](auto& c) { c.value=9; throw std::runtime_error("edit"); }),std::runtime_error);
    EXPECT_EQ(scene.getComponent<PatchedValue>(id)->value,9);
    EXPECT_GT(scene.componentRevision<PatchedValue>(),revision);
}
TEST_F(ScenePatch, HierarchyRevisionAndEventDescribeOnlySuccessfulChanges) {
    auto a=scene.createEntity(), b=scene.createEntity(), child=scene.createEntity(a);
    int events=0;
    auto subscription = bus.createEvent<EntityReparentEvent>().subscribe([&](const auto& e) {
        EXPECT_EQ(e.id,child); EXPECT_EQ(e.previous,a); EXPECT_EQ(e.parent,b); ++events;
    });
    auto revision=scene.hierarchyRevision();
    EXPECT_FALSE(scene.setParent(a,child)); EXPECT_EQ(scene.hierarchyRevision(),revision);
    EXPECT_TRUE(scene.setParent(child,a)); EXPECT_EQ(scene.hierarchyRevision(),revision);
    EXPECT_TRUE(scene.setParent(child,b)); EXPECT_GT(scene.hierarchyRevision(),revision);
    bus.dispatch(); EXPECT_EQ(events,1);
    revision=scene.hierarchyRevision(); scene.destroyEntity(b); scene.flush();
    EXPECT_GT(scene.hierarchyRevision(),revision); EXPECT_FALSE(scene.entities().contains(child));
    scene.destroyEntity(EntityManager::ROOT_PARENT); scene.flush();
    EXPECT_TRUE(scene.entities().contains(EntityManager::ROOT_PARENT));
}
}
