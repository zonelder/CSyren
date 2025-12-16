#pragma once



namespace csyren::render
{
	//<resources>
	class Texture;
	class Mesh;
	class Shader;
	class Material;

	template<typename T> class ResourceStorage;
	//</resources>


	class UploadRingBuffer;
	class Renderer;
	class ResourceManager;
	class ResourceUploadThread;
	class UploadContext;

}