#include <Net/UdpMarketFeed.hpp>
#include <spdlog/spdlog.h>
#include <cstring>
#include <ctime>

namespace stnks
{
    // ── ENet global init (reference counted by enet itself) ─────────────────────

    static struct ENetGlobalInit
    {
        ENetGlobalInit()
        {
            if (enet_initialize() != 0)
                spdlog::error("[Net] enet_initialize() failed");
        }
        ~ENetGlobalInit() { enet_deinitialize(); }
    } s_enetInit;

    // ════════════════════════════════════════════════════════════════════════════
    // MarketFeedServer
    // ════════════════════════════════════════════════════════════════════════════

    MarketFeedServer::MarketFeedServer(const NetFeedConfig& config)
        : config_(config)
    {}

    MarketFeedServer::~MarketFeedServer()
    {
        Stop();
    }

    void MarketFeedServer::Start()
    {
        if (running_.load()) return;

        ENetAddress address;
        address.host = ENET_HOST_ANY;
        address.port = config_.port;

        host_ = enet_host_create(&address, config_.maxClients, NET_CHANNEL_COUNT, 0, 0);
        if (!host_)
        {
            spdlog::error("[MarketFeedServer] Failed to create ENet host on port {}", config_.port);
            return;
        }

        running_ = true;
        spdlog::info("[MarketFeedServer] Listening on port {}", config_.port);
    }

    void MarketFeedServer::Stop()
    {
        running_ = false;
        if (host_)
        {
            enet_host_destroy(host_);
            host_ = nullptr;
        }
        clientCount_ = 0;
    }

    void MarketFeedServer::Poll()
    {
        if (!host_) return;

        ENetEvent event;
        while (enet_host_service(host_, &event, 0) > 0)
        {
            switch (event.type)
            {
            case ENET_EVENT_TYPE_CONNECT:
                HandleConnect(event.peer);
                break;
            case ENET_EVENT_TYPE_DISCONNECT:
                HandleDisconnect(event.peer);
                break;
            case ENET_EVENT_TYPE_RECEIVE:
                HandlePacket(event.peer, event.packet);
                enet_packet_destroy(event.packet);
                break;
            default:
                break;
            }
        }
    }

    void MarketFeedServer::HandleConnect(ENetPeer* peer)
    {
        clientCount_++;
        char ip[64];
        enet_address_get_host_ip(&peer->address, ip, sizeof(ip));
        spdlog::info("[MarketFeedServer] Client connected from {}:{} (total: {})",
                     ip, peer->address.port, clientCount_.load());
    }

    void MarketFeedServer::HandleDisconnect(ENetPeer* peer)
    {
        clientCount_--;
        spdlog::info("[MarketFeedServer] Client disconnected (total: {})", clientCount_.load());
    }

    void MarketFeedServer::HandlePacket(ENetPeer* peer, ENetPacket* packet)
    {
        if (packet->dataLength < 1) return;

        auto msgType = static_cast<NetMsgType>(packet->data[0]);

        if (msgType == NetMsgType::Heartbeat)
        {
            // Echo back heartbeat for latency measurement
            ENetPacket* reply = enet_packet_create(
                packet->data, packet->dataLength, ENET_PACKET_FLAG_RELIABLE);
            enet_peer_send(peer, CHAN_RELIABLE, reply);
        }
    }

    void MarketFeedServer::BroadcastTick(const std::string& symbol, const NetTickPayload& tick)
    {
        if (!host_ || clientCount_.load() == 0) return;

        // Build packet: header + symbol + payload
        uint8_t symbolLen = static_cast<uint8_t>(std::min(symbol.size(), size_t(255)));
        size_t packetSize = sizeof(NetTickPacket) + symbolLen + sizeof(NetTickPayload);

        std::vector<uint8_t> buf(packetSize);
        uint8_t* ptr = buf.data();

        NetTickPacket hdr;
        hdr.symbolLen = symbolLen;
        memcpy(ptr, &hdr, sizeof(hdr));
        ptr += sizeof(hdr);

        memcpy(ptr, symbol.c_str(), symbolLen);
        ptr += symbolLen;

        memcpy(ptr, &tick, sizeof(tick));

        ENetPacket* packet = enet_packet_create(
            buf.data(), buf.size(), ENET_PACKET_FLAG_UNSEQUENCED);
        enet_host_broadcast(host_, CHAN_MARKET, packet);
    }

