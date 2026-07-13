#include <UI/ServerLauncherPanel.hpp>
#include <UI/UIConstants.hpp>
#include <imgui.h>
#include <portable-file-dialogs.h>
#include <spdlog/spdlog.h>
#include <filesystem>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <array>

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#else
#  include <unistd.h>
#  include <sys/wait.h>
#  include <signal.h>
#  include <fcntl.h>
#endif

namespace fs = std::filesystem;

namespace stnks
{
    ServerLauncherPanel::ServerLauncherPanel(UIContext& ctx)
        : ctx_(ctx)
    {
        serverBinaryPath_ = FindServerBinary();

        // Pre-fill API keys from environment
        const char* gnews  = std::getenv("GNEWS_API_KEY");
        const char* claude = std::getenv("CLAUDE_API_KEY");
        if (gnews)  strncpy(config_.gnewsKey,  gnews,  sizeof(config_.gnewsKey)  - 1);
        if (claude) strncpy(config_.claudeKey, claude, sizeof(config_.claudeKey) - 1);

        // Pre-fill broker credentials from environment
        auto& bin = config_.brokers[0]; // Binance
        const char* binKey    = std::getenv("BINANCE_API_KEY");
        const char* binSecret = std::getenv("BINANCE_API_SECRET");
        const char* binSbox   = std::getenv("BINANCE_SANDBOX");
        if (binKey)    { strncpy(bin.apiKey,    binKey,    sizeof(bin.apiKey) - 1);    bin.enabled = true; }
        if (binSecret) strncpy(bin.apiSecret, binSecret, sizeof(bin.apiSecret) - 1);
        if (binSbox && std::string(binSbox) == "0") bin.sandbox = false;

        auto& mt5 = config_.brokers[1]; // MetaTrader
        const char* mtKey  = std::getenv("MT5_API_KEY");
        const char* mtAcct = std::getenv("MT5_ACCOUNT_ID");
        if (mtKey)  { strncpy(mt5.apiKey,    mtKey,  sizeof(mt5.apiKey) - 1);    mt5.enabled = true; }
        if (mtAcct) strncpy(mt5.accountId, mtAcct, sizeof(mt5.accountId) - 1);
    }

    ServerLauncherPanel::~ServerLauncherPanel()
    {
        StopServer();
        if (processThread_.joinable())
            processThread_.join();
    }

    std::string ServerLauncherPanel::FindServerBinary() const
    {
        // Search common build locations relative to the running executable
        std::vector<std::string> candidates;

#ifdef _WIN32
        char exeBuf[512] = {};
        if (GetModuleFileNameA(nullptr, exeBuf, sizeof(exeBuf)))
        {
            fs::path exeDir = fs::path(exeBuf).parent_path();
            candidates.push_back((exeDir / "stnks-server.exe").string());
            candidates.push_back((exeDir / ".." / "Server" / "stnks-server.exe").string());
            candidates.push_back((exeDir / ".." / "Server" / "Debug" / "stnks-server.exe").string());
            candidates.push_back((exeDir / ".." / "Server" / "Release" / "stnks-server.exe").string());
        }
        candidates.push_back("stnks-server.exe");
#else
        char exeBuf[512] = {};
        ssize_t len = ::readlink("/proc/self/exe", exeBuf, sizeof(exeBuf) - 1);
        if (len > 0)
        {
            exeBuf[len] = '\0';
            fs::path exeDir = fs::path(exeBuf).parent_path();
            candidates.push_back((exeDir / "stnks-server").string());
            candidates.push_back((exeDir / ".." / "Server" / "stnks-server").string());
        }
        candidates.push_back("stnks-server");
#endif

        for (auto& c : candidates)
        {
            if (fs::exists(c))
            {
                spdlog::debug("[ServerLauncher] Found binary: {}", c);
                return fs::canonical(c).string();
            }
        }
        spdlog::warn("[ServerLauncher] Could not find stnks-server binary");
        return "";
    }

