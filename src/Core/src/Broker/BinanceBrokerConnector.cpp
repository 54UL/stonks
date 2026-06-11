#include <Broker/BinanceBrokerConnector.hpp>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <openssl/hmac.h>
#include <openssl/sha.h>
#include <algorithm>
#include <chrono>
#include <ctime>
#include <sstream>
#include <iomanip>

namespace stnks
{
    BinanceBrokerConnector::BinanceBrokerConnector(HttpClient& http)
        : http_(http)
    {
    }

    BinanceBrokerConnector::~BinanceBrokerConnector()
    {
        Disconnect();
    }

    // ── Helpers ─────────────────────────────────────────────────────────────────

    std::string BinanceBrokerConnector::ToLower(const std::string& s)
    {
        std::string result = s;
        std::transform(result.begin(), result.end(), result.begin(), ::tolower);
        return result;
    }

    std::string BinanceBrokerConnector::GetTimestamp() const
    {
        auto now = std::chrono::system_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()).count();
        return std::to_string(ms);
    }

    std::string BinanceBrokerConnector::SignQuery(const std::string& queryString) const
    {
        unsigned char hash[EVP_MAX_MD_SIZE];
        unsigned int hashLen = 0;

        HMAC(EVP_sha256(),
             config_.credentials.apiSecret.c_str(),
             static_cast<int>(config_.credentials.apiSecret.size()),
             reinterpret_cast<const unsigned char*>(queryString.c_str()),
             queryString.size(),
             hash, &hashLen);

        std::ostringstream oss;
        for (unsigned int i = 0; i < hashLen; ++i)
            oss << std::hex << std::setfill('0') << std::setw(2) << (int)hash[i];
        return oss.str();
    }

    std::string BinanceBrokerConnector::MakeUrl(const std::string& path) const
    {
        return config_.restBaseUrl + path;
    }

    std::string BinanceBrokerConnector::MakeSignedUrl(const std::string& endpoint,
                                                       const std::string& params) const
    {
        std::string ts = GetTimestamp();
        std::string query = params.empty()
            ? "timestamp=" + ts
            : params + "&timestamp=" + ts;
        std::string signature = SignQuery(query);
        return MakeUrl(endpoint) + "?" + query + "&signature=" + signature;
    }

    std::vector<std::pair<std::string,std::string>> BinanceBrokerConnector::GetAuthHeaders() const
    {
        return {{"X-MBX-APIKEY", config_.credentials.apiKey}};
    }

    std::string BinanceBrokerConnector::BinanceOrderType(OrderType type)
    {
        switch (type)
        {
        case OrderType::Market:    return "MARKET";
        case OrderType::Limit:     return "LIMIT";
        case OrderType::Stop:      return "STOP_LOSS";
        case OrderType::StopLimit: return "STOP_LOSS_LIMIT";
        }
        return "MARKET";
    }

    std::string BinanceBrokerConnector::BinanceTIF(OrderTimeInForce tif)
    {
        switch (tif)
        {
        case OrderTimeInForce::GTC: return "GTC";
        case OrderTimeInForce::DAY: return "GTC"; // Binance doesn't have DAY, use GTC
        case OrderTimeInForce::IOC: return "IOC";
        case OrderTimeInForce::FOK: return "FOK";
        }
        return "GTC";
    }

    OrderStatus BinanceBrokerConnector::ParseBinanceStatus(const std::string& status)
    {
        if (status == "NEW" || status == "PENDING_NEW")
            return OrderStatus::Pending;
        if (status == "FILLED")
            return OrderStatus::Filled;
        if (status == "PARTIALLY_FILLED")
            return OrderStatus::PartialFill;
        if (status == "CANCELED" || status == "PENDING_CANCEL")
            return OrderStatus::Cancelled;
        if (status == "REJECTED")
            return OrderStatus::Rejected;
        if (status == "EXPIRED" || status == "EXPIRED_IN_MATCH")
            return OrderStatus::Expired;
        return OrderStatus::Pending;
    }

    std::string BinanceBrokerConnector::ToBinanceInterval(const std::string& interval)
    {
        if (interval == "1m")  return "1m";
        if (interval == "5m")  return "5m";
        if (interval == "15m") return "15m";
        if (interval == "30m") return "30m";
        if (interval == "1h")  return "1h";
        if (interval == "4h")  return "4h";
        if (interval == "1d")  return "1d";
        if (interval == "1wk") return "1w";
        if (interval == "1mo") return "1M";
        return "1d";
    }

    std::string BinanceBrokerConnector::ToBinanceRange(const std::string& range)
    {
        // Convert our range to Binance limit (number of candles)
        // "1d" → 24 (for 1h), "5d" → 120, "1mo" → 30, "3mo" → 90, "6mo" → 180, "1y" → 365
        if (range == "1d")  return "96";    // 15m candles for 1 day
        if (range == "5d")  return "120";
        if (range == "1mo") return "30";
        if (range == "3mo") return "90";
        if (range == "6mo") return "180";
        if (range == "1y")  return "365";
        if (range == "2y")  return "730";
        return "180";
    }

    // ── Lifecycle ────────────────────────────────────────────────────────────────

    void BinanceBrokerConnector::Initialize(const BrokerConfig& config)
    {
        config_ = config;

        // Set defaults for Binance if not provided
        if (config_.restBaseUrl.empty())
        {
            config_.restBaseUrl = config_.credentials.sandbox
                ? "https://testnet.binance.vision"
                : "https://api.binance.com";
        }
        if (config_.wsUrl.empty())
        {
            config_.wsUrl = config_.credentials.sandbox
                ? "wss://testnet.binance.vision/ws"
                : "wss://stream.binance.com:9443/ws";
        }

        spdlog::info("[Binance] Initialized: REST={} WS={} sandbox={}",
            config_.restBaseUrl, config_.wsUrl, config_.credentials.sandbox);
    }

    void BinanceBrokerConnector::Connect()
    {
        // Market WebSocket (will be connected when symbols are subscribed)
        marketWs_.SetOnMessage([this](const std::string& data, WsMessageType)
        {
            HandleMarketWsMessage(data);
        });

        marketWs_.SetOnStateChange([this](WsState state)
        {
            spdlog::info("[Binance] Market WS state: {}", WsStateToString(state));
            std::lock_guard<std::mutex> lock(cbMutex_);
            if (onConnection_) onConnection_(state);
        });

        marketWs_.SetOnError([this](const std::string& reason, int code)
        {
            spdlog::error("[Binance] Market WS error: {} ({})", reason, code);
            std::lock_guard<std::mutex> lock(cbMutex_);
            if (onError_) onError_(reason, code);
        });

        // Start User Data Stream for order updates
        if (StartUserDataStream())
        {
            WsConfig userWsConfig;
            userWsConfig.url = config_.wsUrl + "/" + listenKey_;
            userWsConfig.pingIntervalSec = 30;
            userWsConfig.autoReconnect = true;

            userWs_.SetOnMessage([this](const std::string& data, WsMessageType)
            {
                HandleUserWsMessage(data);
            });

            userWs_.SetOnError([this](const std::string& reason, int code)
            {
                spdlog::error("[Binance] User WS error: {} ({})", reason, code);
            });

            userWs_.Connect(userWsConfig);
        }

        spdlog::info("[Binance] Connected");
    }

    void BinanceBrokerConnector::Disconnect()
    {
        marketWs_.Disconnect();
        userWs_.Disconnect();

        {
            std::lock_guard<std::mutex> lock(subMutex_);
            subscriptions_.clear();
        }

        spdlog::info("[Binance] Disconnected");
    }

    WsState BinanceBrokerConnector::GetConnectionState() const
    {
        return marketWs_.GetState();
    }

    // ── User Data Stream ────────────────────────────────────────────────────────

    bool BinanceBrokerConnector::StartUserDataStream()
    {
        std::string url = MakeUrl("/api/v3/userDataStream");
        auto resp = http_.Post(url, "", GetAuthHeaders());
        if (!resp.Ok())
        {
            spdlog::error("[Binance] Failed to start user data stream: {}", resp.error);
            return false;
        }

        try
        {
            auto j = nlohmann::json::parse(resp.body);
            listenKey_ = j.value("listenKey", "");
            if (listenKey_.empty())
            {
                spdlog::error("[Binance] Empty listenKey in response");
                return false;
            }
            spdlog::info("[Binance] User data stream started: {}", listenKey_.substr(0, 10) + "...");
            return true;
        }
        catch (const std::exception& e)
        {
            spdlog::error("[Binance] listenKey parse error: {}", e.what());
            return false;
        }
    }

    void BinanceBrokerConnector::KeepAliveUserDataStream()
    {
        if (listenKey_.empty()) return;
        std::string url = MakeUrl("/api/v3/userDataStream?listenKey=" + listenKey_);
        http_.Put(url, "", GetAuthHeaders());
    }

    // ── Market data ──────────────────────────────────────────────────────────────

    void BinanceBrokerConnector::SubscribePrice(const std::string& symbol)
    {
        std::string lower = ToLower(symbol);
        {
            std::lock_guard<std::mutex> lock(subMutex_);
            subscriptions_.push_back(symbol);
        }

        // Connect/reconnect market WS with combined streams
        std::string streamUrl;
        {
            std::lock_guard<std::mutex> lock(subMutex_);
            std::string baseWs = config_.credentials.sandbox
                ? "wss://testnet.binance.vision/stream?streams="
                : "wss://stream.binance.com:9443/stream?streams=";
            std::string streams;
            for (size_t i = 0; i < subscriptions_.size(); ++i)
            {
                if (i > 0) streams += "/";
                streams += ToLower(subscriptions_[i]) + "@trade";
            }
            streamUrl = baseWs + streams;
        }

        WsConfig wsConfig;
        wsConfig.url = streamUrl;
        wsConfig.pingIntervalSec = 30;
        wsConfig.autoReconnect = true;

        // Reconnect with all streams
        marketWs_.Disconnect();
        marketWs_.Connect(wsConfig);

        spdlog::info("[Binance] Subscribed to {}", symbol);
    }

    void BinanceBrokerConnector::UnsubscribePrice(const std::string& symbol)
    {
        {
            std::lock_guard<std::mutex> lock(subMutex_);
            subscriptions_.erase(
                std::remove(subscriptions_.begin(), subscriptions_.end(), symbol),
                subscriptions_.end());
        }

        // Reconnect without this symbol (or disconnect if empty)
        std::lock_guard<std::mutex> lock(subMutex_);
        if (subscriptions_.empty())
        {
            marketWs_.Disconnect();
        }
        else
        {
            // Re-subscribe remaining
            std::string baseWs = config_.credentials.sandbox
                ? "wss://testnet.binance.vision/stream?streams="
                : "wss://stream.binance.com:9443/stream?streams=";
            std::string streams;
            for (size_t i = 0; i < subscriptions_.size(); ++i)
            {
                if (i > 0) streams += "/";
                streams += ToLower(subscriptions_[i]) + "@trade";
            }

            WsConfig wsConfig;
            wsConfig.url = baseWs + streams;
            wsConfig.pingIntervalSec = 30;
            wsConfig.autoReconnect = true;

            marketWs_.Disconnect();
            marketWs_.Connect(wsConfig);
        }
    }

    std::vector<std::string> BinanceBrokerConnector::GetSubscribedSymbols() const
    {
        std::lock_guard<std::mutex> lock(subMutex_);
        return subscriptions_;
    }

    BrokerTick BinanceBrokerConnector::GetLastTick(const std::string& symbol) const
    {
        std::lock_guard<std::mutex> lock(tickMutex_);
        auto it = tickCache_.find(symbol);
        if (it != tickCache_.end()) return it->second;
        return {};
    }

    // ── IBrokerDataSource ────────────────────────────────────────────────────────

    float BinanceBrokerConnector::FetchCurrentPrice(const std::string& symbol)
    {
        auto tick = GetLastTick(symbol);
        if (tick.last > 0.f) return tick.last;

        // Fallback: REST ticker
        std::string url = MakeUrl("/api/v3/ticker/price?symbol=" + symbol);
        auto resp = http_.Get(url);
        if (!resp.Ok()) return 0.f;

        try
        {
            auto j = nlohmann::json::parse(resp.body);
            return std::stof(j.value("price", "0"));
        }
        catch (...) { return 0.f; }
    }

    void BinanceBrokerConnector::Subscribe(const std::string& symbol, TickCallback cb)
    {
        SubscribePrice(symbol);
        SetOnTick([cb, symbol](const BrokerTick& tick) {
            if (tick.symbol == symbol)
                cb(tick.symbol, tick.last, tick.timestamp);
        });
    }

    void BinanceBrokerConnector::Unsubscribe(const std::string& symbol)
    {
        UnsubscribePrice(symbol);
    }

    StockQuote BinanceBrokerConnector::FetchQuote(const std::string& symbol,
                                                   const std::string& interval,
                                                   const std::string& range)
    {
        StockQuote quote;
        quote.symbol = symbol;
        quote.source = GetName();
        quote.fetchedAt = std::time(nullptr);

        // Binance klines API
        std::string binInterval = ToBinanceInterval(interval);
        std::string limit = ToBinanceRange(range);

        std::string url = MakeUrl("/api/v3/klines?symbol=" + symbol +
            "&interval=" + binInterval + "&limit=" + limit);
        auto resp = http_.Get(url);
        if (!resp.Ok()) return quote;

        try
        {
            auto j = nlohmann::json::parse(resp.body);
            for (auto& kline : j)
            {
                // Kline format: [openTime, open, high, low, close, volume, closeTime, ...]
                Candle c;
                c.timestamp = kline[0].get<int64_t>() / 1000; // ms → sec
                c.open  = std::stof(kline[1].get<std::string>());
                c.high  = std::stof(kline[2].get<std::string>());
                c.low   = std::stof(kline[3].get<std::string>());
                c.close = std::stof(kline[4].get<std::string>());
                c.volume = std::stof(kline[5].get<std::string>());
                quote.candles.push_back(c);
            }
        }
        catch (const std::exception& e)
        {
            spdlog::warn("[Binance] Klines parse error: {}", e.what());
        }

        return quote;
    }

    std::vector<SymbolMatch> BinanceBrokerConnector::SearchSymbols(const std::string& query)
    {
        // Binance doesn't have a search API; we fetch exchangeInfo and filter locally
        std::string url = MakeUrl("/api/v3/exchangeInfo");
        auto resp = http_.Get(url);
        if (!resp.Ok()) return {};

        std::vector<SymbolMatch> results;
        std::string upperQuery = query;
        std::transform(upperQuery.begin(), upperQuery.end(), upperQuery.begin(), ::toupper);

        try
        {
            auto j = nlohmann::json::parse(resp.body);
            for (auto& sym : j["symbols"])
            {
                std::string symbol = sym.value("symbol", "");
                std::string baseAsset = sym.value("baseAsset", "");
                std::string status = sym.value("status", "");

                if (status != "TRADING") continue;

                // Match against query
                if (symbol.find(upperQuery) != std::string::npos ||
                    baseAsset.find(upperQuery) != std::string::npos)
                {
                    SymbolMatch m;
                    m.symbol   = symbol;
                    m.name     = baseAsset + "/" + sym.value("quoteAsset", "");
                    m.exchange = "Binance";
                    m.type     = "CRYPTO";
                    m.source   = "Binance";
                    results.push_back(std::move(m));

                    if (results.size() >= 20) break; // Limit results
                }
            }
        }
        catch (...) {}
        return results;
    }

    // ── Order execution ──────────────────────────────────────────────────────────

    BrokerOrder BinanceBrokerConnector::PlaceOrder(const std::string& symbol,
                                                    OrderSide side, OrderType type,
                                                    float quantity, float price,
                                                    float stopPrice, OrderTimeInForce tif)
    {
        std::string params =
            "symbol=" + symbol +
            "&side=" + std::string(OrderSideStr(side)) +
            "&type=" + BinanceOrderType(type) +
            "&quantity=" + std::to_string(quantity);

        // Add price for limit orders
        if (type == OrderType::Limit || type == OrderType::StopLimit)
        {
            std::ostringstream priceStr;
            priceStr << std::fixed << std::setprecision(8) << price;
            params += "&price=" + priceStr.str();
            params += "&timeInForce=" + BinanceTIF(tif);
        }

        // Add stop price
        if (type == OrderType::Stop || type == OrderType::StopLimit)
        {
            std::ostringstream stopStr;
            stopStr << std::fixed << std::setprecision(8) << stopPrice;
            params += "&stopPrice=" + stopStr.str();
        }

        std::string url = MakeSignedUrl("/api/v3/order", params);
        auto resp = http_.Post(url, "", GetAuthHeaders());

        BrokerOrder order;
        order.symbol = symbol;
        order.side = side;
        order.type = type;
        order.quantity = quantity;
        order.price = price;
        order.stopPrice = stopPrice;
        order.createdAt = std::time(nullptr);

        if (!resp.Ok())
        {
            order.status = OrderStatus::Rejected;
            order.errorMsg = resp.error.empty() ? resp.body : resp.error;
            spdlog::error("[Binance] Order rejected: {}", order.errorMsg);
            return order;
        }

        try
        {
            auto j = nlohmann::json::parse(resp.body);
            order.orderId = std::to_string(j.value("orderId", (int64_t)0));
            order.status = ParseBinanceStatus(j.value("status", "NEW"));
            order.filledQty = std::stof(j.value("executedQty", "0"));

            // Track symbol for cancel
            {
                std::lock_guard<std::mutex> lock(orderSymbolMutex_);
                orderIdToSymbol_[order.orderId] = symbol;
            }
        }
        catch (...)
        {
            order.status = OrderStatus::Rejected;
            order.errorMsg = "Failed to parse response";
        }

        spdlog::info("[Binance] Order placed: {} {} {} {} @ {}",
            order.orderId, OrderSideStr(side), quantity, symbol, price);
        return order;
    }

    bool BinanceBrokerConnector::CancelOrder(const std::string& orderId)
    {
        // We need the symbol to cancel on Binance
        std::string symbol;
        {
            std::lock_guard<std::mutex> lock(orderSymbolMutex_);
            auto it = orderIdToSymbol_.find(orderId);
            if (it != orderIdToSymbol_.end())
                symbol = it->second;
        }

        if (symbol.empty())
        {
            spdlog::error("[Binance] Cannot cancel order {}: unknown symbol", orderId);
            return false;
        }

        std::string params = "symbol=" + symbol + "&orderId=" + orderId;
        std::string url = MakeSignedUrl("/api/v3/order", params);
        auto resp = http_.Delete(url, GetAuthHeaders());
        return resp.Ok();
    }

    BrokerOrder BinanceBrokerConnector::GetOrder(const std::string& orderId)
    {
        // Need symbol to query — check tracked orders
        std::string symbol;
        {
            std::lock_guard<std::mutex> lock(orderSymbolMutex_);
            auto it = orderIdToSymbol_.find(orderId);
            if (it != orderIdToSymbol_.end())
                symbol = it->second;
        }

        if (symbol.empty()) return {};

        std::string params = "symbol=" + symbol + "&orderId=" + orderId;
        std::string url = MakeSignedUrl("/api/v3/order", params);
        auto resp = http_.Get(url, GetAuthHeaders());
        if (!resp.Ok()) return {};

        try
        {
            auto j = nlohmann::json::parse(resp.body);
            return ParseOrderJson(j);
        }
        catch (...) { return {}; }
    }

    std::vector<BrokerOrder> BinanceBrokerConnector::GetOpenOrders()
    {
        std::string url = MakeSignedUrl("/api/v3/openOrders", "");
        auto resp = http_.Get(url, GetAuthHeaders());
        if (!resp.Ok()) return {};

        std::vector<BrokerOrder> orders;
        try
        {
            auto j = nlohmann::json::parse(resp.body);
            for (auto& item : j)
            {
                orders.push_back(ParseOrderJson(item));
                // Track symbol
                std::lock_guard<std::mutex> lock(orderSymbolMutex_);
                orderIdToSymbol_[orders.back().orderId] = orders.back().symbol;
            }
        }
        catch (...) {}
        return orders;
    }

    // ── Account ──────────────────────────────────────────────────────────────────

    BrokerBalance BinanceBrokerConnector::GetBalance()
    {
        std::string url = MakeSignedUrl("/api/v3/account", "");
        auto resp = http_.Get(url, GetAuthHeaders());
        if (!resp.Ok()) return {};

        BrokerBalance bal;
        bal.currency = "USDT";

        try
        {
            auto j = nlohmann::json::parse(resp.body);
            for (auto& asset : j["balances"])
            {
                std::string assetName = asset.value("asset", "");
                float free = std::stof(asset.value("free", "0"));
                float locked = std::stof(asset.value("locked", "0"));

                if (assetName == "USDT" || assetName == "BUSD" || assetName == "USD")
                {
                    bal.cash += free;
                    bal.equity += free + locked;
                    bal.buyingPower += free;
                }
            }
        }
        catch (...) {}
        return bal;
    }

    std::vector<BrokerPosition> BinanceBrokerConnector::GetPositions()
    {
        std::string url = MakeSignedUrl("/api/v3/account", "");
        auto resp = http_.Get(url, GetAuthHeaders());
        if (!resp.Ok()) return {};

        std::vector<BrokerPosition> positions;
        try
        {
            auto j = nlohmann::json::parse(resp.body);
            for (auto& asset : j["balances"])
            {
                float free = std::stof(asset.value("free", "0"));
                float locked = std::stof(asset.value("locked", "0"));
                float total = free + locked;

                if (total <= 0.f) continue;

                std::string assetName = asset.value("asset", "");
                // Skip stablecoins as they're "cash"
                if (assetName == "USDT" || assetName == "BUSD" ||
                    assetName == "USD" || assetName == "USDC") continue;

                BrokerPosition pos;
                pos.symbol = assetName + "USDT";
                pos.quantity = total;
                // We'd need a separate price fetch for marketValue/pnl
                positions.push_back(pos);
            }
        }
        catch (...) {}
        return positions;
    }

    // ── Event callbacks ──────────────────────────────────────────────────────────

    void BinanceBrokerConnector::SetOnTick(OnTickCallback cb)
    {
        std::lock_guard<std::mutex> lock(cbMutex_);
        onTick_ = std::move(cb);
    }

    void BinanceBrokerConnector::SetOnOrder(OnOrderCallback cb)
    {
        std::lock_guard<std::mutex> lock(cbMutex_);
        onOrder_ = std::move(cb);
    }

    void BinanceBrokerConnector::SetOnPosition(OnPositionCallback cb)
    {
        std::lock_guard<std::mutex> lock(cbMutex_);
        onPosition_ = std::move(cb);
    }

    void BinanceBrokerConnector::SetOnError(OnErrorCallback cb)
    {
        std::lock_guard<std::mutex> lock(cbMutex_);
        onError_ = std::move(cb);
    }

    void BinanceBrokerConnector::SetOnConnection(OnConnectionCallback cb)
    {
        std::lock_guard<std::mutex> lock(cbMutex_);
        onConnection_ = std::move(cb);
    }

    float BinanceBrokerConnector::GetLatencyMs() const
    {
        return marketWs_.GetLatencyMs();
    }

    uint64_t BinanceBrokerConnector::GetTicksReceived() const
    {
        return ticksReceived_.load(std::memory_order_relaxed);
    }

    // ── WebSocket message handlers ───────────────────────────────────────────────

    void BinanceBrokerConnector::HandleMarketWsMessage(const std::string& data)
    {
        try
        {
            auto j = nlohmann::json::parse(data);

            // Combined stream format: {"stream":"btcusdt@trade","data":{...}}
            nlohmann::json payload;
            if (j.contains("data"))
                payload = j["data"];
            else
                payload = j;

            std::string eventType = payload.value("e", "");

            if (eventType == "trade")
            {
                BrokerTick tick;
                tick.symbol    = payload.value("s", "");
                tick.last      = std::stof(payload.value("p", "0"));
                tick.volume    = std::stof(payload.value("q", "0"));
                tick.timestamp = payload.value("T", (int64_t)0) / 1000;

                // bid/ask approximation from last trade
                tick.bid = tick.last;
                tick.ask = tick.last;

                // Update cache
                {
                    std::lock_guard<std::mutex> lock(tickMutex_);
                    tickCache_[tick.symbol] = tick;
                }

                ticksReceived_.fetch_add(1, std::memory_order_relaxed);

                // Notify callback
                std::lock_guard<std::mutex> lock(cbMutex_);
                if (onTick_) onTick_(tick);
            }
            else if (eventType == "24hrTicker")
            {
                BrokerTick tick;
                tick.symbol    = payload.value("s", "");
                tick.bid       = std::stof(payload.value("b", "0"));
                tick.ask       = std::stof(payload.value("a", "0"));
                tick.last      = std::stof(payload.value("c", "0"));
                tick.volume    = std::stof(payload.value("v", "0"));
                tick.timestamp = payload.value("E", (int64_t)0) / 1000;

                {
                    std::lock_guard<std::mutex> lock(tickMutex_);
                    tickCache_[tick.symbol] = tick;
                }

                ticksReceived_.fetch_add(1, std::memory_order_relaxed);

                std::lock_guard<std::mutex> lock(cbMutex_);
                if (onTick_) onTick_(tick);
            }
        }
        catch (const std::exception& e)
        {
            spdlog::warn("[Binance] Market WS parse error: {}", e.what());
        }
    }

    void BinanceBrokerConnector::HandleUserWsMessage(const std::string& data)
    {
        try
        {
            auto j = nlohmann::json::parse(data);
            std::string eventType = j.value("e", "");

            if (eventType == "executionReport")
            {
                BrokerOrder order;
                order.orderId     = std::to_string(j.value("i", (int64_t)0));
                order.symbol      = j.value("s", "");
                order.side        = (j.value("S", "") == "BUY") ? OrderSide::Buy : OrderSide::Sell;
                order.quantity    = std::stof(j.value("q", "0"));
                order.price       = std::stof(j.value("p", "0"));
                order.stopPrice   = std::stof(j.value("P", "0"));
                order.filledQty   = std::stof(j.value("z", "0"));
                order.avgFillPrice = std::stof(j.value("L", "0")); // Last executed price
                order.status      = ParseBinanceStatus(j.value("X", ""));
                order.createdAt   = j.value("T", (int64_t)0) / 1000;

                // Track symbol
                {
                    std::lock_guard<std::mutex> lock(orderSymbolMutex_);
                    orderIdToSymbol_[order.orderId] = order.symbol;
                }

                std::lock_guard<std::mutex> lock(cbMutex_);
                if (onOrder_) onOrder_(order);
            }
            else if (eventType == "outboundAccountPosition")
            {
                // Account balance update
                for (auto& bal : j["B"])
                {
                    BrokerPosition pos;
                    std::string asset = bal.value("a", "");
                    float free = std::stof(bal.value("f", "0"));
                    float locked = std::stof(bal.value("l", "0"));
                    float total = free + locked;

                    if (total <= 0.f) continue;
                    if (asset == "USDT" || asset == "BUSD" || asset == "USDC") continue;

                    pos.symbol = asset + "USDT";
                    pos.quantity = total;

                    std::lock_guard<std::mutex> lock(cbMutex_);
                    if (onPosition_) onPosition_(pos);
                }
            }
        }
        catch (const std::exception& e)
        {
            spdlog::warn("[Binance] User WS parse error: {}", e.what());
        }
    }

    // ── JSON parsers ─────────────────────────────────────────────────────────────

    BrokerOrder BinanceBrokerConnector::ParseOrderJson(const nlohmann::json& j) const
    {
        BrokerOrder order;
        order.orderId      = std::to_string(j.value("orderId", (int64_t)0));
        order.symbol       = j.value("symbol", "");
        order.side         = (j.value("side", "") == "BUY") ? OrderSide::Buy : OrderSide::Sell;
        order.quantity     = std::stof(j.value("origQty", "0"));
        order.price        = std::stof(j.value("price", "0"));
        order.stopPrice    = std::stof(j.value("stopPrice", "0"));
        order.filledQty    = std::stof(j.value("executedQty", "0"));
        order.status       = ParseBinanceStatus(j.value("status", ""));
        order.createdAt    = j.value("time", (int64_t)0) / 1000;
        order.updatedAt    = j.value("updateTime", (int64_t)0) / 1000;

        // Order type mapping
        std::string typeStr = j.value("type", "");
        if (typeStr == "MARKET") order.type = OrderType::Market;
        else if (typeStr == "LIMIT") order.type = OrderType::Limit;
        else if (typeStr == "STOP_LOSS") order.type = OrderType::Stop;
        else if (typeStr == "STOP_LOSS_LIMIT") order.type = OrderType::StopLimit;

        return order;
    }

    BrokerTick BinanceBrokerConnector::ParseTradeJson(const nlohmann::json& j) const
    {
        BrokerTick tick;
        tick.symbol    = j.value("s", "");
        tick.last      = std::stof(j.value("p", "0"));
        tick.volume    = std::stof(j.value("q", "0"));
        tick.timestamp = j.value("T", (int64_t)0) / 1000;
        tick.bid = tick.last;
        tick.ask = tick.last;
        return tick;
    }

} // namespace stnks
