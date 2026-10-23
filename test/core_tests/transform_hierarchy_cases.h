#pragma once
#include "core/transform_hierarchy.h"
#include <thread>
#include <chrono>

TEST_F(SceneWorkflow, PhysicsPropagationUpdatesTransformlessBranchesInOnePass)
{
    SceneInstance instance; auto& scene=instance.scene();
    auto parent=scene.createEntity(), gap=scene.createEntity(parent), child=scene.createEntity(gap);
    scene.addComponent<Transform>(parent); scene.addComponent<Transform>(child);
    WorldTransforms worlds; worlds.update(scene);
    int visited=0;
    worlds.propagate(scene,[&](auto id,const auto& parentMatrix,const auto& parentRotation) {
        ++visited;
        if(id==parent) ASSERT_TRUE(setWorldPose(scene,id,{10,0,0},{},parentMatrix,parentRotation));
        if(id==child) {
            EXPECT_FLOAT_EQ(parentMatrix.translation().x,10);
            ASSERT_TRUE(setWorldPose(scene,id,{12,0,0},{},parentMatrix,parentRotation));
        }
    });
    EXPECT_EQ(visited,4);
    EXPECT_FLOAT_EQ(scene.getComponent<Transform>(child)->position.x,2);
    EXPECT_FLOAT_EQ(worlds.matrix(child).translation().x,12);
    EXPECT_FALSE(worlds.update(scene)); // propagation already stamped the final revisions
}

TEST_F(SceneWorkflow, UnchangedWorldPoseDoesNotInvalidateOrPublishPatch)
{
    SceneInstance instance; auto& scene=instance.scene();
    auto id=scene.createEntity(); scene.addComponent<Transform>(id);
    int events=0;
    auto subscription = instance.context().bus.createEvent<ComponentPatchEvent<Transform>>().subscribe([&](const auto&) { ++events; });
    const auto revision=scene.componentRevision<Transform>();
    for(int i=0;i<100;++i) ASSERT_TRUE(setWorldPose(scene,id,{0,0,0},{}));
    EXPECT_EQ(scene.componentRevision<Transform>(),revision);
    instance.settle(); EXPECT_EQ(events,0);
    ASSERT_TRUE(setWorldPose(scene,id,{1,0,0},{}));
    instance.settle(); EXPECT_EQ(events,1);
}

TEST_F(SceneWorkflow, RenderViewsReadOnePreparedWorldSnapshot)
{
    core::details::ServiceRegistry::create<Time>();
    render::Renderer renderer; // no targets are needed to prepare scene world data
    SceneInstance instance; auto& scene=instance.scene();
    auto parent=scene.createEntity(), child=scene.createEntity(parent);
    Transform p; p.position=math::Vector3{10,0,0};
    Transform c; c.position=math::Vector3{2,0,0};
    scene.addComponent<Transform>(parent,p); scene.addComponent<Transform>(child,c);
    const auto cameras=core::details::Param<render::Renderer::CameraQuery>::make(&scene);
    renderer.prepareCameraViews(cameras,0,800,600);
    const auto* matrix=&renderer.worldTransforms().matrix(child);
    EXPECT_FLOAT_EQ(matrix->translation().x,12);
    scene.patch<Transform>(parent,[](auto& tr) { tr.position.x=20; });
    // All view recordings read the prepared frame, even if an edit follows it.
    EXPECT_FLOAT_EQ(renderer.worldTransforms().matrix(child).translation().x,12);
    renderer.prepareCameraViews(cameras,0,800,600);
    EXPECT_EQ(matrix,&renderer.worldTransforms().matrix(child));
    EXPECT_FLOAT_EQ(matrix->translation().x,22);
}

