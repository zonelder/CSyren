#pragma once
// Test access to Scene's task binding and transaction (Scene befriends this type).
#include "core/scene.h"

namespace csyren::core::details {
struct SceneJobTestAccess {
    using Transaction = Scene::Transaction;
    using TaskScope = Scene::TaskScope;
};
}