    std::string ServerLauncherPanel::BuildCommandLine() const
    {
        std::string cmd;
        if (serverBinaryPath_.empty())
            return cmd;

        cmd = "\"" + serverBinaryPath_ + "\"";
        cmd += " --host " + std::string(config_.host);
        cmd += " --port " + std::to_string(config_.port);
        cmd += " --interval " + std::to_string(config_.pollInterval);
        cmd += " --sentiment-interval " + std::to_string(config_.sentimentInterval);
        cmd += " --feed-port " + std::to_string(config_.feedPort);
        cmd += " --db \"" + std::string(config_.dbName) + "\"";
        cmd += " --log \"" + std::string(config_.logPath) + "\"";

        // Broker env vars (passed via command environment, not CLI args)
        // Binance
        auto& bin = config_.brokers[0];
        if (bin.enabled && bin.apiKey[0])
            cmd += " --env BINANCE_API_KEY=\"" + std::string(bin.apiKey) + "\"";
        if (bin.enabled && bin.apiSecret[0])
            cmd += " --env BINANCE_API_SECRET=\"" + std::string(bin.apiSecret) + "\"";
        if (bin.enabled)
            cmd += " --env BINANCE_SANDBOX=" + std::string(bin.sandbox ? "1" : "0");
        // MetaTrader
        auto& mt5 = config_.brokers[1];
        if (mt5.enabled && mt5.apiKey[0])
            cmd += " --env MT5_API_KEY=\"" + std::string(mt5.apiKey) + "\"";
        if (mt5.enabled && mt5.accountId[0])
            cmd += " --env MT5_ACCOUNT_ID=\"" + std::string(mt5.accountId) + "\"";

        if (config_.gnewsKey[0])
            cmd += " --gnews-key \"" + std::string(config_.gnewsKey) + "\"";
        if (config_.claudeKey[0])
            cmd += " --claude-key \"" + std::string(config_.claudeKey) + "\"";

        return cmd;
    }

    void ServerLauncherPanel::LaunchServer()
    {
        if (running_.load()) return;
        if (serverBinaryPath_.empty())
        {
            ctx_.PushToast("Server binary not found", ui::kToastError);
            return;
        }

        std::string cmd = BuildCommandLine();

        {
            std::lock_guard<std::mutex> lock(logMutex_);
            logLines_.clear();
            logLines_.push_back("[launcher] Starting: " + cmd);
        }

        running_.store(true);
        exitCode_ = -1;

        if (processThread_.joinable())
            processThread_.join();

        processThread_ = std::thread([this, cmd]() {
#ifdef _WIN32
            STARTUPINFOA si{};
            si.cb = sizeof(si);
            // No pipe redirection — let the console window handle output

            PROCESS_INFORMATION pi{};
            std::string cmdMut = cmd;

            // Launch in its own console window with a title
            std::string titleEnv = "title STNKS Server && " + cmd;
            std::string shellCmd = "cmd.exe /K \"" + titleEnv + "\"";
            std::string shellMut = shellCmd;

            BOOL ok = CreateProcessA(
                nullptr, shellMut.data(), nullptr, nullptr, FALSE,
                CREATE_NEW_CONSOLE, nullptr, nullptr, &si, &pi);

            if (!ok)
            {
                std::lock_guard<std::mutex> lock(logMutex_);
                logLines_.push_back("[launcher] Failed to start process (error " + std::to_string(GetLastError()) + ")");
                running_.store(false);
                return;
            }

            {
                std::lock_guard<std::mutex> lock(logMutex_);
                logLines_.push_back("[launcher] Server launched in external console (PID " + std::to_string(pi.dwProcessId) + ")");
            }

            processHandle_ = pi.hProcess;
            processPid_    = pi.dwProcessId;
            CloseHandle(pi.hThread);

            // Wait for process to exit
            WaitForSingleObject(pi.hProcess, INFINITE);

            DWORD ec = 0;
            GetExitCodeProcess(pi.hProcess, &ec);
            exitCode_ = (int)ec;
            CloseHandle(pi.hProcess);
            processHandle_ = nullptr;
            processPid_    = 0;
#else
            // Launch in a new terminal emulator
            std::string termCmd;
            // Try common terminal emulators
            if (system("which gnome-terminal > /dev/null 2>&1") == 0)
                termCmd = "gnome-terminal -- bash -c '" + cmd + "; echo \"[Press Enter to close]\"; read' &";
            else if (system("which xterm > /dev/null 2>&1") == 0)
                termCmd = "xterm -hold -e " + cmd + " &";
            else
            {
                // Fallback: run in background, no terminal
                termCmd = cmd;
            }

            int ret = system(termCmd.c_str());
            exitCode_ = ret;
#endif
            running_.store(false);
            std::lock_guard<std::mutex> lock(logMutex_);
            logLines_.push_back("[launcher] Server exited (code " + std::to_string(exitCode_) + ")");
        });
    }