TEST_F(SceneWorkflow, HierarchyComposesLocalMatricesThroughNodesWithoutTransforms)
{
    SceneInstance instance; auto& scene=instance.scene();
    auto parent=scene.createEntity(), gap=scene.createEntity(parent), child=scene.createEntity(gap);
    Transform p; p.position=math::Vector3{4,5,6}; p.rotation=math::Quaternion::euler(10,30,20); p.scale=math::Vector3{2,3,4};
    Transform c; c.position=math::Vector3{1,2,3}; c.rotation=math::Quaternion::euler(20,40,10); c.scale=math::Vector3{.5f,2,1};
    scene.addComponent<Transform>(parent,p); scene.addComponent<Transform>(child,c);
    WorldTransforms worlds;
    ASSERT_TRUE(worlds.update(scene));
    EXPECT_TRUE(worlds.matrix(child)==c.localMatrix()*p.localMatrix());
    EXPECT_TRUE(worldMatrix(scene,child)==worlds.matrix(child));
    EXPECT_FALSE(worlds.update(scene));
    scene.patch<Transform>(parent,[](auto& tr) { tr.position.x+=10; });
    ASSERT_TRUE(worlds.update(scene));
    EXPECT_TRUE(worlds.matrix(child)==worldMatrix(scene,child));
    scene.removeComponent<Transform>(parent); scene.flush();
    ASSERT_TRUE(worlds.update(scene)); EXPECT_TRUE(worlds.matrix(child)==c.localMatrix());
    scene.addComponent<Transform>(gap,p);
    ASSERT_TRUE(worlds.update(scene)); EXPECT_TRUE(worlds.matrix(child)==c.localMatrix()*p.localMatrix());
    const auto revision=scene.hierarchyRevision();
    EXPECT_FALSE(scene.setParent(gap,child)); EXPECT_EQ(scene.hierarchyRevision(),revision);
    EXPECT_TRUE(scene.setParent(child,EntityManager::ROOT_PARENT));
    ASSERT_TRUE(worlds.update(scene)); EXPECT_TRUE(worlds.matrix(child)==c.localMatrix());
    EXPECT_TRUE(scene.getComponent<Transform>(child)->localMatrix()==c.localMatrix());
    SceneInstance other; ASSERT_TRUE(worlds.update(other.scene()));
}

TEST_F(SceneWorkflow, TypedQueriesMatchSceneTransformHelpers)
{
    using namespace csyren::core;
    SceneInstance instance; auto& scene=instance.scene();
    auto parent=scene.createEntity(), gap=scene.createEntity(parent), child=scene.createEntity(gap);
    Transform p; p.position=math::Vector3{4,5,6}; p.rotation=math::Quaternion::euler(10,30,20); p.scale=math::Vector3{2,3,4};
    Transform c; c.position=math::Vector3{1,2,3}; c.rotation=math::Quaternion::euler(20,40,10);
    scene.addComponent<Transform>(parent,p); scene.addComponent<Transform>(child,c);
    // Typed system parameters, built the way TypedSystem::execute builds them.
    auto read=details::Param<Select<Transform>>::make(&scene);
    auto write=details::Param<Update<Transform>>::make(&scene);
    EXPECT_TRUE(worldMatrix(read,child)==worldMatrix(scene,child));
    EXPECT_NEAR((worldRotation(read,child)*math::Vector3::forward-worldRotation(scene,child)*math::Vector3::forward).magnitude(),0,1e-5f);
    EXPECT_EQ(parentOf(read,child),gap);
    WorldTransforms fromQuery, fromScene;
    ASSERT_TRUE(fromQuery.update(read)); ASSERT_TRUE(fromScene.update(scene));
    EXPECT_TRUE(fromQuery.matrix(child)==fromScene.matrix(child));
    EXPECT_FALSE(fromQuery.update(read));
    const math::Vector3 target{17,-4,9}; const auto rotation=math::Quaternion::euler(45,20,15);
    ASSERT_TRUE(setWorldPose(write,child,target,rotation));
    EXPECT_NEAR((worldMatrix(scene,child).translation()-target).magnitude(),0,1e-4f);
    EXPECT_TRUE(fromQuery.update(read)); // the patch changed the Transform revision
    EXPECT_TRUE(fromQuery.matrix(child)==worldMatrix(scene,child));
}

