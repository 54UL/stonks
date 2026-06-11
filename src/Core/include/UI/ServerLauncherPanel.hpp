#pragma once

#include <UI/UIContext.hpp>
#include <imgui.h>

#include <string>
#include <thread>
#include <atomic>
#include <mutex>
#include <vector>
#include <cstdint>

namespace stnks
{
    class ServerLauncherPanel
    {
    public:
        explicit ServerLauncherPanel(UIContext& ctx);
        ~ServerLauncherPanel();

        void Draw(bool* open);

        bool IsRunning() const { return running_.load(); }
        void StopServer();

    private:
        UIContext& ctx_;

        // Server config (editable in UI)
        // Per-broker credential block
        struct BrokerEntry
        {
            bool enabled = false;
            bool sandbox = true;
            char apiKey[256]    = "";
            char apiSecret[256] = "";
            char accountId[128] = "";
            // OAuth2 (GBM)
            char clientId[128]     = "";
            char clientSecret[256] = "";
            char refreshToken[256] = "";
        };

        struct ServerConfig
        {
            char host[64]       = "0.0.0.0";
            int  port           = 8099;
            int  feedPort       = 8100;
            int  pollInterval   = 60;
            int  sentimentInterval = 600;
            char dbName[128]    = "strategies.db";
            char logPath[256]   = "app/stnks-server.log";

            // Brokers (index matches BrokerSource: 1=Binance, 2=GBM, 3=MetaTrader)
            BrokerEntry brokers[3] = {};

            // AI / News (read from env on init)
            char gnewsKey[128]  = "";
            char claudeKey[128] = "";
        };

        ServerConfig config_;

        // Process management
        std::atomic<bool>   running_{false};
        std::thread         processThread_;
        std::mutex          logMutex_;
        std::vector<std::string> logLines_;
        int                 exitCode_ = -1;

#ifdef _WIN32
        void*               processHandle_ = nullptr;  // HANDLE
        unsigned long        processPid_    = 0;        // DWORD
#else
        pid_t               pid_ = -1;
#endif

        // Resolved binary path (found at construction)
        std::string serverBinaryPath_;

        void LaunchServer();
        void ReadProcessOutput();
        std::string BuildCommandLine() const;
        std::string FindServerBinary() const;

        void DrawConfig();
        void DrawStatus();
        void DrawBrokers();
        void DrawLog();

        // Log filter: 0=All, 1=Info, 2=Warn, 3=Error, 4=Launcher
        int logFilter_ = 0;

        bool PassesLogFilter(const std::string& line) const;

        static constexpr int kMaxLogLines = 500;
    };

} // namespace stnks
