#pragma once



namespace csyren::render
{
	//<resources>
	class Texture;
	class Mesh;
	class GraphicShader;
    class ComputeShader;
	class Material;
	class AnimationClip;

	template<typename T> class ResourceStorage;
	//</resources>


	class UploadRingBuffer;
	class Renderer;
	class ResourceManager;
    class ResourceUploadPipeline;
    class RenderQueue;
	class UploadContext;

}