TEST_F(SceneWorkflow, WorldPoseConversionHandlesShearedParentsAndSingularScale)
{
    SceneInstance instance; auto& scene=instance.scene();
    auto root=scene.createEntity(), parent=scene.createEntity(root), child=scene.createEntity(parent);
    Transform a; a.position=math::Vector3{3,7,1}; a.scale=math::Vector3{2,3,4}; a.rotation=math::Quaternion::euler(10,20,30);
    Transform b; b.rotation=math::Quaternion::euler(30,50,20);
    scene.addComponent<Transform>(root,a); scene.addComponent<Transform>(parent,b); scene.addComponent<Transform>(child);
    const math::Vector3 target{17,-4,9}; const auto rotation=math::Quaternion::euler(45,20,15);
    ASSERT_TRUE(setWorldPose(scene,child,target,rotation));
    EXPECT_NEAR((worldMatrix(scene,child).translation()-target).magnitude(),0,1e-4f);
    EXPECT_NEAR((worldRotation(scene,child)*math::Vector3::forward-rotation*math::Vector3::forward).magnitude(),0,1e-4f);
    const auto before=*scene.getComponent<Transform>(child);
    scene.patch<Transform>(root,[](auto& tr) { tr.scale.x=0; });
    EXPECT_FALSE(setWorldPose(scene,child,{1,2,3},rotation));
    EXPECT_TRUE(before.localMatrix()==scene.getComponent<Transform>(child)->localMatrix());
}

TEST_F(SceneWorkflow, ParentedGizmoMovesInWorldAxesAndUndoInvalidatesWorldSnapshot)
{
    MemoryStorage storage; SceneSession session(storage); auto& scene=session.editorScene();
    auto parent=scene.createEntity(), child=scene.createEntity(parent);
    Transform p; p.rotation=math::Quaternion::euler(0,0,90); p.scale=math::Vector3{2,3,1};
    scene.addComponent<Transform>(parent,p); scene.addComponent<Transform>(child);
    session.keyFor(child);
    TransformGesture gesture; WorldTransforms worlds; worlds.update(scene);
    ASSERT_TRUE(gesture.begin(session,child)); ASSERT_TRUE(gesture.move(session,{3,0,0})); gesture.finish(session);
    ASSERT_TRUE(worlds.update(scene)); EXPECT_NEAR((worlds.matrix(child).translation()-math::Vector3{3,0,0}).magnitude(),0,1e-4f);
    run(session,SceneOperation::Undo); ASSERT_TRUE(worlds.update(scene)); EXPECT_NEAR(worlds.matrix(child).translation().magnitude(),0,1e-4f);
    run(session,SceneOperation::Redo); ASSERT_TRUE(worlds.update(scene)); EXPECT_NEAR(worlds.matrix(child).translation().x,3,1e-4f);
    ASSERT_TRUE(gesture.begin(session,child,GizmoMode::Rotate));
    ASSERT_TRUE(gesture.rotate(session,{1,0,0},.5f)); gesture.finish(session);
    const auto expected=math::Quaternion(DirectX::XMQuaternionMultiply(p.rotation,DirectX::XMQuaternionRotationAxis(math::Vector3{1,0,0},.5f)));
    EXPECT_NEAR((worldRotation(scene,child)*math::Vector3::forward-expected*math::Vector3::forward).magnitude(),0,1e-4f);
}

TEST_F(SceneWorkflow, HierarchySurvivesArchivePlayStopAndBatchAppendInvalidatesSnapshots)
{
    MemoryStorage storage; SceneSession session(storage); auto& scene=session.editorScene();
    auto parent=scene.createEntity("parent"), child=scene.createEntity("child",parent);
    Transform p; p.position=math::Vector3{10,0,0}; Transform c; c.position=math::Vector3{2,0,0};
    scene.addComponent<Transform>(parent,p); scene.addComponent<Transform>(child,c);
    WorldTransforms worlds; worlds.update(scene);
    auto batch=scene.stageEntities(1); auto added=batch.createEntity("batch"); batch.addComponent<Transform>(added);
    ASSERT_TRUE(scene.append(std::move(batch))); ASSERT_TRUE(worlds.update(scene));
    run(session,SceneOperation::SaveAs,"hierarchy"); run(session,SceneOperation::Play);
    auto& runtime=session.activeContext().scene;
    for (const auto& entity:runtime.entities()) if(entity.name=="child") EXPECT_NEAR(worldMatrix(runtime,entity.id).translation().x,12,1e-4f);
    run(session,SceneOperation::Stop); run(session,SceneOperation::Open,"hierarchy",true);
    for (const auto& entity:session.editorScene().entities()) if(entity.name=="child") EXPECT_NEAR(worldMatrix(session.editorScene(),entity.id).translation().x,12,1e-4f);
}

