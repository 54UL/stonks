#pragma once

#include <string>
#include <functional>
#include <unordered_map>
#include <mutex>
#include <memory>
#include <thread>
#include <atomic>
#include <cstdint>
#include <vector>

namespace stnks
{
    // Inbound webhook event from a broker or external service.
    struct WebhookEvent
    {
        std::string source;       // e.g., "gbm", "metatrader", "tradingview"
        std::string eventType;    // e.g., "order.filled", "price.alert", "signal"
        std::string body;         // Raw JSON body
        std::string signature;    // X-Signature header for HMAC validation
        int64_t     receivedAt = 0;

        // Parsed fields (populated by handler, not receiver)
        std::string symbol;
        float       price     = 0.f;
        std::string orderId;
    };

    // Handler callback type
    using WebhookHandler = std::function<void(const WebhookEvent& event)>;

    // HTTP server that receives webhook callbacks from brokers.
    //
    // Brokers push events here when:
    //   - Orders are filled/cancelled/rejected
    //   - Price alerts trigger
    //   - Positions change
    //   - Account events (margin call, etc.)
    //
    // Security:
    //   - HMAC-SHA256 signature validation per source
    //   - IP whitelist (optional)
    //   - Rate limiting
    //
    // Usage:
    //   WebhookReceiver receiver;
    //   receiver.SetSecret("broker", "my-hmac-secret");
    //   receiver.RegisterHandler("broker", "order.filled", [](const WebhookEvent& e) { ... });
    //   receiver.Start(8101);
    //   // Broker POSTs to http://your-server:8101/webhook/broker
    //   receiver.Stop();
    //
    // Endpoint format:
    //   POST /webhook/{source}
    //   Headers: X-Signature: sha256=<hmac>, Content-Type: application/json
    //   Body: broker-specific JSON payload
    class WebhookReceiver
    {
    public:
        WebhookReceiver();
        ~WebhookReceiver();

        // Start listening on the specified port
        void Start(int port);

        // Stop the server
        void Stop();

        bool IsRunning() const { return running_.load(); }
        int  GetPort() const { return port_; }

        // Set HMAC secret for a source (used to validate X-Signature header)
        void SetSecret(const std::string& source, const std::string& secret);

        // Register a handler for a specific source + event type
        // If eventType is "*", handler receives all events from that source
        void RegisterHandler(const std::string& source,
                             const std::string& eventType,
                             WebhookHandler handler);

        // Optional: set allowed IP addresses for a source
        void SetAllowedIPs(const std::string& source, const std::vector<std::string>& ips);

        // Get count of received webhooks (for diagnostics)
        uint64_t GetReceivedCount() const { return receivedCount_.load(); }
        uint64_t GetRejectedCount() const { return rejectedCount_.load(); }

    private:
        struct SourceConfig
        {
            std::string secret;
            std::vector<std::string> allowedIPs;
            std::unordered_map<std::string, WebhookHandler> handlers;  // eventType → handler
        };

        bool ValidateSignature(const std::string& source,
                               const std::string& body,
                               const std::string& signature) const;

        bool ValidateIP(const std::string& source, const std::string& remoteIP) const;

        void DispatchEvent(const WebhookEvent& event);

        mutable std::mutex configMutex_;
        std::unordered_map<std::string, SourceConfig> sources_;

        std::unique_ptr<std::thread> serverThread_;
        std::atomic<bool>            running_{false};
        int                          port_ = 0;

        std::atomic<uint64_t> receivedCount_{0};
        std::atomic<uint64_t> rejectedCount_{0};

        // Opaque server handle (cpp-httplib Server*)
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };

} // namespace stnks
