cmake_minimum_required(VERSION 3.20)

project(STNKS_CORE CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(STNKS_CORE_HEADERS
    include/Stnks.hpp
    include/Paths.hpp
    include/Engine.hpp
    include/Transform.hpp
        include/App/App.hpp
    include/Dependency.hpp
    include/App/EnginePipeline.hpp
    include/App/ExecutionPipeline.hpp
    include/App/SDL2App.hpp
    include/Graphics/Rendering.hpp
    include/Graphics/Rendering/RenderingContext.hpp
    include/Graphics/Rendering/Renderable.hpp
    include/Graphics/Rendering/FrameBuffer.hpp
    include/Graphics/Rendering/Frustum.hpp
    include/Graphics/Rendering/Entities/Camera.hpp
    include/Graphics/Shading/Shader.hpp
    include/Graphics/Shading/ShaderPipeline.hpp
    include/Input/PlayerInput.hpp
    include/GlobalKeys.hpp
    include/Dependencies/Globals.hpp
    include/ECS/ComponentPool.hpp
    include/ECS/Entity.hpp
    include/ECS/ISystem.hpp
    include/ECS/Registry.hpp
    include/Threading/ThreadPool.hpp
    include/Threading/AsyncQueue.hpp
    include/Threading/WorkerThread.hpp
    include/Threading/ThreadRegistry.hpp
    include/UI/UI.hpp
    # HTTP & Market
    include/Http/HttpClient.hpp
    include/Market/MarketData.hpp
    include/Market/IMarketSource.hpp
    include/Market/YahooFinanceSource.hpp
    include/Market/MarketService.hpp
    # Charts
    include/Charts/ChartLayer.hpp
    include/Charts/CandlestickLayer.hpp
    include/Charts/VolumeLayer.hpp
    include/Charts/RSILayer.hpp
    include/Charts/StockChart.hpp
    include/Charts/IStrategyRenderer.hpp
    include/Charts/TPSLRenderer.hpp
    include/Charts/StrategyLayer.hpp
    # Strategy
    include/Strategy/Strategy.hpp
    include/Strategy/StrategyStore.hpp
    # News
    include/News/NewsService.hpp
    # AI
    include/AI/IMarketAnalyzer.hpp
    include/AI/ClaudeAnalyzer.hpp
    # Service
    include/Service/IStrategyService.hpp
    include/Service/LocalStrategyService.hpp
    include/Service/RemoteStrategyService.hpp
    # Net
    include/Net/UdpMarketFeed.hpp
    # Server
    include/Server/IStrategyAction.hpp
    include/Server/BrokerAction.hpp
    include/Server/SentimentAction.hpp
    include/Server/StrategyServer.hpp
    include/Server/HttpApiServer.hpp
    # IMGUI LIBRARY HEADERS
    external/imgui/imgui.h
    external/imgui/imgui_internal.h
    external/imgui/imstb_rectpack.h
    external/imgui/imstb_truetype.h
    external/imgui/imstb_textedit.h
    external/imgui/imconfig.h
    external/imgui/backends/imgui_impl_sdl2.h
    external/imgui/backends/imgui_impl_opengl3.h)

set(STNKS_CORE_SOURCES
    src/Stnks.cpp
    src/App/SDL2App.cpp
    src/Graphics/Rendering.cpp
    src/Graphics/Rendering/Renderable.cpp
    src/Graphics/Rendering/FrameBuffer.cpp
    src/Graphics/Rendering/Entities/Camera.cpp
    src/Graphics/Shading/Shader.cpp
    src/Graphics/Shading/ShaderPipeline.cpp
    src/Input/PlayerInput.cpp
    src/Engine.cpp
    src/UI/UI.cpp
    # HTTP & Market
    src/Http/HttpClient.cpp
    src/Market/MarketService.cpp
    src/Market/YahooFinanceSource.cpp
    # Charts
    src/Charts/StockChart.cpp
    # Strategy
    src/Strategy/StrategyStore.cpp
    # News
    src/News/NewsService.cpp
    # AI
    src/AI/ClaudeAnalyzer.cpp
    # Service
    src/Service/LocalStrategyService.cpp
    src/Service/RemoteStrategyService.cpp
    # Net
    src/Net/UdpMarketFeed.cpp
    # Server
    src/Server/BrokerAction.cpp
    src/Server/SentimentAction.cpp
    src/Server/StrategyServer.cpp
    src/Server/HttpApiServer.cpp
    # IMGUI LIBRARY SOURCES
    external/imgui/imgui.cpp
    external/imgui/imgui_demo.cpp
    external/imgui/imgui_draw.cpp
    external/imgui/imgui_tables.cpp
    external/imgui/imgui_widgets.cpp
    external/imgui/backends/imgui_impl_sdl2.cpp
    external/imgui/backends/imgui_impl_opengl3.cpp)

# PACKAGES
find_package(Threads REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(SDL2 REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(GLEW REQUIRED)
find_package(Stb REQUIRED)
find_package(cereal CONFIG REQUIRED)
find_package(cpr CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(unofficial-sqlite3 CONFIG REQUIRED)
find_package(httplib CONFIG REQUIRED)
find_package(unofficial-enet CONFIG REQUIRED)

# DIR INCLUDES
include_directories(${GLEW_INCLUDE_DIRS})

# ADD THE CORE LIBRARY
add_library(STNKS_CORE STATIC ${STNKS_CORE_SOURCES} ${STNKS_CORE_HEADERS})

set_target_properties(STNKS_CORE PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_compile_definitions(STNKS_CORE
    PUBLIC
        $<$<PLATFORM_ID:Windows>:NOMINMAX>
        $<$<PLATFORM_ID:Windows>:WIN32_LEAN_AND_MEAN>)

target_include_directories(STNKS_CORE
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
    PUBLIC ${SDL2_INCLUDE_DIRS}
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/external/imgui
           ${CMAKE_CURRENT_SOURCE_DIR}/external/portable-file-dialogs
    PUBLIC ${Stb_INCLUDE_DIR}
    PUBLIC ${GLM_INCLUDE_DIRS})

target_link_libraries(STNKS_CORE
    PUBLIC Threads::Threads
    PUBLIC ${SDL2_LIBRARIES}
    PUBLIC GLEW::GLEW
    PUBLIC glm::glm
    PUBLIC spdlog::spdlog
    PUBLIC cereal::cereal
    PUBLIC cpr::cpr
    PUBLIC nlohmann_json::nlohmann_json
    PUBLIC unofficial::sqlite3::sqlite3
    PUBLIC httplib::httplib
    PUBLIC unofficial::enet::enet)

# std::filesystem needs explicit linking on GCC < 9
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS "9.0")
    target_link_libraries(STNKS_CORE PUBLIC stdc++fs)
endif()
