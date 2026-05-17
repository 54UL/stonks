#pragma once

#include <App/App.hpp>
#include <Paths.hpp>
#include <App/EnginePipeline.hpp>
#include <Graphics/Rendering.hpp>
#include <Graphics/Rendering/Entities/Camera.hpp>
#include <Input/PlayerInput.hpp>
#include <Dependencies/Globals.hpp>
#include <Threading/ThreadRegistry.hpp>
#include <ECS/Registry.hpp>

#include <memory>
#include <vector>
#include <array>
#include <imgui.h>
#include <spdlog/spdlog.h>

namespace stnks
{
    struct ChannelSample
    {
        float durationMs = 0.f;
        bool  async      = false;
    };

    struct ThreadDebugInfo
    {
        static constexpr int kHistorySize = 96;

        ChannelSample main;
        ChannelSample rendering;
        float updatePhaseMs  = 0.f;
        float presentPhaseMs = 0.f;

        std::array<float, kHistorySize> mainHistory      {};
        std::array<float, kHistorySize> renderingHistory {};
        std::array<float, kHistorySize> frameHistory     {};
        int historyOffset = 0;

        void PushSample()
        {
            mainHistory     [historyOffset] = main.durationMs;
            renderingHistory[historyOffset] = rendering.durationMs;
            frameHistory    [historyOffset] = updatePhaseMs + presentPhaseMs;
            historyOffset = (historyOffset + 1) % kHistorySize;
        }
    };

    class Engine final : public EnginePipeline
    {
    public:
        std::shared_ptr<App>            appInstance_;
        std::shared_ptr<Globals>        globals_;
        std::shared_ptr<Camera>         camera_;

        Rendering                       renderEngine_;
        PlayerInput                     inputSystem_;
        ThreadDebugInfo                 threadDebugInfo_;
        ThreadRegistry                  threadRegistry_;
        ecs::Registry                   registry_;

    public:
        explicit Engine(std::shared_ptr<App> appInstance);
        ~Engine() override;

        void LoadGlobals(const std::string &fileName);
        void StoreGlobals(const std::string &fileName) const;
        void ConfigResource();

        // Engine pipeline
        void Init() override;
        void Update() override;
        void PrepareFrame() override;
        void PresentFrame() override;
        PlayerInput* GetInputSystem() override;
    };
} // namespace stnks
