#pragma once
#include <cstddef>
#include <functional>
#include <unordered_map>
#include <string>
#include <memory>

struct grpc_completion_queue;
struct grpc_channel;
struct grpc_call;
struct grpc_op;
struct grpc_event;

class grpc_proxy {
    grpc_proxy& operator=(const grpc_proxy&) = delete;
    grpc_proxy& operator=(grpc_proxy&&) = delete;
    grpc_proxy(const grpc_proxy&) = delete;
    grpc_proxy(grpc_proxy&&) = delete;

    grpc_completion_queue* m_cq = nullptr;
    std::shared_ptr<grpc_channel> m_channel = nullptr;
    grpc_call* m_call = nullptr;

    uint64_t m_call_timeout;

    grpc_event run_ops(grpc_call* call, grpc_completion_queue* cq, grpc_op* ops, int mops);
public:
    struct status {
        int status;
        std::string details;
        std::string error;
        std::unordered_multimap<std::string, std::string> metadata;
    };

    grpc_proxy();
    ~grpc_proxy();

    void set_call_timeout(uint64_t ms) noexcept { m_call_timeout = ms; }

    bool start(const char* host, const char* method);
    // TODO: We might want to batch those 3 together to optimize performance
    bool send_initial_metadata(const std::unordered_multimap<std::string, std::string>& headers);
    bool send_request(const void* data, size_t len);
    bool send_client_close();
    bool receive_initial_metadata(std::unordered_multimap<std::string, std::string>& headers);
    bool receive_message(std::function<void(const void* data, size_t len)> cb);
    bool receive_status(status& s);

    static void process_init() noexcept;
    static void process_deinit() noexcept;
    static bool is_backend_alive(const std::string& host) noexcept;
};