#pragma once

#include <UI/UIContext.hpp>
#include <imgui.h>
#include <unordered_map>
#include <string>

namespace stnks
{
    class DashboardPanel
    {
    public:
        explicit DashboardPanel(UIContext& ctx) : ctx_(ctx) {}
        void Draw(bool* open);

    private:
        UIContext& ctx_;

        void DrawConnectionStatus();
        void DrawRemoteConnect();
        void DrawPerformance();
        void DrawTelemetry();
        void DrawStrategiesSummary();
        void DrawSystems();
        void DrawDataConfig();
        void DrawEnvironment();

        // Remote connection UI state
        char remoteAddress_[128] = "localhost:8099";
        bool showConnectPanel_   = false;

        // Env var edit buffers (key → fixed-size char buffer)
        struct EnvEditBuf { char data[256] = {}; };
        std::unordered_map<std::string, EnvEditBuf> envEditBuffers_;
        bool envBuffersInit_ = false;
    };

} // namespace stnks
