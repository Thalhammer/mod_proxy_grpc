#include <grpc_proxy.h>
#include <grpc/grpc.h>
#include <grpc/support/log.h>
#include <grpc/support/alloc.h>
#include <grpc/byte_buffer_reader.h>
#include <cstring>
#include <cstdio>
#include <vector>
#include <mutex>

static gpr_timespec get_deadline(uint64_t timeout) {
    if(timeout == 0) return gpr_inf_future(GPR_CLOCK_REALTIME);
    auto now = gpr_now(GPR_CLOCK_REALTIME);
    auto off = gpr_time_from_millis(timeout, GPR_CLOCK_REALTIME);
    return gpr_time_add(now, off);
}

static std::mutex g_channel_cache_mtx;
static std::unordered_map<std::string, std::shared_ptr<grpc_channel>> g_channel_cache;

std::shared_ptr<grpc_channel> get_working_channel(const std::string& host) {
    std::unique_lock<std::mutex> lck(g_channel_cache_mtx);
    auto it = g_channel_cache.find(host);
    if(it != g_channel_cache.end()) {
        auto state = grpc_channel_check_connectivity_state(it->second.get(), true);
        if(state != GRPC_CHANNEL_SHUTDOWN && state != GRPC_CHANNEL_TRANSIENT_FAILURE) return it->second;
        g_channel_cache.erase(it);
    }
    grpc_channel_args args;
    args.num_args = 0;
    std::shared_ptr<grpc_channel> channel(grpc_insecure_channel_create(host.c_str(), &args, NULL),
                                            [](grpc_channel* ch){ if(ch) grpc_channel_destroy(ch); });
    if(channel.get() != nullptr) g_channel_cache.insert({host, channel});
    return channel;
}

grpc_proxy::grpc_proxy()
    : m_channel(nullptr), m_cq(nullptr), m_call(nullptr), m_call_timeout(0)
{}

grpc_proxy::~grpc_proxy() {
    if(m_call) grpc_call_unref(m_call);
    if(m_cq) grpc_completion_queue_destroy(m_cq);
    m_channel.reset();
}

bool grpc_proxy::start(const char* host, const char* method)
{
    m_channel = get_working_channel(host);
    if(!m_channel) {
        gpr_log(__FILE__, __LINE__, GPR_LOG_SEVERITY_ERROR, "Failed to create channel");
        return false;
    }

    // Create cq
    grpc_completion_queue_attributes att;
    att.version = GRPC_CQ_CURRENT_VERSION;
    att.cq_completion_type = GRPC_CQ_NEXT;
    att.cq_polling_type = GRPC_CQ_DEFAULT_POLLING;
    auto factory = grpc_completion_queue_factory_lookup(&att);
    if(!factory) {
        gpr_log(__FILE__, __LINE__, GPR_LOG_SEVERITY_ERROR, "Failed to get cq factory");
        return false;
    }
    m_cq = grpc_completion_queue_create(factory, &att, NULL);
    if(!m_cq) {
        gpr_log(__FILE__, __LINE__, GPR_LOG_SEVERITY_ERROR, "Failed to create cq");
        return false;
    }

    auto host_slice = grpc_slice_from_copied_string(host);
    auto method_slice = grpc_slice_from_copied_string(method);
    m_call = grpc_channel_create_call(m_channel.get(), NULL, 0, m_cq, method_slice, &host_slice, get_deadline(m_call_timeout), NULL);
    grpc_slice_unref(host_slice);
    grpc_slice_unref(method_slice);
    if(!m_call) {
        gpr_log(__FILE__, __LINE__, GPR_LOG_SEVERITY_ERROR, "Failed to initiate call");
        return false;
    }

    return true;
}

static auto queue_pluck(grpc_completion_queue* cq, void* tag, gpr_timespec deadline, void *reserved) {
    auto res = grpc_completion_queue_next(cq, deadline, reserved);
    while(res.type == GRPC_OP_COMPLETE && res.success) {
        if(res.tag == tag) break;
        res = grpc_completion_queue_next(cq, deadline, reserved);
    }
    return res;
}

grpc_event grpc_proxy::run_ops(grpc_call* call, grpc_completion_queue* cq, grpc_op* ops, int mops) {
    if(grpc_call_start_batch(call, ops, mops, (void*)0xdeadbeef, NULL) != GRPC_CALL_OK) {
        gpr_log(__FILE__, __LINE__, GPR_LOG_SEVERITY_ERROR, "Failed to start call op");
        grpc_event e = {};
        e.success = false;
        return e;
    }
    auto e = queue_pluck(cq, (void*)0xdeadbeef, get_deadline(m_call_timeout), nullptr);
    if(e.type != GRPC_OP_COMPLETE) {
        gpr_log(__FILE__, __LINE__, GPR_LOG_SEVERITY_ERROR, "Failed to pluck call op");
    }
    return e;
}

static void parse_metadata(grpc_metadata_array& array, std::unordered_multimap<std::string, std::string>& out) {
    for(size_t i=0; i<array.count; i++) {
        auto e = &array.metadata[i];
        std::string key(
            reinterpret_cast<const char*>(GRPC_SLICE_START_PTR(e->key)),
            GRPC_SLICE_LENGTH(e->key));
        std::string value(
            reinterpret_cast<const char*>(GRPC_SLICE_START_PTR(e->value)),
            GRPC_SLICE_LENGTH(e->value));
        out.emplace(std::move(key), std::move(value));
    }
}