    // ════════════════════════════════════════════════════════════════════════════
    // MarketFeedClient
    // ════════════════════════════════════════════════════════════════════════════

    MarketFeedClient::MarketFeedClient(const NetFeedConfig& config)
        : config_(config)
    {
        lastHeartbeat_ = std::chrono::steady_clock::now();
    }

    MarketFeedClient::~MarketFeedClient()
    {
        Disconnect();
    }

    void MarketFeedClient::Connect(const std::string& hostAddr, uint16_t port)
    {
        if (running_.load()) return;

        // Store connection params and launch background thread immediately
        // so DNS resolution + connect never blocks the UI thread.
        state_ = NetState::Connecting;
        connectStart_ = std::chrono::steady_clock::now();
        running_ = true;

        uint16_t resolvedPort = port > 0 ? port : config_.port;
        thread_ = std::thread(&MarketFeedClient::NetworkLoop, this, hostAddr, resolvedPort);
        spdlog::info("[MarketFeedClient] Connecting to {}:{} (async)", hostAddr, resolvedPort);
    }

    void MarketFeedClient::Disconnect()
    {
        running_ = false;
        if (thread_.joinable())
            thread_.join();

        if (server_ && host_)
        {
            enet_peer_disconnect_now(server_, 0);
            server_ = nullptr;
        }
        if (host_)
        {
            enet_host_destroy(host_);
            host_ = nullptr;
        }
        state_ = NetState::Offline;
        latencyMs_ = -1.f;
    }

    void MarketFeedClient::SetTickCallback(TickCallback cb)
    {
        std::lock_guard<std::mutex> lock(callbackMutex_);
        callback_ = std::move(cb);
    }

    void MarketFeedClient::NetworkLoop(std::string hostAddr, uint16_t port)
    {
        // Create ENet host + initiate connection (this does DNS, may take time)
        host_ = enet_host_create(nullptr, 1, NET_CHANNEL_COUNT, 0, 0);
        if (!host_)
        {
            spdlog::error("[MarketFeedClient] Failed to create ENet client host");
            state_ = NetState::Disconnected;
            return;
        }

        ENetAddress addr;
        if (enet_address_set_host(&addr, hostAddr.c_str()) != 0)
        {
            spdlog::error("[MarketFeedClient] DNS resolve failed for '{}'", hostAddr);
            enet_host_destroy(host_);
            host_ = nullptr;
            state_ = NetState::Disconnected;
            return;
        }
        addr.port = port;

        server_ = enet_host_connect(host_, &addr, NET_CHANNEL_COUNT, 0);
        if (!server_)
        {
            spdlog::error("[MarketFeedClient] No available peer slot");
            enet_host_destroy(host_);
            host_ = nullptr;
            state_ = NetState::Disconnected;
            return;
        }

        while (running_.load())
        {
            if (!host_) break;

            // Connection timeout
            if (state_.load() == NetState::Connecting)
            {
                auto now = std::chrono::steady_clock::now();
                double elapsed = std::chrono::duration<double>(now - connectStart_).count();
                if (elapsed > kConnectTimeoutSec)
                {
                    spdlog::warn("[MarketFeedClient] Connection timed out");
                    state_ = NetState::Disconnected;
                    break;
                }
            }

            // Send heartbeat every second when connected
            if (state_.load() == NetState::Connected)
            {
                auto now = std::chrono::steady_clock::now();
                double sinceLast = std::chrono::duration<double>(now - lastHeartbeat_).count();
                if (sinceLast >= 1.0)
                {
                    NetHeartbeatPacket hb;
                    hb.serverTime = std::chrono::duration_cast<std::chrono::milliseconds>(
                        now.time_since_epoch()).count();

                    ENetPacket* pkt = enet_packet_create(&hb, sizeof(hb), ENET_PACKET_FLAG_RELIABLE);
                    enet_peer_send(server_, CHAN_RELIABLE, pkt);
                    lastHeartbeat_ = now;
                }
            }

            ENetEvent event;
            // Service with 10ms timeout to keep loop responsive
            while (enet_host_service(host_, &event, 10) > 0)
            {
                switch (event.type)
                {
                case ENET_EVENT_TYPE_CONNECT:
                    state_ = NetState::Connected;
                    lastHeartbeat_ = std::chrono::steady_clock::now();
                    spdlog::info("[MarketFeedClient] Connected to server");
                    break;

                case ENET_EVENT_TYPE_DISCONNECT:
                    state_ = NetState::Disconnected;
                    server_ = nullptr;
                    latencyMs_ = -1.f;
                    spdlog::warn("[MarketFeedClient] Disconnected from server");
                    break;

                case ENET_EVENT_TYPE_RECEIVE:
                    HandlePacket(event.packet);
                    enet_packet_destroy(event.packet);
                    break;

                default:
                    break;
                }
            }
        }
    }

