#pragma once

#include <Market/IBrokerDataSource.hpp>
#include <Net/IWebSocketClient.hpp>
#include <Strategy/Strategy.hpp>
#include <string>
#include <vector>
#include <functional>
#include <cstdint>

namespace stnks
{
    // ── Broker Data Types ─────────────────────────────────────────────────────────

    enum class OrderSide { Buy, Sell };
    enum class OrderType { Market, Limit, Stop, StopLimit };
    enum class OrderStatus { Pending, Filled, PartialFill, Cancelled, Rejected, Expired };
    enum class OrderTimeInForce { GTC, DAY, IOC, FOK };

    inline const char* OrderSideStr(OrderSide s) { return s == OrderSide::Buy ? "BUY" : "SELL"; }
    inline const char* OrderTypeStr(OrderType t)
    {
        switch (t) {
            case OrderType::Market:    return "MARKET";
            case OrderType::Limit:     return "LIMIT";
            case OrderType::Stop:      return "STOP";
            case OrderType::StopLimit: return "STOP_LIMIT";
        }
        return "UNKNOWN";
    }

    struct BrokerOrder
    {
        std::string orderId;           // Broker-assigned ID
        std::string symbol;
        OrderSide   side       = OrderSide::Buy;
        OrderType   type       = OrderType::Market;
        OrderTimeInForce tif   = OrderTimeInForce::GTC;
        float       quantity   = 0.f;
        float       price      = 0.f;   // Limit price (0 for market)
        float       stopPrice  = 0.f;   // Stop trigger price
        float       filledQty  = 0.f;
        float       avgFillPrice = 0.f;
        OrderStatus status     = OrderStatus::Pending;
        int64_t     createdAt  = 0;
        int64_t     updatedAt  = 0;
        std::string errorMsg;          // Populated on rejection

        // Strategy linkage
        int strategyId = 0;            // Our internal strategy ID that triggered this order
    };

    struct BrokerPosition
    {
        std::string symbol;
        float       quantity    = 0.f;   // Positive=long, negative=short
        float       avgCost    = 0.f;
        float       marketValue = 0.f;
        float       unrealizedPnL = 0.f;
    };

    struct BrokerBalance
    {
        float       cash        = 0.f;
        float       equity      = 0.f;
        float       buyingPower = 0.f;
        std::string currency    = "USD";
    };

    struct BrokerTick
    {
        std::string symbol;
        float       bid        = 0.f;
        float       ask        = 0.f;
        float       last       = 0.f;
        float       volume     = 0.f;
        int64_t     timestamp  = 0;

        float Mid() const { return (bid + ask) * 0.5f; }
        float Spread() const { return ask - bid; }
    };

    // ── Connection/Auth ───────────────────────────────────────────────────────────

    enum class BrokerAuthType
    {
        ApiKey,          // Simple API key + secret
        OAuth2,          // OAuth2 flow (GBM+, TD Ameritrade)
        Session,         // Username/password session (MetaTrader)
        Certificate      // Client certificate (FIX)
    };

    struct BrokerCredentials
    {
        BrokerAuthType authType = BrokerAuthType::ApiKey;

        // ApiKey auth
        std::string apiKey;
        std::string apiSecret;

        // OAuth2 auth
        std::string clientId;
        std::string clientSecret;
        std::string refreshToken;
        std::string accessToken;       // Cached, refreshed automatically
        int64_t     tokenExpiry = 0;

        // Session auth (MetaTrader)
        std::string server;            // e.g., "MetaQuotes-Demo"
        std::string login;
        std::string password;

        // Common
        std::string accountId;
        bool        sandbox = true;    // Use paper/demo account
    };

    struct BrokerConfig
    {
        std::string      name;          // Human-readable ("GBM+", "MetaTrader 5")
        std::string      restBaseUrl;   // REST API base URL
        std::string      wsUrl;         // WebSocket streaming URL
        std::string      webhookSecret; // HMAC secret for webhook signature validation
        int              webhookPort = 0; // If > 0, start webhook receiver on this port
        BrokerCredentials credentials;
    };

