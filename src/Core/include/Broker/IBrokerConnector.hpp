#pragma once

#include <EnumTraits.hpp>
#include <Market/IBrokerDataSource.hpp>
#include <Net/IWebSocketClient.hpp>
#include <Strategy/Strategy.hpp>
#include <string>
#include <vector>
#include <functional>
#include <cstdint>

namespace stnks
{
    enum class OrderSide { Buy, Sell };
    enum class OrderType { Market, Limit, Stop, StopLimit };
    enum class OrderStatus { Pending, Filled, PartialFill, Cancelled, Rejected, Expired };
    enum class OrderTimeInForce { GTC, DAY, IOC, FOK };

    template<> struct EnumTraits<OrderSide> {
        static constexpr std::pair<OrderSide, const char*> values[] = {
            {OrderSide::Buy, "BUY"}, {OrderSide::Sell, "SELL"}
        };
    };

    template<> struct EnumTraits<OrderType> {
        static constexpr std::pair<OrderType, const char*> values[] = {
            {OrderType::Market, "MARKET"}, {OrderType::Limit, "LIMIT"},
            {OrderType::Stop, "STOP"}, {OrderType::StopLimit, "STOP_LIMIT"}
        };
    };

    template<> struct EnumTraits<OrderStatus> {
        static constexpr std::pair<OrderStatus, const char*> values[] = {
            {OrderStatus::Pending, "Pending"}, {OrderStatus::Filled, "Filled"},
            {OrderStatus::PartialFill, "PartialFill"}, {OrderStatus::Cancelled, "Cancelled"},
            {OrderStatus::Rejected, "Rejected"}, {OrderStatus::Expired, "Expired"}
        };
    };

    template<> struct EnumTraits<OrderTimeInForce> {
        static constexpr std::pair<OrderTimeInForce, const char*> values[] = {
            {OrderTimeInForce::GTC, "GTC"}, {OrderTimeInForce::DAY, "DAY"},
            {OrderTimeInForce::IOC, "IOC"}, {OrderTimeInForce::FOK, "FOK"}
        };
    };

    inline const char* OrderSideStr(OrderSide s) { return EnumToString(s); }
    inline const char* OrderTypeStr(OrderType t) { return EnumToString(t); }

    struct BrokerOrder
    {
        std::string orderId;
        std::string symbol;
        OrderSide   side       = OrderSide::Buy;
        OrderType   type       = OrderType::Market;
        OrderTimeInForce tif   = OrderTimeInForce::GTC;
        float       quantity   = 0.f;
        float       price      = 0.f;
        float       stopPrice  = 0.f;
        float       filledQty  = 0.f;
        float       avgFillPrice = 0.f;
        OrderStatus status     = OrderStatus::Pending;
        int64_t     createdAt  = 0;
        int64_t     updatedAt  = 0;
        std::string errorMsg;
        int         strategyId = 0;
    };

    struct BrokerPosition
    {
        std::string symbol;
        float       quantity    = 0.f;
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

    enum class BrokerAuthType { ApiKey, OAuth2, Session, Certificate };

    struct BrokerCredentials
    {
        BrokerAuthType authType = BrokerAuthType::ApiKey;
        std::string apiKey;
        std::string apiSecret;
        std::string clientId;
        std::string clientSecret;
        std::string refreshToken;
        std::string accessToken;
        int64_t     tokenExpiry = 0;
        std::string server;
        std::string login;
        std::string password;
        std::string accountId;
        bool        sandbox = true;
    };

    struct BrokerConfig
    {
        std::string      name;
        std::string      restBaseUrl;
        std::string      wsUrl;
        std::string      webhookSecret;
        int              webhookPort = 0;
        BrokerCredentials credentials;
    };

    using OnTickCallback       = std::function<void(const BrokerTick& tick)>;
    using OnOrderCallback      = std::function<void(const BrokerOrder& order)>;
    using OnPositionCallback   = std::function<void(const BrokerPosition& pos)>;
    using OnErrorCallback      = std::function<void(const std::string& msg, int code)>;
    using OnConnectionCallback = std::function<void(WsState state)>;

    class IBrokerConnector : public IBrokerDataSource
    {
    public:
        ~IBrokerConnector() override = default;

        virtual void Initialize(const BrokerConfig& config) = 0;
        virtual void Connect() = 0;
        virtual void Disconnect() = 0;
        virtual WsState GetConnectionState() const = 0;

        virtual void SubscribePrice(const std::string& symbol) = 0;
        virtual void UnsubscribePrice(const std::string& symbol) = 0;
        virtual std::vector<std::string> GetSubscribedSymbols() const = 0;
        virtual BrokerTick GetLastTick(const std::string& symbol) const = 0;

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

        virtual BrokerBalance GetBalance() = 0;
        virtual std::vector<BrokerPosition> GetPositions() = 0;

        virtual void SetOnTick(OnTickCallback cb) = 0;
        virtual void SetOnOrder(OnOrderCallback cb) = 0;
        virtual void SetOnPosition(OnPositionCallback cb) = 0;
        virtual void SetOnError(OnErrorCallback cb) = 0;
        virtual void SetOnConnection(OnConnectionCallback cb) = 0;

        virtual float GetLatencyMs() const = 0;
        virtual uint64_t GetTicksReceived() const = 0;
        virtual const BrokerConfig& GetConfig() const = 0;
    };

} // namespace stnks
