cmake_minimum_required(VERSION 3.20)

project(STNKS_CORE CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(STNKS_HEADLESS "Build without graphics (SDL2, OpenGL, ImGui, UI, Charts)" OFF)

set(STNKS_CORE_HEADERS
    include/EnumTraits.hpp
    include/Stnks.hpp
    include/Paths.hpp
    include/Engine.hpp
    include/Transform.hpp
    include/Dependency.hpp
    include/App/App.hpp
    include/App/EnginePipeline.hpp
    include/App/ExecutionPipeline.hpp
    include/GlobalKeys.hpp
    include/Dependencies/Globals.hpp
    include/Threading/ThreadPool.hpp
    include/Threading/AsyncQueue.hpp
    include/Threading/WorkerThread.hpp
    include/Threading/ThreadRegistry.hpp
    # HTTP & Market
    include/Http/HttpClient.hpp
    include/Market/MarketData.hpp
    include/Market/IMarketSource.hpp
    include/Market/IBrokerDataSource.hpp
    include/Market/YahooFinanceSource.hpp
    include/Market/MarketService.hpp
    include/Market/MarketHours.hpp
    # Strategy
    include/Strategy/Strategy.hpp
    include/Strategy/StrategyStore.hpp
    # Database
    include/Db/DbStore.hpp
    include/Db/Database.hpp
    include/Db/MigrationRunner.hpp
    include/Db/Queries.hpp
    include/Db/StrategySchema.hpp
    # Events
    include/Events/GraphEvent.hpp
    include/Events/GraphEventService.hpp
    # Signals
    include/Signals/MarketSignal.hpp
    include/Signals/MarketSignalService.hpp
    # News
    include/News/NewsService.hpp
    # AI
    include/AI/IMarketAnalyzer.hpp
    include/AI/ClaudeAnalyzer.hpp
    include/AI/PromptTemplate.hpp
    # Service
    include/Service/IStrategyService.hpp
    include/Service/LocalStrategyService.hpp
    include/Service/RemoteStrategyService.hpp
    # Net
    include/Net/UdpMarketFeed.hpp
    include/Net/IWebSocketClient.hpp
    include/Net/WebSocketClient.hpp
    # Broker
    include/Broker/IBrokerConnector.hpp
    include/Broker/WebhookReceiver.hpp
    include/Broker/MetaTraderConnector.hpp
    include/Broker/BinanceBrokerConnector.hpp
    # Server
    include/Server/IStrategyAction.hpp
    include/Server/BrokerAction.hpp
    include/Server/SentimentAction.hpp
    include/Server/StrategyServer.hpp
    include/Server/HttpApiServer.hpp)

set(STNKS_CORE_SOURCES
    src/Stnks.cpp
    src/Engine.cpp
    # HTTP & Market
    src/Http/HttpClient.cpp
    src/Market/MarketService.cpp
    src/Market/YahooFinanceSource.cpp
    # Strategy
    src/Strategy/StrategyStore.cpp
    # Database
    src/Db/Database.cpp
    src/Db/MigrationRunner.cpp
    src/Db/StrategySchema.cpp
    # Events
    src/Events/GraphEventService.cpp
    # Signals
    src/Signals/MarketSignalService.cpp
    # News
    src/News/NewsService.cpp
    # AI
    src/AI/ClaudeAnalyzer.cpp
    # Service
    src/Service/LocalStrategyService.cpp
    src/Service/RemoteStrategyService.cpp
    # Net
    src/Net/UdpMarketFeed.cpp
    src/Net/WebSocketClient.cpp
    # Broker
    src/Broker/WebhookReceiver.cpp
    src/Broker/MetaTraderConnector.cpp
    src/Broker/BinanceBrokerConnector.cpp
    # Server
    src/Server/BrokerAction.cpp
    src/Server/SentimentAction.cpp
    src/Server/StrategyServer.cpp
    src/Server/HttpApiServer.cpp)

if(NOT STNKS_HEADLESS)
    list(APPEND STNKS_CORE_HEADERS
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
        include/UI/UI.hpp
        include/UI/UIContext.hpp
        include/UI/UIConstants.hpp
        include/UI/StrategyTableDefs.hpp
        include/UI/StrategyTablePanel.hpp
        include/UI/PortfolioPanel.hpp
        include/UI/MarketSignalsPanel.hpp
        include/UI/AIPanels.hpp
        include/UI/GraphEventsPanel.hpp
        include/UI/MarketInsightsPanel.hpp
        include/UI/DashboardPanel.hpp
        include/UI/ServerLauncherPanel.hpp
        include/UI/PositionsPanel.hpp
        # Charts
        include/Charts/ChartLayer.hpp
        include/Charts/CandlestickLayer.hpp
        include/Charts/VolumeLayer.hpp
        include/Charts/RSILayer.hpp
        include/Charts/StockChart.hpp
        include/Charts/MACDLayer.hpp
        include/Charts/ChartGrid.hpp
        include/Charts/DetachedChart.hpp
        include/Charts/VolumeProfileLayer.hpp
        include/Charts/IStrategyRenderer.hpp
        include/Charts/TPSLRenderer.hpp
        include/Charts/StrategyLayer.hpp
        include/Charts/StrategyWizard.hpp
        # IMGUI LIBRARY HEADERS
        external/imgui/imgui.h
        external/imgui/imgui_internal.h
        external/imgui/imstb_rectpack.h
        external/imgui/imstb_truetype.h
        external/imgui/imstb_textedit.h
        external/imgui/imconfig.h
        external/imgui/backends/imgui_impl_sdl2.h
        external/imgui/backends/imgui_impl_opengl3.h)

    list(APPEND STNKS_CORE_SOURCES
        src/App/SDL2App.cpp
        src/Graphics/Rendering.cpp
        src/Graphics/Rendering/Renderable.cpp
        src/Graphics/Rendering/FrameBuffer.cpp
        src/Graphics/Rendering/Entities/Camera.cpp
        src/Graphics/Shading/Shader.cpp
        src/Graphics/Shading/ShaderPipeline.cpp
        src/Input/PlayerInput.cpp
        src/UI/UI.cpp
        src/UI/StrategyTablePanel.cpp
        src/UI/PortfolioPanel.cpp
        src/UI/MarketSignalsPanel.cpp
        src/UI/AIPanels.cpp
        src/UI/GraphEventsPanel.cpp
        src/UI/MarketInsightsPanel.cpp
        src/UI/DashboardPanel.cpp
        src/UI/ServerLauncherPanel.cpp
        src/UI/PositionsPanel.cpp
        # Charts
        src/Charts/StockChart.cpp
        src/Charts/DetachedChart.cpp
        # IMGUI LIBRARY SOURCES
        external/imgui/imgui.cpp
        external/imgui/imgui_demo.cpp
        external/imgui/imgui_draw.cpp
        external/imgui/imgui_tables.cpp
        external/imgui/imgui_widgets.cpp
        external/imgui/backends/imgui_impl_sdl2.cpp
        external/imgui/backends/imgui_impl_opengl3.cpp)
endif()

find_package(Threads REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(cereal CONFIG REQUIRED)
find_package(cpr CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(unofficial-sqlite3 CONFIG REQUIRED)
find_package(httplib CONFIG REQUIRED)
find_package(unofficial-enet CONFIG REQUIRED)
find_package(ixwebsocket CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)

if(NOT STNKS_HEADLESS)
    find_package(SDL2 REQUIRED)
    find_package(glm CONFIG REQUIRED)
    find_package(GLEW REQUIRED)
    find_package(Stb REQUIRED)
    include_directories(${GLEW_INCLUDE_DIRS})
endif()

add_library(STNKS_CORE STATIC ${STNKS_CORE_SOURCES} ${STNKS_CORE_HEADERS})

set_target_properties(STNKS_CORE PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_compile_definitions(STNKS_CORE
    PUBLIC
        $<$<PLATFORM_ID:Windows>:NOMINMAX>
        $<$<PLATFORM_ID:Windows>:WIN32_LEAN_AND_MEAN>
        $<$<BOOL:${STNKS_HEADLESS}>:STNKS_HEADLESS>
    PRIVATE
        STNKS_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../..")

target_include_directories(STNKS_CORE
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

if(NOT STNKS_HEADLESS)
    target_include_directories(STNKS_CORE
        PUBLIC ${SDL2_INCLUDE_DIRS}
        PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/external/imgui
               ${CMAKE_CURRENT_SOURCE_DIR}/external/portable-file-dialogs
        PUBLIC ${Stb_INCLUDE_DIR}
        PUBLIC ${GLM_INCLUDE_DIRS})
endif()

target_link_libraries(STNKS_CORE
    PUBLIC Threads::Threads
    PUBLIC spdlog::spdlog
    PUBLIC cereal::cereal
    PUBLIC cpr::cpr
    PUBLIC nlohmann_json::nlohmann_json
    PUBLIC unofficial::sqlite3::sqlite3
    PUBLIC httplib::httplib
    PUBLIC unofficial::enet::enet
    PUBLIC ixwebsocket::ixwebsocket
    PUBLIC OpenSSL::SSL OpenSSL::Crypto)

if(NOT STNKS_HEADLESS)
    target_link_libraries(STNKS_CORE
        PUBLIC ${SDL2_LIBRARIES}
        PUBLIC GLEW::GLEW
        PUBLIC glm::glm)
endif()

# std::filesystem needs explicit linking on GCC < 9
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS "9.0")
    target_link_libraries(STNKS_CORE PUBLIC stdc++fs)
endif()
