#pragma once



namespace csyren::render
{
	//<resources>
	class Texture;
	class Mesh;
	class GraphicShader;
	class Material;

	template<typename T> class ResourceStorage;
	//</resources>


	class UploadRingBuffer;
	class Renderer;
	class ResourceManager;
	class ResourceUploadThread;
	class UploadContext;

}