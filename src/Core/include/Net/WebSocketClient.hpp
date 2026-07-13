#pragma once

#include <Net/IWebSocketClient.hpp>
#include <mutex>
#include <atomic>
#include <memory>

// Forward-declare ixwebsocket types to avoid header pollution
namespace ix { class WebSocket; }

namespace stnks
{
    // Concrete WebSocket client using ixwebsocket library.
    //
    // Features:
    //   - TLS/SSL support (wss://)
    //   - Automatic reconnection with exponential backoff
    //   - Per-message deflate compression
    //   - Built-in ping/pong heartbeat
    //   - Thread-safe Send from any thread
    //
    // Usage:
    //   WebSocketClient ws;
    //   ws.SetOnMessage([](const std::string& msg, WsMessageType) { ... });
    //   ws.Connect({.url = "wss://stream.broker.com/v1/prices"});
    //   ws.SendText(R"({"subscribe": "AAPL"})");
    //   // ... later ...
    //   ws.Disconnect();
    class WebSocketClient : public IWebSocketClient
    {
    public:
        WebSocketClient();
        ~WebSocketClient() override;

        // Non-copyable, movable
        WebSocketClient(const WebSocketClient&) = delete;
        WebSocketClient& operator=(const WebSocketClient&) = delete;


        void Connect(const WsConfig& config) override;
        void Disconnect() override;
        WsState GetState() const override;

        void SendText(const std::string& message) override;
        void SendBinary(const std::string& data) override;

        void SetOnMessage(MessageCallback cb) override;
        void SetOnStateChange(StateCallback cb) override;
        void SetOnError(ErrorCallback cb) override;

        float GetLatencyMs() const override;
        uint64_t GetMessagesReceived() const override;

    private:
        std::unique_ptr<ix::WebSocket> ws_;
        std::atomic<WsState>           state_{WsState::Disconnected};
        std::atomic<float>             latencyMs_{0.f};
        std::atomic<uint64_t>          messagesReceived_{0};

        // Callbacks (set before Connect, read from network thread)
        std::mutex          cbMutex_;
        MessageCallback     onMessage_;
        StateCallback       onStateChange_;
        ErrorCallback       onError_;

        void SetState(WsState s);
    };

} // namespace stnks
