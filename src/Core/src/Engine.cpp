#include <Engine.hpp>
#include <Dependency.hpp>
#include <chrono>
#include <filesystem>
#ifdef _WIN32
#  include <windows.h>
#else
#  include <unistd.h>
#endif

namespace stnks {
#ifndef STNKS_HEADLESS
    Engine::Engine(std::shared_ptr<App> appInstance) : appInstance_(appInstance) {
    }
#else
    Engine::Engine() {}
#endif

    Engine::~Engine() {
        threadRegistry_.Shutdown();

        if (globals_) {
            spdlog::warn("Engine config auto-saved");
            StoreGlobals(paths::RESOURCES_DEFAULT);
        }
    }

    void Engine::LoadGlobals(const std::string &fileName) {
        const auto filePath = globals_->GetWorkingFolder() + fileName;

        std::ifstream file(filePath);
        if (!file.is_open()) {
            spdlog::error("Globals file not found: '{}'", filePath);
            return;
        }

        {
            unsigned char bom[3] = {};
            file.read(reinterpret_cast<char *>(bom), 3);
            if (!(bom[0] == 0xEF && bom[1] == 0xBB && bom[2] == 0xBF))
                file.seekg(0);
        }

        try {
            cereal::JSONInputArchive ar(file);
            ar(*globals_);
        } catch (const std::exception &e) {
            spdlog::error("Failed to parse globals '{}': {}", filePath, e.what());
        }
    }

    void Engine::StoreGlobals(const std::string &fileName) const {
        const auto filePath = globals_->GetWorkingFolder() + fileName;

        std::ofstream file(filePath);
        if (!file.is_open()) {
            spdlog::warn("Cannot write globals file: '{}'", filePath);
            return;
        }

        cereal::JSONOutputArchive ar(file);
        ar(*globals_);
    }

    void Engine::ConfigResource() {
        globals_ = GetDependency(Globals);
        globals_->AutoSetWorkingFolder();
        globals_->SetupDefaults();
        LoadGlobals(paths::RESOURCES_DEFAULT);
        StoreGlobals(paths::RESOURCES_DEFAULT);

        globals_->Set(gk::prefix::ENGINE, gk::key::ENGINE_WORKING_DIR,
                      globals_->GetWorkingFolder());

#ifdef _WIN32
        char exeBuf[512] = {};
        if (GetModuleFileNameA(nullptr, exeBuf, sizeof(exeBuf))) {
            const std::string exeDir =
                    std::filesystem::path(exeBuf).parent_path().string();
            globals_->Set(gk::prefix::ENGINE, gk::key::ENGINE_EXE_DIR, exeDir);
        }
#else
        {
            char exeBuf[512] = {};
            ssize_t len = ::readlink("/proc/self/exe", exeBuf, sizeof(exeBuf) - 1);
            if (len > 0) {
                exeBuf[len] = '\0';
                const std::string exeDir =
                        std::filesystem::path(exeBuf).parent_path().string();
                globals_->Set(gk::prefix::ENGINE, gk::key::ENGINE_EXE_DIR, exeDir);
            }
        }
#endif
    }

    void Engine::Init() {
        ConfigResource();

#ifndef STNKS_HEADLESS
        auto mainWindowSize = appInstance_->GetMainWindowSize();
        renderEngine_.SetScreenSize(mainWindowSize.x, mainWindowSize.y);

        // Create a default orthographic camera
        camera_ = std::make_shared<Camera>(mainWindowSize.x, mainWindowSize.y);
        camera_->underylingTransform.setGlobalPosition({0.0f, 0.0f, -1.0f});
        camera_->Init(GetDependency(Engine));
        renderEngine_.AddRenderable(camera_);
#endif

        spdlog::info("[Engine] Initialized - stnks finance tool ready");
    }

    void Engine::Update() {
        using Clock = std::chrono::high_resolution_clock;
        using Ms = std::chrono::duration<float, std::milli>;

        const auto frameStart = Clock::now();

        auto t1 = Clock::now();
        // Main update logic runs here
        threadDebugInfo_.main.durationMs = Ms(Clock::now() - t1).count();
        threadDebugInfo_.main.async = false;

        threadDebugInfo_.updatePhaseMs = Ms(Clock::now() - frameStart).count();
    }

    void Engine::PrepareFrame() {
    }

    void Engine::PresentFrame() {
#ifndef STNKS_HEADLESS
        using Clock = std::chrono::high_resolution_clock;
        using Ms = std::chrono::duration<float, std::milli>;

        const float dt = appInstance_->GetDeltaTime();
        const auto frameStart = Clock::now();

        auto t2 = Clock::now();
        renderEngine_.Pass(dt);
        threadDebugInfo_.rendering.durationMs = Ms(Clock::now() - t2).count();
        threadDebugInfo_.rendering.async = false;

        threadDebugInfo_.presentPhaseMs = Ms(Clock::now() - frameStart).count();
        threadDebugInfo_.PushSample();

        inputSystem_.ResetState();
#endif
    }

#ifndef STNKS_HEADLESS
    PlayerInput *Engine::GetInputSystem() {
        return &inputSystem_;
    }
#endif
} // namespace stnks
