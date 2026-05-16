#pragma once

#include <string>
#include <vector>
#include <utility>
#include <functional>
#include <future>
#include <Threading/ThreadRegistry.hpp>

namespace stnks
{
    struct HttpResponse
    {
        long        statusCode = 0;
        std::string body;
        std::string error;
        bool Ok() const { return statusCode >= 200 && statusCode < 300 && error.empty(); }
    };

    class HttpClient
    {
    public:
        explicit HttpClient(ThreadRegistry& threads);

        // Synchronous GET (call from any thread)
        HttpResponse Get(const std::string& url);

        // Synchronous POST with JSON body and optional headers
        HttpResponse Post(const std::string& url, const std::string& body,
                          const std::vector<std::pair<std::string,std::string>>& headers = {});

        // Synchronous PUT with JSON body
        HttpResponse Put(const std::string& url, const std::string& body,
                         const std::vector<std::pair<std::string,std::string>>& headers = {});

        // Synchronous DELETE
        HttpResponse Delete(const std::string& url);

        // Async GET — submitted to ThreadRegistry pool, returns future
        std::future<HttpResponse> GetAsync(const std::string& url);

        ThreadRegistry& GetThreads() { return threads_; }

    private:
        ThreadRegistry& threads_;
    };

} // namespace stnks
