#include "application.h"
#include "scene_session.h"
#include "core/input_event.h"
#include "core/services.h"
#include "core/window.h"
#include "core/scene.h"
#include "physics/rigid_body.h"
#include "dx12_graphic/renderer.h"
#include <d3d12sdklayers.h>
#include <iostream>

namespace {
unsigned errors = 0, warnings = 0, stage = 0;
size_t bodiesBefore = 0, bodiesAfter = 0;
bool inspected = false;
size_t bodies() {
    size_t result = 0;
    for (auto [id, body] : csyren::core::Services::get<csyren::editor::SceneSession>()->activeContext().scene.view<const csyren::physics::RigidBody>()) ++result;
    return result;
}
void CALLBACK inspect(HWND, UINT, UINT_PTR timer, DWORD) {
    using namespace csyren;
    if (stage++ == 0) {
        core::Services::get<editor::SceneSession>()->enqueue(editor::SceneOperation::Play);
        return;
    }
    if (stage == 2) {
        bodiesBefore = bodies();
        auto* session = core::Services::get<editor::SceneSession>();
        if (!session->playing() || !session->error().empty()) ++errors;
        // Inject gameplay input independently of ImGui keyboard focus.
        auto& bus = session->activeContext().bus;
        auto token = bus.register_publisher<core::InputEvent>(static_cast<uint32_t>(core::InputEvent::Type::KeyDown));
        core::InputEvent event(core::InputEvent::Type::KeyDown, core::DeviceType::Keyboard, VK_SPACE);
        bus.publish(token, event);
        return;
    }
    if (stage == 3) {
        bodiesAfter = bodies();
        core::Services::get<editor::SceneSession>()->enqueue(editor::SceneOperation::Stop);
        return;
    }
    if (core::Services::get<editor::SceneSession>()->playing() || bodies() != bodiesBefore) ++errors;
    KillTimer(nullptr, timer);
    auto* renderer = core::Services::get<render::Renderer>();
    renderer->queue().syncAwaitLastComplete();
    Microsoft::WRL::ComPtr<ID3D12InfoQueue> info;
    renderer->device()->QueryInterface(IID_PPV_ARGS(&info));
    if (info) for (UINT64 i = 0; i < info->GetNumStoredMessages(); ++i) {
        SIZE_T size = 0; info->GetMessage(i, nullptr, &size); std::vector<char> bytes(size);
        auto* message = reinterpret_cast<D3D12_MESSAGE*>(bytes.data()); info->GetMessage(i, message, &size);
        if (message->Severity <= D3D12_MESSAGE_SEVERITY_ERROR) ++errors;
        if (message->Severity == D3D12_MESSAGE_SEVERITY_WARNING) ++warnings;
        if (message->Severity <= D3D12_MESSAGE_SEVERITY_WARNING) std::cerr << message->pDescription << '\n';
    }
    if (FAILED(renderer->device()->GetDeviceRemovedReason())) ++errors;
    inspected = true;
    PostQuitMessage(0);
}
}
int main(int argc, char** argv) {
    csyren::Application application;
    if (!application.init(argc > 1 ? std::filesystem::path(argv[1]) : std::filesystem::path("CSyren.csyrenproj"))) return 2;
    SetTimer(nullptr, 0, 2000, inspect);
    const auto result = application.run();
    std::cout << "Application exit=" << result << ", inspected=" << inspected
        << ", rigid bodies=" << bodiesBefore << "->" << bodiesAfter
        << ", D3D12 errors=" << errors << ", warnings=" << warnings << '\n';
    return result || !inspected || errors || warnings || bodiesAfter <= bodiesBefore;
}
