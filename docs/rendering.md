# Rendering pipeline: deferred + forward, clustered lights

Every camera view (`CameraRenderView`) owns a set of attachments
(`CameraRenderTarget`, `ViewAttachment`). Systems refer to them as
`scene.<suffix>` in `TaskDescription::gpu(...)`; the frame graph expands a
`GpuTaskScope::View` task once per rendered camera and resolves the names to
`view.<id>.<suffix>`.

| suffix           | format                    | content                                   |
| ---------------- | ------------------------- | ----------------------------------------- |
| `gbuffer0`       | `R8G8B8A8_UNORM`          | albedo.rgb (linear), metallic             |
| `gbuffer1`       | `R16G16B16A16_FLOAT`      | world normal.xyz, roughness               |
| `gbuffer2`       | `R11G11B10_FLOAT`         | emissive radiance                         |
| `depth`          | `R32_TYPELESS` (D32/R32)  | reverse-Z depth (0 = far / sky)           |
| `hdr`            | `R16G16B16A16_FLOAT`      | linear scene radiance                     |
| `color`          | `R8G8B8A8_UNORM`          | final LDR image (editor / presentation)   |
| `clusterGrid`    | `StructuredBuffer<uint>`  | light count per froxel                    |
| `clusterIndices` | `StructuredBuffer<uint>`  | 64 light indices per froxel               |

## Passes (phase `render`, in order)

1. **FramePrepareSystem** (phase `prepare`): clears the GBuffer and depth.
2. **MeshRenderSystem** – geometry pass. Draws every `MeshRenderer` whose
   shader writes `SV_TARGET0..2` (`GraphicShader::isDeferred()`), batched by
   material and mesh.
3. **LightClusterSystem** – compute (`shaders/light_cluster.coshader`). One
   thread per froxel (16 x 8 tiles x 24 exponential depth slices) tests every
   point light sphere against the froxel's view-space AABB and writes the
   index list. Skipped for orthographic cameras and when
   `LightingSettings::clustered` is off (the shaders then loop over all lights).
4. **LightPassSystem** – fullscreen triangle (`shaders/light_pass.shader`):
   reconstructs the world position from depth, shades with Cook-Torrance GGX
   (directional lights + the froxel's point lights + ambient + emissive) and
   writes `hdr`. Pixels with depth 0 get the camera background.
5. **ForwardRenderSystem** – single-target materials on top of `hdr`: opaque
   unlit shaders first (depth write), then materials with blending enabled
   sorted back-to-front (depth test only). Lit forward shaders include
   `lighting.hlsli` and read the same light/cluster buffers.
6. **TonemapSystem** – exposure, ACES, gamma 2.2 into `color`. Debug views pass through.

The editor (`EditorSystem`) and the presentation copy read `color` afterwards.

## Components

* `DirectionalLight { color, intensity, enabled }` – direction = entity forward (+Z).
* `PointLight { color, intensity, radius, enabled }` – radiance at 1 m, windowed to 0 at `radius`.
* `LightingSettings { ambientColor, ambientIntensity, exposure, clustered, debugView }` –
  one per scene (first found). Debug views: 1 albedo, 2 normals,
  3 roughness/metallic, 4 depth, 5 lights per cluster (heat map), 6 emissive,
  7 lighting only (white albedo).

`LightCollectSystem` (phase `acquire`, before `FrameAcquireSystem`) gathers
them into the `SceneLights` service (`GpuLight`, 64 bytes, directional lights
first) and `Renderer::environment()`, which `cameraFrame()` copies into every
view's `EngineVariableBuffer`.

## Writing shaders

* **Deferred material**: `#include "gbuffer.hlsli"`, return `GBufferOutput`
  from `packGBuffer(albedoLinear, metallic, normal, roughness, emissive)`.
  `lit.shader` / `lit_texture.shader` are the references. Material parameters
  `tint`, `roughness`, `metallic`, `emissive` (rgb sRGB + strength) are set by
  `Primitives::initializeMaterialDefaults`.
* **Forward lit (transparent)**: `#include "lighting.hlsli"`, call
  `computeLighting(Surface, SV_Position.xy, outCount)`; use
  `Primitives::transparentStates()` for the material (`forward_lit.shader`).
* **Unlit forward**: any single-target shader (`default.shader`,
  `texture.shader`). Output linear radiance (`pow(c, 2.2)` for sRGB inputs).
* `#include "file"` is expanded textually relative to the shader's folder
  (`shader_file.h`). Only one `//@update frame` cbuffer is bound per shader, so
  shaders that include `lighting.hlsli`/`gbuffer.hlsli` must not declare their own.
* New frame semantics: `View`, `InvProjection`, `InvViewProjection`,
  `ScreenParams`, `CameraParams`, `LightParams`, `ClusterParams`,
  `ClusterDepthParams`, `AmbientColor` (w = exposure), `SunColor`, `BackgroundColor`.
* `StructuredBuffer`/`ByteAddressBuffer` are bound as root SRVs and
  `RWStructuredBuffer` as root UAVs (GPU address, no descriptor):
  `Renderer::setShaderBuffer` / `setShaderRWBuffer`. Textures stay descriptor tables.

## Render target formats come from the pass

`Renderer::passStates()` overwrites `numRenderTargets`, `rtvFormats` and
`dsvFormat` of a material's `MaterialStateDesc` with the attachments the
recording pass declared, so one material works in any pass and the PSO cache
keys on the result. Materials only carry blend / rasterizer / depth state.

## Adding a pass

1. Declare the attachments in `describe()` with the D3D12 state you need
   (`RENDER_TARGET`, `DEPTH_WRITE`, `PIXEL_SHADER_RESOURCE`,
   `UNORDERED_ACCESS`); the frame graph inserts the barriers and binds render
   targets in declaration order. Use `after/before` to order against other
   render-phase systems.
2. Fullscreen work: `FullscreenPass` (`load()` in `onAttach`, `bind()` then
   `Renderer::setShaderTexture/Buffer`, `drawFullscreen()`).
3. Compute: `Renderer::bindComputeShader`, `setShaderConstants`,
   `setShaderRWBuffer`, `dispatch`. `PSOFactory::getCompute` caches the PSO.
4. New per-view textures: add a `ViewAttachment`, its format and resting
   state in `camera_render_target.cpp`; the frame graph picks it up automatically.

## Not done yet

Shadows (CSM for the sun), spot lights (`GpuLight::extra` is reserved),
image-based ambient, bloom. Cluster assignment is one dispatch per view; a
depth-bounded (tiled) cull would reduce the per-pixel light loop further.
