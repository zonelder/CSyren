#pragma once


#include "cstdmf/file_system.h"

namespace csyren
{
	class Application
	{
	public:
		Application();
		~Application();

		Application(const Application& rhs) = delete;
		Application& operator=(const Application& rhs) = delete;

        // Empty engineAssets uses the engine repository from CSyren.paths.props.
        bool init(const std::filesystem::path& engineAssets = {},
                  const std::vector<cstdmf::FileSystem::Mount>& solutionPaths = {});
		int	 run();

		void onSceneStart();

	private:
	};
}
