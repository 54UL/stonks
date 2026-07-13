#pragma once

#include <string>
#include <functional>
#include <cstdint>

namespace stnks
{
    // WebSocket connection states
    enum class WsState
    {
        Disconnected,
        Connecting,
        Connected,
        Reconnecting,
        Error
    };

    inline const char* WsStateToString(WsState s)
    {
        switch (s)
        {
            case WsState::Disconnected: return "Disconnected";
            case WsState::Connecting:   return "Connecting";
            case WsState::Connected:    return "Connected";
            case WsState::Reconnecting: return "Reconnecting";
            case WsState::Error:        return "Error";
        }
        return "Unknown";
    }

    // WebSocket message types
    enum class WsMessageType
    {
        Text,
        Binary,
        Ping,
        Pong
    };

    // Configuration for WebSocket connections
    struct WsConfig
    {
        std::string url;                        // wss://... or ws://...
        std::string protocol;                   // Sub-protocol (optional)
        int         pingIntervalSec   = 30;     // Heartbeat ping interval
        int         pingTimeoutSec    = 10;     // Pong timeout before disconnect
        bool        autoReconnect     = true;   // Auto-reconnect on drop
        int         reconnectDelayMs  = 2000;   // Backoff initial delay
        int         reconnectMaxMs    = 30000;  // Max backoff delay
        int         handshakeTimeoutMs = 5000;  // TLS/HTTP upgrade timeout

        // Optional auth headers (Bearer token, API key, etc.)
        std::vector<std::pair<std::string, std::string>> headers;
    };

    // Abstract WebSocket client interface.
    // Implementations wrap platform-specific libraries (ixwebsocket, Beast, etc.)
    //
    // Threading model:
    //   - Connect/Disconnect/Send: call from any thread
    //   - Callbacks (OnMessage, etc.): invoked from the WebSocket network thread
    //   - Callers must handle thread-safety in callbacks
    class IWebSocketClient
    {
    public:
        virtual ~IWebSocketClient() = default;


        virtual void Connect(const WsConfig& config) = 0;
        virtual void Disconnect() = 0;
        virtual WsState GetState() const = 0;
        virtual bool IsConnected() const { return GetState() == WsState::Connected; }


        virtual void SendText(const std::string& message) = 0;
        virtual void SendBinary(const std::string& data) = 0;


        using MessageCallback    = std::function<void(const std::string& data, WsMessageType type)>;
        using StateCallback      = std::function<void(WsState newState)>;
        using ErrorCallback      = std::function<void(const std::string& reason, int code)>;

        virtual void SetOnMessage(MessageCallback cb) = 0;
        virtual void SetOnStateChange(StateCallback cb) = 0;
        virtual void SetOnError(ErrorCallback cb) = 0;


        // Round-trip latency of last ping/pong in milliseconds
        virtual float GetLatencyMs() const { return 0.f; }

        // Number of messages received since last connect
        virtual uint64_t GetMessagesReceived() const { return 0; }
    };

} // namespace stnks
