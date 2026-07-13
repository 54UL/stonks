#pragma once

#include <Market/MarketData.hpp>
#include <enet/enet.h>

#include <string>
#include <vector>
#include <functional>
#include <thread>
#include <atomic>
#include <mutex>
#include <cstdint>
#include <chrono>
#include <unordered_map>

namespace stnks
{

    // Channel assignments
    enum NetChannel : uint8_t
    {
        CHAN_MARKET    = 0,  // Unreliable: real-time market ticks
        CHAN_RELIABLE  = 1,  // Reliable: heartbeat, control messages
        NET_CHANNEL_COUNT = 2
    };

    // Message types
    enum class NetMsgType : uint8_t
    {
        Tick         = 1,   // Market tick (server → client)
        Heartbeat    = 2,   // Connection alive (bidirectional)
        Subscribe    = 3,   // Client subscribes to symbol
        Unsubscribe  = 4,   // Client unsubscribes from symbol
    };

    // Binary tick packet (sent unreliable for speed)
    #pragma pack(push, 1)
    struct NetTickPacket
    {
        uint8_t msgType   = (uint8_t)NetMsgType::Tick;
        uint8_t symbolLen = 0;      // Followed by symbol chars + payload
    };

    struct NetTickPayload
    {
        float   price     = 0.f;
        float   open      = 0.f;
        float   high      = 0.f;
        float   low       = 0.f;
        float   volume    = 0.f;
        int64_t timestamp = 0;
    };

    struct NetHeartbeatPacket
    {
        uint8_t msgType   = (uint8_t)NetMsgType::Heartbeat;
        int64_t serverTime = 0;  // Server epoch ms
    };
    #pragma pack(pop)


    struct MarketTick
    {
        std::string symbol;
        float       price     = 0.f;
        float       open      = 0.f;
        float       high      = 0.f;
        float       low       = 0.f;
        float       volume    = 0.f;
        int64_t     timestamp = 0;
    };


    struct NetFeedConfig
    {
        uint16_t port = 8100;          // ENet port for market feed
        int      maxClients = 8;       // Max connected UI clients
    };


    enum class NetState
    {
        Offline,
        Connecting,
        Connected,
        Disconnected,
    };

    inline const char* NetStateToString(NetState s)
    {
        switch (s)
        {
        case NetState::Offline:      return "Offline";
        case NetState::Connecting:   return "Connecting";
        case NetState::Connected:    return "Connected";
        case NetState::Disconnected: return "Disconnected";
        }
        return "Unknown";
    }


    class MarketFeedServer
    {
    public:
        MarketFeedServer();
        explicit MarketFeedServer(const NetFeedConfig& config);
        ~MarketFeedServer();

        void Start();
        void Stop();
        bool IsRunning() const { return running_.load(); }

        // Poll ENet events (call from server's network thread or timer)
        void Poll();

        // Broadcast a tick to all connected clients (unreliable channel)
        void BroadcastTick(const std::string& symbol, const NetTickPayload& tick);

        // Get connected client count
        int GetClientCount() const { return clientCount_.load(); }

    private:
        void HandleConnect(ENetPeer* peer);
        void HandleDisconnect(ENetPeer* peer);
        void HandlePacket(ENetPeer* peer, ENetPacket* packet);

        NetFeedConfig       config_;
        ENetHost*           host_ = nullptr;
        std::atomic<bool>   running_{false};
        std::atomic<int>    clientCount_{0};
    };


    class MarketFeedClient
    {
    public:
        using TickCallback = std::function<void(const MarketTick&)>;

        MarketFeedClient();
        explicit MarketFeedClient(const NetFeedConfig& config);
        ~MarketFeedClient();

        // Connect to server, start recv thread
        void Connect(const std::string& host, uint16_t port = 0);
        void Disconnect();

        NetState GetState() const { return state_.load(); }
        bool IsConnected() const { return state_.load() == NetState::Connected; }

        // Latency from last heartbeat round-trip (ms), -1 if disconnected
        float GetLatencyMs() const { return latencyMs_.load(); }

        // Get latest tick for a symbol (thread-safe)
        MarketTick GetLatestTick(const std::string& symbol) const;
        float GetLivePrice(const std::string& symbol) const;

        // Set callback for incoming ticks
        void SetTickCallback(TickCallback cb);

        // Time since last successful heartbeat (seconds)
        float GetTimeSinceLastHeartbeat() const;

        // Reconnection state
        int GetReconnectAttempts() const { return reconnectAttempts_; }

    private:
        void NetworkLoop(std::string hostAddr, uint16_t port);
        void HandlePacket(ENetPacket* packet);

        NetFeedConfig       config_;
        ENetHost*           host_ = nullptr;
        ENetPeer*           server_ = nullptr;
        std::thread         thread_;
        std::atomic<bool>   running_{false};
        std::atomic<NetState> state_{NetState::Offline};
        std::atomic<float>  latencyMs_{-1.f};

        std::chrono::steady_clock::time_point lastHeartbeat_;      // Last received echo
        std::chrono::steady_clock::time_point lastHeartbeatSent_;  // Last sent heartbeat
        std::chrono::steady_clock::time_point connectStart_;

        mutable std::mutex                          tickMutex_;
        std::unordered_map<std::string, MarketTick> latestTicks_;

        mutable std::mutex  callbackMutex_;
        TickCallback        callback_;

        static constexpr double kConnectTimeoutSec = 5.0;
        static constexpr double kReconnectDelaySec = 5.0;
        static constexpr double kHeartbeatTimeoutSec = 10.0; // Dead if no heartbeat echo for this long

        // Connection params stored for reconnect
        std::string savedHost_;
        uint16_t    savedPort_ = 0;
        int         reconnectAttempts_ = 0;

        void AttemptReconnect();
        void CleanupConnection();
    };

} // namespace stnks