    void MarketFeedClient::HandlePacket(ENetPacket* packet)
    {
        if (packet->dataLength < 1) return;

        auto msgType = static_cast<NetMsgType>(packet->data[0]);

        switch (msgType)
        {
        case NetMsgType::Tick:
        {
            if (packet->dataLength < sizeof(NetTickPacket)) break;

            NetTickPacket hdr;
            memcpy(&hdr, packet->data, sizeof(hdr));

            size_t expectedMin = sizeof(NetTickPacket) + hdr.symbolLen + sizeof(NetTickPayload);
            if (packet->dataLength < expectedMin) break;

            std::string symbol(reinterpret_cast<const char*>(packet->data + sizeof(NetTickPacket)),
                               hdr.symbolLen);

            NetTickPayload payload;
            memcpy(&payload, packet->data + sizeof(NetTickPacket) + hdr.symbolLen, sizeof(payload));

            MarketTick tick;
            tick.symbol    = symbol;
            tick.price     = payload.price;
            tick.open      = payload.open;
            tick.high      = payload.high;
            tick.low       = payload.low;
            tick.volume    = payload.volume;
            tick.timestamp = payload.timestamp;

            {
                std::lock_guard<std::mutex> lock(tickMutex_);
                latestTicks_[symbol] = tick;
            }

            {
                std::lock_guard<std::mutex> lock(callbackMutex_);
                if (callback_)
                    callback_(tick);
            }
            break;
        }
        case NetMsgType::Heartbeat:
        {
            // Heartbeat echo — measure round-trip
            if (packet->dataLength >= sizeof(NetHeartbeatPacket))
            {
                NetHeartbeatPacket hb;
                memcpy(&hb, packet->data, sizeof(hb));

                auto now = std::chrono::steady_clock::now();
                int64_t nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                    now.time_since_epoch()).count();
                float rtt = static_cast<float>(nowMs - hb.serverTime);
                latencyMs_ = rtt;
            }
            break;
        }
        default:
            break;
        }
    }

    MarketTick MarketFeedClient::GetLatestTick(const std::string& symbol) const
    {
        std::lock_guard<std::mutex> lock(tickMutex_);
        auto it = latestTicks_.find(symbol);
        if (it != latestTicks_.end())
            return it->second;
        return {};
    }

    float MarketFeedClient::GetLivePrice(const std::string& symbol) const
    {
        std::lock_guard<std::mutex> lock(tickMutex_);
        auto it = latestTicks_.find(symbol);
        if (it != latestTicks_.end())
            return it->second.price;
        return 0.f;
    }

    float MarketFeedClient::GetTimeSinceLastHeartbeat() const
    {
        auto now = std::chrono::steady_clock::now();
        return std::chrono::duration<float>(now - lastHeartbeat_).count();
    }

} // namespace stnks