    // ── Events ────────────────────────────────────────────────────────────────────

    using OnTickCallback       = std::function<void(const BrokerTick& tick)>;
    using OnOrderCallback      = std::function<void(const BrokerOrder& order)>;
    using OnPositionCallback   = std::function<void(const BrokerPosition& pos)>;
    using OnErrorCallback      = std::function<void(const std::string& msg, int code)>;
    using OnConnectionCallback = std::function<void(WsState state)>;

    // ── IBrokerConnector ──────────────────────────────────────────────────────────
    //
    // Unified interface for connecting to a futures/equities broker.
    // Combines:
    //   - Real-time market data feed (WebSocket stream or polling)
    //   - Order execution (REST or FIX)
    //   - Account/position queries
    //   - Webhook inbound events
    //
    // Implementations:
    //   - GBMBrokerConnector     (GBM+ REST + WebSocket)
    //   - MetaTraderConnector    (MT5 Manager API or bridge)
    //   - GenericWebhookBroker   (any broker with webhook + REST)
    //
    // Also implements IBrokerDataSource so it can be registered with MarketService
    // for the "R" (real-time) timeframe.
    class IBrokerConnector : public IBrokerDataSource
    {
    public:
        ~IBrokerConnector() override = default;

        // ── Connection lifecycle ──────────────────────────────────────────────────

        virtual void Initialize(const BrokerConfig& config) = 0;
        virtual void Connect() = 0;
        virtual void Disconnect() = 0;
        virtual WsState GetConnectionState() const = 0;

        // ── Market data subscription ─────────────────────────────────────────────

        virtual void SubscribePrice(const std::string& symbol) = 0;
        virtual void UnsubscribePrice(const std::string& symbol) = 0;
        virtual std::vector<std::string> GetSubscribedSymbols() const = 0;

        // Get last tick for symbol (from internal cache, lock-free)
        virtual BrokerTick GetLastTick(const std::string& symbol) const = 0;

        // ── Order execution ──────────────────────────────────────────────────────

        virtual BrokerOrder PlaceOrder(const std::string& symbol,
                                        OrderSide side,
                                        OrderType type,
                                        float quantity,
                                        float price = 0.f,
                                        float stopPrice = 0.f,
                                        OrderTimeInForce tif = OrderTimeInForce::GTC) = 0;

        virtual bool CancelOrder(const std::string& orderId) = 0;
        virtual BrokerOrder GetOrder(const std::string& orderId) = 0;
        virtual std::vector<BrokerOrder> GetOpenOrders() = 0;

        // ── Account ──────────────────────────────────────────────────────────────

        virtual BrokerBalance GetBalance() = 0;
        virtual std::vector<BrokerPosition> GetPositions() = 0;

        // ── Event callbacks ──────────────────────────────────────────────────────

        virtual void SetOnTick(OnTickCallback cb) = 0;
        virtual void SetOnOrder(OnOrderCallback cb) = 0;
        virtual void SetOnPosition(OnPositionCallback cb) = 0;
        virtual void SetOnError(OnErrorCallback cb) = 0;
        virtual void SetOnConnection(OnConnectionCallback cb) = 0;

        // ── IBrokerDataSource implementation helpers ──────────────────────────────
        // Subclasses typically implement:
        //   GetName()           → broker name
        //   IsRealtime()        → true
        //   GetDelaySeconds()   → 0
        //   FetchCurrentPrice() → GetLastTick(symbol).last
        //   SupportsStreaming() → true
        //   FetchQuote()        → REST candle fetch or empty (RT doesn't need candles)
        //   SearchSymbols()     → broker symbol search API

        // ── Diagnostics ──────────────────────────────────────────────────────────

        virtual float GetLatencyMs() const = 0;
        virtual uint64_t GetTicksReceived() const = 0;
        virtual const BrokerConfig& GetConfig() const = 0;
    };

} // namespace stnks