bool grpc_proxy::send_initial_metadata(const std::unordered_multimap<std::string, std::string>& headers) {
    std::vector<grpc_metadata> meta;
    for(auto& e : headers) {
        // grpc set headers are ignored
        if(e.first == "te") continue;
        if(e.first == "content-type") continue;
        if(e.first == "user-agent") continue;
        if(e.first == "grpc-accept-encoding") continue;
        if(e.first == "grpc-encoding") continue;
        if(e.first == "accept-encoding") continue;
        if(e.first == "content-length") continue;
        if(e.first == "connection") continue;
        grpc_metadata m = {};
        m.key = grpc_slice_from_static_string(e.first.c_str());
        m.value = grpc_slice_from_static_string(e.second.c_str());
        m.flags = 0;
        meta.push_back(m);
    }
    grpc_op op = {};
    op.op = GRPC_OP_SEND_INITIAL_METADATA;
    op.data.send_initial_metadata.count = meta.size();
    op.data.send_initial_metadata.metadata = meta.data();
    auto e = run_ops(m_call, m_cq, &op, 1);
    for(auto& e : meta) {
        grpc_slice_unref(e.key);
        grpc_slice_unref(e.value);
    }
    return e.success;
}

bool grpc_proxy::send_request(const void* data, size_t len) {
    grpc_op op = {};
    op.op = GRPC_OP_SEND_MESSAGE;
    auto slice = grpc_slice_from_static_buffer(data, len);
    op.data.send_message.send_message = grpc_raw_byte_buffer_create(&slice, 1);
    grpc_slice_unref(slice);
    auto e = run_ops(m_call, m_cq, &op, 1);
    grpc_byte_buffer_destroy(op.data.send_message.send_message);
    return e.success && e.type == GRPC_OP_COMPLETE;
}

bool grpc_proxy::send_client_close() {
    grpc_op op = {};
    op.op = GRPC_OP_SEND_CLOSE_FROM_CLIENT;
    auto e = run_ops(m_call, m_cq, &op, 1);
    return e.success && e.type == GRPC_OP_COMPLETE;
}

bool grpc_proxy::receive_initial_metadata(std::unordered_multimap<std::string, std::string>& headers) {
    grpc_metadata_array array = {};
    grpc_metadata_array_init(&array);
    grpc_op op = {};
    op.op = GRPC_OP_RECV_INITIAL_METADATA;
    op.data.recv_initial_metadata.recv_initial_metadata = &array;
    auto e = run_ops(m_call, m_cq, &op, 1);
    parse_metadata(array, headers);
    grpc_metadata_array_destroy(&array);
    return e.success && e.type == GRPC_OP_COMPLETE;
}

bool grpc_proxy::receive_message(std::function<void(const void* data, size_t len)> cb) {
    grpc_byte_buffer* payload = {};
    grpc_op op = {};
    op.op = GRPC_OP_RECV_MESSAGE;
    op.data.recv_message.recv_message = &payload;
    auto e = run_ops(m_call, m_cq, &op, 1);
    if(e.type != GRPC_OP_COMPLETE || !payload) return false;

    std::vector<uint8_t> data;
    grpc_byte_buffer_reader reader;
    grpc_byte_buffer_reader_init(&reader, payload);
    grpc_slice slice;
    while (grpc_byte_buffer_reader_next(&reader, &slice)) {
        auto s = data.size();
        data.resize(s + GRPC_SLICE_LENGTH(slice));
        memcpy(&data[s], GRPC_SLICE_START_PTR(slice), GRPC_SLICE_LENGTH(slice));
        grpc_slice_unref(slice);
    }
    grpc_byte_buffer_reader_destroy(&reader);
    grpc_byte_buffer_destroy(payload);
    payload = nullptr;
    if(cb) cb(data.data(), data.size());

    return e.success;
}

bool grpc_proxy::receive_status(grpc_proxy::status& s) {
    grpc_metadata_array array = {};
    grpc_metadata_array_init(&array);
    const char* str = nullptr;
    grpc_status_code code = {};
    grpc_slice status_details = {};
    grpc_op op = {};
    op.op = GRPC_OP_RECV_STATUS_ON_CLIENT;
    op.data.recv_status_on_client.trailing_metadata = &array;
    op.data.recv_status_on_client.status = &code;
    op.data.recv_status_on_client.error_string = &str;
    op.data.recv_status_on_client.status_details = &status_details;
    auto e = run_ops(m_call, m_cq, &op, 1);
    s.status = static_cast<int>(code);
    if(str) {
        s.error = std::string(str);
        gpr_free(const_cast<char*>(str));
    }
    if(!GRPC_SLICE_IS_EMPTY(status_details)) {
        s.details.clear();
        s.details.append(reinterpret_cast<const char*>(GRPC_SLICE_START_PTR(status_details)), GRPC_SLICE_LENGTH(status_details));
    }
    grpc_slice_unref(status_details);
    parse_metadata(array, s.metadata);
    grpc_metadata_array_destroy(&array);
    return e.success  && e.type == GRPC_OP_COMPLETE;
}

void grpc_proxy::process_init() noexcept {
    gpr_set_log_verbosity(GPR_LOG_SEVERITY_DEBUG);
	grpc_tracer_set_enabled("api", true);
    grpc_init();
}

void grpc_proxy::process_deinit() noexcept {
    std::unique_lock<std::mutex> lck(g_channel_cache_mtx);
    g_channel_cache.clear();
    lck.unlock();
    grpc_shutdown();
}

bool grpc_proxy::is_backend_alive(const std::string& host) noexcept {
    auto ch = get_working_channel(host);
    return ch != nullptr;
}