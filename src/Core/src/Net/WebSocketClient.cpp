#include <Net/WebSocketClient.hpp>
#include <ixwebsocket/IXWebSocket.h>
#include <spdlog/spdlog.h>

namespace stnks
{
    WebSocketClient::WebSocketClient()
        : ws_(std::make_unique<ix::WebSocket>())
    {
    }

    WebSocketClient::~WebSocketClient()
    {
        Disconnect();
    }

    void WebSocketClient::Connect(const WsConfig& config)
    {
        Disconnect();

        ws_->setUrl(config.url);

        // Set sub-protocol if specified
        if (!config.protocol.empty())
            ws_->addSubProtocol(config.protocol);

        // Set extra headers (auth tokens, API keys)
        ix::WebSocketHttpHeaders headers;
        for (auto& [key, val] : config.headers)
            headers[key] = val;
        ws_->setExtraHeaders(headers);

        // Ping/pong heartbeat
        ws_->setPingInterval(config.pingIntervalSec);

        // Auto-reconnect
        if (config.autoReconnect)
        {
            ws_->enableAutomaticReconnection();
            ws_->setMinWaitBetweenReconnectionRetries(config.reconnectDelayMs);
            ws_->setMaxWaitBetweenReconnectionRetries(config.reconnectMaxMs);
        }
        else
        {
            ws_->disableAutomaticReconnection();
        }

        // Handshake timeout
        ws_->setHandshakeTimeout(config.handshakeTimeoutMs);

        // Per-message deflate compression
        ws_->enablePerMessageDeflate();

        // Message handler
        ws_->setOnMessageCallback([this](const ix::WebSocketMessagePtr& msg)
        {
            switch (msg->type)
            {
                case ix::WebSocketMessageType::Open:
                {
                    SetState(WsState::Connected);
                    spdlog::info("[WebSocket] Connected to {}", msg->openInfo.uri);
                    break;
                }
                case ix::WebSocketMessageType::Close:
                {
                    SetState(WsState::Disconnected);
                    spdlog::info("[WebSocket] Closed: {} ({})",
                        msg->closeInfo.reason, msg->closeInfo.code);
                    break;
                }
                case ix::WebSocketMessageType::Error:
                {
                    SetState(WsState::Error);
                    spdlog::error("[WebSocket] Error: {} (HTTP {})",
                        msg->errorInfo.reason, msg->errorInfo.http_status);

                    std::lock_guard<std::mutex> lock(cbMutex_);
                    if (onError_)
                        onError_(msg->errorInfo.reason, msg->errorInfo.http_status);
                    break;
                }
                case ix::WebSocketMessageType::Message:
                {
                    messagesReceived_.fetch_add(1, std::memory_order_relaxed);

                    WsMessageType type = msg->binary
                        ? WsMessageType::Binary
                        : WsMessageType::Text;

                    std::lock_guard<std::mutex> lock(cbMutex_);
                    if (onMessage_)
                        onMessage_(msg->str, type);
                    break;
                }
                case ix::WebSocketMessageType::Ping:
                {
                    // ixwebsocket auto-responds with pong
                    break;
                }
                case ix::WebSocketMessageType::Pong:
                {
                    // Compute latency from ping timestamp
                    // (ixwebsocket doesn't expose this directly; approximate)
                    break;
                }
                case ix::WebSocketMessageType::Fragment:
                    break;
            }
        });

        SetState(WsState::Connecting);
        ws_->start();
    }

    void WebSocketClient::Disconnect()
    {
        if (state_.load() == WsState::Disconnected)
            return;

        ws_->stop();
        SetState(WsState::Disconnected);
        messagesReceived_.store(0, std::memory_order_relaxed);
    }

    WsState WebSocketClient::GetState() const
    {
        return state_.load(std::memory_order_acquire);
    }

    void WebSocketClient::SendText(const std::string& message)
    {
        if (state_.load() != WsState::Connected)
            return;
        ws_->sendText(message);
    }

    void WebSocketClient::SendBinary(const std::string& data)
    {
        if (state_.load() != WsState::Connected)
            return;
        ws_->sendBinary(data);
    }

    void WebSocketClient::SetOnMessage(MessageCallback cb)
    {
        std::lock_guard<std::mutex> lock(cbMutex_);
        onMessage_ = std::move(cb);
    }

    void WebSocketClient::SetOnStateChange(StateCallback cb)
    {
        std::lock_guard<std::mutex> lock(cbMutex_);
        onStateChange_ = std::move(cb);
    }

    void WebSocketClient::SetOnError(ErrorCallback cb)
    {
        std::lock_guard<std::mutex> lock(cbMutex_);
        onError_ = std::move(cb);
    }

    float WebSocketClient::GetLatencyMs() const
    {
        return latencyMs_.load(std::memory_order_relaxed);
    }

    uint64_t WebSocketClient::GetMessagesReceived() const
    {
        return messagesReceived_.load(std::memory_order_relaxed);
    }

    void WebSocketClient::SetState(WsState s)
    {
        state_.store(s, std::memory_order_release);

        std::lock_guard<std::mutex> lock(cbMutex_);
        if (onStateChange_)
            onStateChange_(s);
    }

} // namespace stnks
