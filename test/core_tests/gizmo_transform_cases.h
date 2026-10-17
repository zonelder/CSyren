#pragma once
#include <limits>

TEST_F(SceneWorkflow, RotateGizmoUsesWorldAxisAndOneUndo)
{
    using namespace DirectX;
    MemoryStorage storage; SceneSession session(storage);
    run(session, SceneOperation::CreateEntity);
    const auto id = session.takeCreatedEntity();
    auto transform = session.editorScene().getComponent<Transform>(id);
    transform->position = math::Vector3{ 4,5,6 };
    transform->scale = math::Vector3{ 2,3,4 };
    transform->rotation = XMQuaternionRotationAxis(XMVectorSet(1,0,0,0), .4f);
    const auto initial = *transform;
    run(session, SceneOperation::SaveAs, "rotate gizmo");
    TransformGesture gesture;
    ASSERT_TRUE(gesture.begin(session,id,GizmoMode::Rotate));
    ASSERT_TRUE(gesture.rotate(session,{0,1,0},.2f));
    ASSERT_TRUE(gesture.rotate(session,{0,1,0},.7f));
    ASSERT_TRUE(gesture.rotate(session,{0,1,0},XM_PIDIV2));
    gesture.finish(session);
    const math::Vector3 expected(XMVector3Rotate(XMVector3Rotate(XMVectorSet(0,0,1,0),initial.rotation),
        XMQuaternionRotationAxis(XMVectorSet(0,1,0,0),XM_PIDIV2)));
    const math::Vector3 actual(XMVector3Rotate(XMVectorSet(0,0,1,0),transform->rotation));
    EXPECT_NEAR(actual.x,expected.x,1e-5f); EXPECT_NEAR(actual.y,expected.y,1e-5f); EXPECT_NEAR(actual.z,expected.z,1e-5f);
    EXPECT_NEAR(transform->rotation.magnitude(),1.f,1e-5f);
    EXPECT_TRUE(transform->position.exactEqual(initial.position));
    EXPECT_TRUE(transform->scale.exactEqual(initial.scale));
    const auto changed = transform->rotation;
    run(session,SceneOperation::Undo);
    EXPECT_TRUE(transform->rotation.exactEqual(initial.rotation)); EXPECT_FALSE(session.dirty());
    run(session,SceneOperation::Redo); EXPECT_TRUE(transform->rotation.exactEqual(changed));
    run(session,SceneOperation::Save);
    ASSERT_TRUE(gesture.begin(session,id,GizmoMode::Rotate));
    ASSERT_TRUE(gesture.rotate(session,{1,0,0},1.f)); gesture.cancel(session);
    EXPECT_TRUE(transform->rotation.exactEqual(changed)); EXPECT_FALSE(session.dirty());
}

TEST_F(SceneWorkflow, ScaleGizmoPreservesMirrorsAndOtherTransformFields)
{
    MemoryStorage storage; SceneSession session(storage);
    run(session,SceneOperation::CreateEntity);
    const auto id = session.takeCreatedEntity();
    auto transform = session.editorScene().getComponent<Transform>(id);
    transform->position = math::Vector3{ 3,2,1 };
    transform->rotation = math::Quaternion::angleAxis(.7f,{0,0,1});
    transform->scale = math::Vector3{-2,3,.5f};
    const auto initial = *transform;
    run(session,SceneOperation::SaveAs,"scale gizmo");
    TransformGesture gesture;
    ASSERT_TRUE(gesture.begin(session,id,GizmoMode::Scale));
    ASSERT_TRUE(gesture.scale(session,0,1.5f)); ASSERT_TRUE(gesture.scale(session,0,2.f));
    gesture.finish(session);
    EXPECT_TRUE(transform->scale.exactEqual(math::Vector3{-4,3,.5f}));
    EXPECT_TRUE(transform->position.exactEqual(initial.position));
    EXPECT_TRUE(transform->rotation.exactEqual(initial.rotation));
    run(session,SceneOperation::Undo); EXPECT_TRUE(transform->scale.exactEqual(initial.scale)); EXPECT_FALSE(session.dirty());
    run(session,SceneOperation::Redo); EXPECT_FLOAT_EQ(transform->scale.x,-4);
    ASSERT_TRUE(gesture.begin(session,id,GizmoMode::Scale));
    ASSERT_TRUE(gesture.scale(session,3,.5f)); gesture.finish(session);
    EXPECT_TRUE(transform->scale.exactEqual(math::Vector3{-2,1.5f,.25f}));
    run(session,SceneOperation::Undo); EXPECT_TRUE(transform->scale.exactEqual(math::Vector3{-4,3,.5f}));
    run(session,SceneOperation::Save);
    ASSERT_TRUE(gesture.begin(session,id,GizmoMode::Scale));
    ASSERT_TRUE(gesture.scale(session,3,2)); gesture.cancel(session);
    EXPECT_TRUE(transform->scale.exactEqual(math::Vector3{-4,3,.5f})); EXPECT_FALSE(session.dirty());
}