TEST_F(SceneWorkflow, PhysicsRoundTripsParentAndChildBodiesInWorldSpace)
{
    auto& engine=core::details::ServiceRegistry::create<physics::PhysicsEngine>();
    auto& time=core::details::ServiceRegistry::create<Time>();
    SceneInstance instance; auto& scene=instance.scene();
    auto parent=scene.createEntity(), child=scene.createEntity(parent);
    Transform p; p.position=math::Vector3{20,30,40}; p.rotation=math::Quaternion::euler(0,35,0);
    Transform c; c.position=math::Vector3{4,2,1};
    scene.addComponent<Transform>(parent,p); scene.addComponent<Transform>(child,c);
    for (auto id:{child,parent}) {
        scene.addComponent<physics::BoxCollider>(id);
        physics::RigidBody rb; rb.type=physics::BodyType::Dynamic;
        if (id==parent) rb.linearVelocity=math::Vector3{1,0,0};
        scene.addComponent<physics::RigidBody>(id,rb);
    }
    instance.settle();
    SystemManager manager; manager.phase({"simulation"}); manager.phase({"events",{"simulation"}});
    manager.add("PhysicsSystem"); manager.add("PhysicsSyncSystem"); manager.add("SceneEventsSystem");
    manager.bind(instance.context(),instance.context(),true); manager.init();
    engine.setGravity({0,0,0});
    auto expectedChild=worldMatrix(scene,child).translation();
    core::details::TimeHandler clock;
    const auto step=[&] { clock.update(time); manager.execute(); };
    std::this_thread::sleep_for(std::chrono::milliseconds(10)); step();
    EXPECT_GT(scene.getComponent<Transform>(parent)->position.x,20);
    EXPECT_NEAR(scene.getComponent<Transform>(parent)->position.x,20+std::clamp(time.deltaTime(),0.f,.1f),1e-4f);
    EXPECT_NEAR((worldMatrix(scene,child).translation()-expectedChild).magnitude(),0,1e-4f);
    scene.patch<Transform>(parent,[](auto& tr) { tr.position.x+=7; }); step();
    expectedChild.x+=7;
    EXPECT_NEAR((worldMatrix(scene,child).translation()-expectedChild).magnitude(),0,1e-4f);
    // PhysicsSyncSystem rebuilds the patched body at the end of the frame; the
    // next step simulates the rebuilt body from the inherited pose.
    scene.patch<physics::BoxCollider>(child,[](auto& box) { box.size=math::Vector3{2,2,2}; }); instance.settle(); step(); step();
    EXPECT_NEAR((worldMatrix(scene,child).translation()-expectedChild).magnitude(),0,1e-4f);
    manager.unbind(); manager.shutdown(); core::details::ServiceRegistry::shutdownAll();
}

TEST_F(SceneWorkflow, SceneCameraFrameUsesInheritedWorldPose)
{
    SceneInstance instance; auto& scene=instance.scene();
    auto parent=scene.createEntity(), camera=scene.createEntity(parent);
    Transform p; p.position=math::Vector3{5,6,7}; p.rotation=math::Quaternion::euler(0,60,0);
    Transform c; c.position=math::Vector3{0,0,-3};
    scene.addComponent<Transform>(parent,p); scene.addComponent<Transform>(camera,c);
    const auto world=worldMatrix(scene,camera);
    const auto frame=render::details::cameraFrame(Camera{},world,800,600,0,render::RenderEnvironment{});
    const auto expected=world.translation();
    EXPECT_NEAR(frame.cameraPosition.x,expected.x,1e-4f);
    EXPECT_NEAR(frame.cameraPosition.y,expected.y,1e-4f);
    EXPECT_NEAR(frame.cameraPosition.z,expected.z,1e-4f);
    EXPECT_TRUE(math::Matrix4x4(frame.viewMatrix)*world==math::Matrix4x4::identity);
}