    void ServerLauncherPanel::StopServer()
    {
        if (!running_.load()) return;

#ifdef _WIN32
        // Kill the entire process tree (cmd.exe + stnks-server.exe)
        if (processPid_ > 0)
        {
            std::string killCmd = "taskkill /T /F /PID " + std::to_string(processPid_);
            system(killCmd.c_str());
        }
        else if (processHandle_)
        {
            TerminateProcess((HANDLE)processHandle_, 0);
        }
#else
        if (pid_ > 0)
            kill(pid_, SIGTERM);
#endif
    }


    void ServerLauncherPanel::Draw(bool* open)
    {
        if (!ImGui::Begin("Server Launcher", open)) { ImGui::End(); return; }

        DrawStatus();
        ImGui::Separator();
        DrawConfig();
        ImGui::Separator();
        DrawBrokers();
        ImGui::Separator();
        DrawLog();

        ImGui::End();
    }

    void ServerLauncherPanel::DrawStatus()
    {
        bool isRunning = running_.load();

        if (isRunning)
        {
            ImGui::TextColored(ui::kColorConnected, "RUNNING");
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.15f, 0.15f, 1.f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.85f, 0.2f, 0.2f, 1.f));
            if (ImGui::Button("Stop Server"))
            {
                StopServer();
                ctx_.PushToast("Stopping server...", ui::kToastWarning);
            }
            ImGui::PopStyleColor(2);
        }
        else
        {
            if (exitCode_ >= 0)
                ImGui::TextColored(ui::kColorDisconnected, "STOPPED (exit %d)", exitCode_);
            else
                ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.f), "NOT RUNNING");

            ImGui::SameLine();

            bool canLaunch = !serverBinaryPath_.empty();
            if (!canLaunch) ImGui::BeginDisabled();

            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.55f, 0.3f, 1.f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.2f, 0.65f, 0.4f, 1.f));
            if (ImGui::Button("Launch Server"))
            {
                LaunchServer();
                ctx_.PushToast("Launching server...", ui::kToastInfo);
            }
            ImGui::PopStyleColor(2);

            if (!canLaunch)
            {
                ImGui::EndDisabled();
                ImGui::SameLine();
                ImGui::TextColored(ui::kColorBearish, "Binary not found");
            }
        }

        if (!serverBinaryPath_.empty())
        {
            ImGui::SameLine();
            ImGui::TextDisabled("(%s)", serverBinaryPath_.c_str());
        }

        if (!running_.load())
        {
            ImGui::SameLine();
            if (ImGui::SmallButton("Browse..."))
            {
                auto dlg = pfd::open_file("Select Server Binary", serverBinaryPath_,
#ifdef _WIN32
                    {"Executable", "*.exe", "All Files", "*"});
#else
                    {"All Files", "*"});
#endif
                auto paths = dlg.result();
                if (!paths.empty())
                    serverBinaryPath_ = paths[0];
            }
        }
    }

    void ServerLauncherPanel::DrawConfig()
    {
        bool isRunning = running_.load();
        if (isRunning) ImGui::BeginDisabled();

        if (ImGui::CollapsingHeader("Server Configuration", ImGuiTreeNodeFlags_DefaultOpen))
        {
            float w = ImGui::CalcItemWidth();

            ImGui::TextDisabled("Network");
            ImGui::SetNextItemWidth(w * 0.6f);
            ImGui::InputText("Host", config_.host, sizeof(config_.host));
            ImGui::SameLine();
            ImGui::SetNextItemWidth(w * 0.25f);
            ImGui::InputInt("HTTP Port", &config_.port, 0);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(w * 0.25f);
            ImGui::InputInt("Feed Port", &config_.feedPort, 0);

            ImGui::Spacing();
            ImGui::TextDisabled("Intervals");
            ImGui::SetNextItemWidth(w * 0.4f);
            ImGui::InputInt("Poll (sec)", &config_.pollInterval, 10);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(w * 0.4f);
            ImGui::InputInt("Sentiment (sec)", &config_.sentimentInterval, 60);

            ImGui::Spacing();
            ImGui::TextDisabled("Storage");
            ImGui::InputText("Database", config_.dbName, sizeof(config_.dbName));
            ImGui::SameLine();
            if (ImGui::SmallButton("...##db"))
            {
                auto dlg = pfd::save_file("Select Database File", config_.dbName,
                    {"SQLite Files", "*.db *.sqlite", "All Files", "*"});
                std::string r = dlg.result();
                if (!r.empty()) strncpy(config_.dbName, r.c_str(), sizeof(config_.dbName) - 1);
            }
            ImGui::InputText("Log File", config_.logPath, sizeof(config_.logPath));
            ImGui::SameLine();
            if (ImGui::SmallButton("...##log"))
            {
                auto dlg = pfd::save_file("Select Log File", config_.logPath,
                    {"Log Files", "*.log *.txt", "All Files", "*"});
                std::string r = dlg.result();
                if (!r.empty()) strncpy(config_.logPath, r.c_str(), sizeof(config_.logPath) - 1);
            }

            ImGui::Spacing();
            ImGui::TextDisabled("AI & News (from env if empty)");
            ImGui::InputText("GNews Key", config_.gnewsKey, sizeof(config_.gnewsKey),
                             config_.gnewsKey[0] ? ImGuiInputTextFlags_Password : 0);
            ImGui::InputText("Claude Key", config_.claudeKey, sizeof(config_.claudeKey),
                             config_.claudeKey[0] ? ImGuiInputTextFlags_Password : 0);

            // Show computed command line
            if (ImGui::TreeNode("Command Preview"))
            {
                std::string cmd = BuildCommandLine();
                ImGui::TextWrapped("%s", cmd.c_str());
                if (ImGui::SmallButton("Copy"))
                    ImGui::SetClipboardText(cmd.c_str());
                ImGui::TreePop();
            }
        }

        if (isRunning) ImGui::EndDisabled();
    }

    void ServerLauncherPanel::DrawBrokers()
    {
        if (!ImGui::CollapsingHeader("Broker Sources", ImGuiTreeNodeFlags_DefaultOpen))
            return;

        bool isRunning = running_.load();
        if (isRunning) ImGui::BeginDisabled();

        ImGui::TextDisabled("Configure broker connections. All brokers run in dry-run mode.");
        ImGui::Spacing();

        struct BrokerMeta { const char* name; const char* icon; ImVec4 color; };
        static const BrokerMeta meta[] = {
            {"Binance",    "[B]", {0.96f, 0.76f, 0.07f, 1.f}},
            {"MetaTrader", "[M]", {0.30f, 0.75f, 0.40f, 1.f}},
        };

        for (int i = 0; i < 2; ++i)
        {
            auto& broker = config_.brokers[i];
            auto& m = meta[i];

            ImGui::PushID(i);

            // Colored icon + name + enable toggle
            ImGui::TextColored(m.color, "%s", m.icon);
            ImGui::SameLine();
            ImGui::Checkbox(m.name, &broker.enabled);
            ImGui::SameLine();
            ImGui::Checkbox("Sandbox", &broker.sandbox);

            if (broker.enabled)
            {
                ImGui::Indent(24.f);

                // All brokers use API Key auth
                ImGui::InputText("API Key", broker.apiKey, sizeof(broker.apiKey),
                                 broker.apiKey[0] ? ImGuiInputTextFlags_Password : 0);

                if (i == 0) // Binance also has API Secret
                {
                    ImGui::InputText("API Secret", broker.apiSecret, sizeof(broker.apiSecret),
                                     broker.apiSecret[0] ? ImGuiInputTextFlags_Password : 0);
                }

                if (i == 2) // MetaTrader also has account ID
                    ImGui::InputText("Account ID", broker.accountId, sizeof(broker.accountId));

                ImGui::Unindent(24.f);
            }

            ImGui::PopID();

            if (i < 2) ImGui::Spacing();
        }

        // Summary: which brokers are active
        ImGui::Spacing();
        ImGui::TextDisabled("Symbol resolution: enabled brokers > Yahoo Finance fallback");
        int enabledCount = 0;
        for (int i = 0; i < 3; ++i)
            if (config_.brokers[i].enabled) enabledCount++;

        if (enabledCount == 0)
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.f), "No brokers configured — using Yahoo Finance only");
        else
        {
            ImGui::SameLine();
            ImGui::Text("(%d active)", enabledCount);
        }

        if (isRunning) ImGui::EndDisabled();
    }

    bool ServerLauncherPanel::PassesLogFilter(const std::string& line) const
    {
        switch (logFilter_)
        {
        case 1: // Info — show info + launcher
            return line.find("[info]") != std::string::npos ||
                   line.find("[launcher]") != std::string::npos;
        case 2: // Warn
            return line.find("[warn") != std::string::npos;
        case 3: // Error
            return line.find("[error]") != std::string::npos ||
                   line.find("ERROR") != std::string::npos ||
                   line.find("Failed") != std::string::npos;
        case 4: // Launcher
            return line.find("[launcher]") != std::string::npos;
        default: return true; // All
        }
    }

    void ServerLauncherPanel::DrawLog()
    {
        // Header with filter on the same line
        bool open = ImGui::CollapsingHeader("Server Output", ImGuiTreeNodeFlags_DefaultOpen);

        if (open)
        {
            // Log level filter + clear button
            ImGui::SetNextItemWidth(100.f);
            const char* filterLabels[] = {"All", "Info", "Warn", "Error", "Launcher"};
            ImGui::Combo("##LogFilter", &logFilter_, filterLabels, 5);
            ImGui::SameLine();
            if (ImGui::SmallButton("Clear"))
            {
                std::lock_guard<std::mutex> lock(logMutex_);
                logLines_.clear();
            }

            std::lock_guard<std::mutex> lock(logMutex_);

            if (logLines_.empty())
            {
                ImGui::TextDisabled("No output yet.");
                return;
            }

            // Fill all remaining vertical space
            float logHeight = ImGui::GetContentRegionAvail().y;
            if (logHeight < 60.f) logHeight = 60.f;

            ImGui::BeginChild("##ServerLog", ImVec2(0, logHeight), true);
            for (auto& line : logLines_)
            {
                if (!PassesLogFilter(line)) continue;

                // Color server log lines
                if (line.find("[error]") != std::string::npos ||
                    line.find("ERROR") != std::string::npos ||
                    line.find("Failed") != std::string::npos)
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 0.3f, 0.3f, 1.f));
                else if (line.find("[warn") != std::string::npos)
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 0.8f, 0.3f, 1.f));
                else if (line.find("[launcher]") != std::string::npos)
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.7f, 1.f, 1.f));
                else
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.75f, 0.75f, 0.8f, 1.f));

                ImGui::TextUnformatted(line.c_str());
                ImGui::PopStyleColor();
            }
            if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 20.f)
                ImGui::SetScrollHereY(1.f);
            ImGui::EndChild();
        }
    }

} // namespace stnks