TEST_F(SceneWorkflow, TransformGizmoRejectsInvalidInputAndClampsSingularScale)
{
    MemoryStorage storage; SceneSession session(storage);
    run(session,SceneOperation::CreateEntity); const auto id = session.takeCreatedEntity();
    run(session,SceneOperation::SaveAs,"invalid gizmo");
    TransformGesture gesture;
    ASSERT_TRUE(gesture.begin(session,id,GizmoMode::Rotate));
    EXPECT_FALSE(gesture.rotate(session,{0,0,0},1));
    EXPECT_FALSE(gesture.rotate(session,{0,0,1},std::numeric_limits<float>::infinity()));
    EXPECT_FALSE(gesture.scale(session,0,2));
    ASSERT_TRUE(gesture.rotate(session,{0,0,1},0)); gesture.finish(session);
    EXPECT_FALSE(session.dirty());
    ASSERT_TRUE(gesture.begin(session,id,GizmoMode::Scale));
    EXPECT_FALSE(gesture.scale(session,-1,2)); EXPECT_FALSE(gesture.scale(session,4,2));
    EXPECT_FALSE(gesture.scale(session,0,0)); EXPECT_FALSE(gesture.scale(session,0,-1));
    EXPECT_FALSE(gesture.scale(session,0,std::numeric_limits<float>::quiet_NaN()));
    ASSERT_TRUE(gesture.scale(session,3,1)); gesture.finish(session); EXPECT_FALSE(session.dirty());
    ASSERT_TRUE(gesture.begin(session,id,GizmoMode::Scale));
    ASSERT_TRUE(gesture.scale(session,0,1e-8f)); gesture.finish(session);
    EXPECT_FLOAT_EQ(session.editorScene().getComponent<Transform>(id)->scale.x,.001f);
    run(session,SceneOperation::Undo); EXPECT_FALSE(session.dirty());
    run(session,SceneOperation::Play);
    EXPECT_FALSE(gesture.begin(session,id,GizmoMode::Rotate)); EXPECT_FALSE(gesture.begin(session,id,GizmoMode::Scale));
    run(session,SceneOperation::Stop);
}

TEST_F(SceneWorkflow, RotationDragCrossesPiWithoutJumping)
{
    using namespace DirectX;
    const auto vector = [](float degrees) { const float a = XMConvertToRadians(degrees); return math::Vector3{std::cos(a),std::sin(a),0}; };
    EXPECT_NEAR(rotationStep(vector(170),vector(-170),{0,0,1}),XMConvertToRadians(20),1e-5f);
    EXPECT_NEAR(rotationStep(vector(-170),vector(170),{0,0,1}),XMConvertToRadians(-20),1e-5f);
    float accumulated = 0;
    for (int i = 0; i < 8; ++i) accumulated += rotationStep(vector(i*90.f),vector((i+1)*90.f),{0,0,1});
    EXPECT_NEAR(accumulated,2*XM_2PI,1e-4f);
    math::Vector3 point;
    EXPECT_FALSE(rotationPlaneVector({{0,0,-5},{1,0,0},100},{0,0,0},{0,0,1},point));
    EXPECT_FALSE(rotationPlaneVector({{0,0,-5},{0,0,1},100},{0,0,0},{0,0,1},point));
}

TEST_F(SceneWorkflow, GizmoRotationAndScaleSurviveDuplicatePlayAndReload)
{
    MemoryStorage storage; SceneSession session(storage);
    run(session,SceneOperation::CreateEntity);
    const auto id = session.takeCreatedEntity();
    const auto key = session.keyFor(id);
    TransformGesture gesture;
    ASSERT_TRUE(gesture.begin(session,id,GizmoMode::Rotate));
    ASSERT_TRUE(gesture.rotate(session,{0,1,0},.7f)); gesture.finish(session);
    ASSERT_TRUE(gesture.begin(session,id,GizmoMode::Scale));
    ASSERT_TRUE(gesture.scale(session,3,2)); gesture.finish(session);
    const auto expected = *session.editorScene().getComponent<Transform>(id);
    pipelineAction(session,SceneOperation::DuplicateEntity,key);
    const auto copy = session.takeCreatedEntity();
    auto copied = session.editorScene().getComponent<Transform>(copy);
    EXPECT_TRUE(copied->rotation.exactEqual(expected.rotation)); EXPECT_TRUE(copied->scale.exactEqual(expected.scale));
    run(session,SceneOperation::SaveAs,"rotated and scaled");
    const auto authored = pipelineSnapshot(storage,session);
    run(session,SceneOperation::Play);
    for (auto [entity,transform] : session.activeContext().scene.view<Transform>())
    {
        transform.rotation = math::Quaternion{}; transform.scale = math::Vector3{9,9,9};
    }
    run(session,SceneOperation::Stop);
    EXPECT_EQ(pipelineSnapshot(storage,session),authored);
    run(session,SceneOperation::Open,"rotated and scaled");
    EXPECT_EQ(pipelineSnapshot(storage,session),authored);
}

