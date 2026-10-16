#include "application.h"
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
    for (auto [id, body] : csyren::core::Services::get<csyren::core::Scene>()->view<const csyren::physics::RigidBody>()) ++result;
    return result;
}
void CALLBACK inspect(HWND, UINT, UINT_PTR timer, DWORD) {
    using namespace csyren;
    if (stage++ == 0) {
        bodiesBefore = bodies();
        auto hwnd = core::Services::get<core::Window>()->hwnd();
        PostMessage(hwnd, WM_KEYDOWN, VK_SPACE, 0);
        PostMessage(hwnd, WM_KEYUP, VK_SPACE, 0);
        return;
    }
    if (stage == 2) { bodiesAfter = bodies(); return; }
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
int main() {
    csyren::Application application;
    if (!application.init()) return 2;
    SetTimer(nullptr, 0, 2000, inspect);
    const auto result = application.run();
    std::cout << "Application exit=" << result << ", inspected=" << inspected
        << ", rigid bodies=" << bodiesBefore << "->" << bodiesAfter
        << ", D3D12 errors=" << errors << ", warnings=" << warnings << '\n';
    return result || !inspected || errors || warnings || bodiesAfter <= bodiesBefore;
}
