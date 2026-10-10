#include "scene_picking.h"
#include "editor_view.h"
#include "dx12_graphic/mesh_builder.h"
#include "core/meta_json.h"
#include <fstream>
#include <iostream>

int main(int argc, char** argv)
{
    using namespace csyren;
    using namespace DirectX;
    if (argc != 2) return 2;
    std::ifstream input(argv[1]);
    nlohmann::json document; input >> document;
    render::MeshBuilder cube;
    for (int i = 0; i < 8; ++i)
        cube.addVertex({ (i&1) ? .5f : -.5f, (i&2) ? .5f : -.5f, (i&4) ? .5f : -.5f });
    for (const auto face : { std::array<uint16_t, 4>{ 0, 1, 3, 2 }, { 4, 6, 7, 5 },
        { 0, 2, 6, 4 }, { 1, 5, 7, 3 }, { 0, 4, 5, 1 }, { 2, 3, 7, 6 } })
        cube.addTriangle(face[0], face[1], face[2]).addTriangle(face[0], face[2], face[3]);
    const auto geometry = cube.build();
    int cubeResource = -1;
    for (const auto& resource : document["resources"])
        if (resource["path"] == "__primitive_cube__") cubeResource = resource["id"];
    std::vector<math::Matrix4x4> worlds;
    for (const auto& entity : document["entities"])
    {
        const auto& components = entity["components"];
        if (!components.contains("MeshFilter") || components["MeshFilter"]["mesh"] != cubeResource) continue;
        const auto& t = components["Transform"];
        core::Transform transform;
        transform.position = math::Vector3(t["position"][0], t["position"][1], t["position"][2]);
        transform.scale = math::Vector3(t["scale"][0], t["scale"][1], t["scale"][2]);
        transform.rotation = math::Quaternion(t["rotation"][0], t["rotation"][1], t["rotation"][2], t["rotation"][3]);
        worlds.push_back(transform.world());
    }
    editor::EditorView view;
    const auto vp = XMMatrixMultiply(XMMatrixInverse(nullptr, view.transform.world()), view.camera.projectionMatrix());
    int checked = 0;
    for (int y = 0; y <= 40; ++y) for (int x = 0; x <= 40; ++x)
    {
        const auto ray = editor::viewportRay(x/20.f-1, y/20.f-1, vp);
        float nearBounds = ray.length, nearMesh = ray.length;
        for (const auto& world : worlds)
        {
            float distance;
            if (editor::intersectBounds(ray, geometry.bounds, world, distance)) nearBounds = std::min(nearBounds, distance);
            if (editor::intersectMesh(ray, geometry.bounds, geometry.positions, geometry.indices, world, distance))
                nearMesh = std::min(nearMesh, distance);
        }
        if (std::abs(nearBounds - nearMesh) > .001f)
        { std::cerr << "Mismatch at " << x << ',' << y << ": bounds=" << nearBounds << " mesh=" << nearMesh << '\n'; return 1; }
        ++checked;
    }
    std::cout << worlds.size() << " scene cubes, " << checked << " rays: nearest mesh surface matches analytic cube bounds\n";
}