struct GizmoMouseFixture
{
    MemoryStorage storage;
    SceneSession session{storage};
    EditorView view;
    TransformGizmo gizmo;
    Entity::ID id;
    GizmoMouseFixture()
    {
        run(session,SceneOperation::CreateEntity); id = session.takeCreatedEntity();
        run(session,SceneOperation::SaveAs,"gizmo mouse");
        ImGui::CreateContext(); auto& io = ImGui::GetIO(); io.IniFilename = nullptr;
        io.DisplaySize = {800,600}; io.DeltaTime = 1.f/60;
        unsigned char* pixels; int w,h; io.Fonts->GetTexDataAsRGBA32(&pixels,&w,&h);
        view.camera.aspectRatio = 4.f/3; view.transform.position = math::Vector3{0,0,-5};
    }
    void frame(GizmoMode mode)
    {
        ImGui::NewFrame();
        const auto vp = DirectX::XMMatrixMultiply(DirectX::XMMatrixInverse(nullptr,view.transform.world()),view.camera.projectionMatrix());
        gizmo.render(session,id,view.camera,vp,{0,0},{800,600},true,mode);
        if (!gizmo.active()) session.finishEdit();
        ImGui::Render();
    }
};

TEST_F(SceneWorkflow, ImGuiRotateRingDragAndEscape)
{
    GizmoMouseFixture ui; auto& io = ImGui::GetIO();
    io.AddMousePosEvent(480,300); ui.frame(GizmoMode::Rotate);
    io.AddMouseButtonEvent(0,true); ui.frame(GizmoMode::Rotate); ASSERT_TRUE(ui.gizmo.active());
    io.AddMousePosEvent(450,250); ui.frame(GizmoMode::Rotate);
    io.AddMousePosEvent(400,220); ui.frame(GizmoMode::Rotate);
    io.AddMouseButtonEvent(0,false); ui.frame(GizmoMode::Rotate); EXPECT_FALSE(ui.gizmo.active());
    const auto rotation = ui.session.editorScene().getComponent<Transform>(ui.id)->rotation;
    const math::Vector3 x(DirectX::XMVector3Rotate(DirectX::XMVectorSet(1,0,0,0),rotation));
    EXPECT_NEAR(x.x,0.f,1e-4f); EXPECT_NEAR(x.y,1.f,1e-4f);
    run(ui.session,SceneOperation::Undo); EXPECT_FALSE(ui.session.dirty());
    run(ui.session,SceneOperation::Redo); run(ui.session,SceneOperation::Save);
    io.AddMousePosEvent(480,300); ui.frame(GizmoMode::Rotate);
    io.AddMouseButtonEvent(0,true); ui.frame(GizmoMode::Rotate); ASSERT_TRUE(ui.gizmo.active());
    io.AddMousePosEvent(400,220); ui.frame(GizmoMode::Rotate);
    io.AddKeyEvent(ImGuiKey_Escape,true); ui.frame(GizmoMode::Rotate);
    EXPECT_FALSE(ui.gizmo.active()); EXPECT_FALSE(ui.session.dirty());
    EXPECT_TRUE(ui.session.editorScene().getComponent<Transform>(ui.id)->rotation.exactEqual(rotation));
}

TEST_F(SceneWorkflow, ImGuiScaleAxisAndUniformDrag)
{
    GizmoMouseFixture ui; auto& io = ImGui::GetIO();
    io.AddMousePosEvent(464,300); ui.frame(GizmoMode::Scale);
    io.AddMouseButtonEvent(0,true); ui.frame(GizmoMode::Scale); ASSERT_TRUE(ui.gizmo.active());
    io.AddMousePosEvent(504,300); ui.frame(GizmoMode::Scale);
    io.AddMousePosEvent(544,300); ui.frame(GizmoMode::Scale);
    io.AddMouseButtonEvent(0,false); ui.frame(GizmoMode::Scale);
    auto transform = ui.session.editorScene().getComponent<Transform>(ui.id);
    EXPECT_NEAR(transform->scale.x,std::exp(1.f),1e-5f); EXPECT_FLOAT_EQ(transform->scale.y,1); EXPECT_FLOAT_EQ(transform->scale.z,1);
    run(ui.session,SceneOperation::Undo); EXPECT_FALSE(ui.session.dirty());
    io.AddMousePosEvent(400,300); ui.frame(GizmoMode::Scale);
    io.AddMouseButtonEvent(0,true); ui.frame(GizmoMode::Scale); ASSERT_TRUE(ui.gizmo.active());
    io.AddMousePosEvent(440,260); ui.frame(GizmoMode::Scale);
    io.AddMouseButtonEvent(0,false); ui.frame(GizmoMode::Scale);
    EXPECT_GT(transform->scale.x,2.f); EXPECT_FLOAT_EQ(transform->scale.x,transform->scale.y); EXPECT_FLOAT_EQ(transform->scale.x,transform->scale.z);
    run(ui.session,SceneOperation::Undo); EXPECT_FALSE(ui.session.dirty());
}
