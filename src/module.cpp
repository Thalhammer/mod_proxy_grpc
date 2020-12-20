extern "C" {
#include <httpd.h>
#include <http_config.h>
#include <http_request.h>
#include <ap_config.h>
#include <mod_proxy.h>
#include <apr_base64.h>
}
#include <utils.h>
#include <config.h>
#include <grpc_proxy.h>
#include <base64.h>
#include <grpc/support/log.h>

static grpc_completion_queue* create_cq() noexcept;
static void proxy_grpc_register_hooks(apr_pool_t *p) noexcept;

/** ========= Config support ========== **/
static const char* proxy_grpc_set_max_message_size(cmd_parms* cmd, void* cfg, const char* arg) noexcept;
static const char* proxy_grpc_set_calltimeout(cmd_parms* cmd, void* cfg, const char* arg) noexcept;
static void* proxy_grpc_create_dir_conf(apr_pool_t* pool, char* context) noexcept;
static void* proxy_grpc_merge_dir_conf(apr_pool_t* pool, void* BASE, void* ADD) noexcept;

const command_rec proxy_grpc_directives[] = {
    AP_INIT_TAKE1("grpcMaxMessageSize", (cmd_func)proxy_grpc_set_max_message_size, NULL, ACCESS_CONF | RSRC_CONF, "Set GRPC Service host (and port)"),
    AP_INIT_TAKE1("grpcCallTimeout", (cmd_func)proxy_grpc_set_calltimeout, NULL, ACCESS_CONF | RSRC_CONF, "Set call timeout"),
    { NULL }
};

extern "C" {
    /* Dispatch list for API hooks */
    AP_DECLARE_MODULE(proxy_grpc) = {
        STANDARD20_MODULE_STUFF, 
        proxy_grpc_create_dir_conf, /* create per-dir    config structures */
        proxy_grpc_merge_dir_conf,  /* merge  per-dir    config structures */
        NULL,                       /* create per-server config structures */
        NULL,                       /* merge  per-server config structures */
        proxy_grpc_directives,      /* table of config file commands       */
        proxy_grpc_register_hooks   /* register hooks                      */
    };
}

static int proxy_grpc_handler_options(request_rec *r, proxy_worker *worker, proxy_server_conf *conf, char *url, const char *proxyname, apr_port_t proxyport) {
    if(!grpc_proxy::is_backend_alive(proxyname)) return HTTP_SERVICE_UNAVAILABLE;
    r->status = 200;
    r->status_line = apr_pstrdup(r->pool, "OK");
    return DONE;
}

static int proxy_grpc_handler_post(request_rec *r, proxy_worker *worker, proxy_server_conf *conf, char *url, const char *proxyname, apr_port_t proxyport) {
    const auto cfg = static_cast<const proxy_grpc_config_t*>(ap_get_module_config(r->per_dir_config, &proxy_grpc_module));
    const auto headers_in = convert_table(r->headers_in, true);
    
    auto content_type = headers_in.find("content-type");
    auto accept = headers_in.find("accept");
    if(content_type == headers_in.end()) return DECLINED;
    // We only support base64 encoded protobuf messages atm
    if(strcasecmp(content_type->second.c_str(), "application/grpc-web-text") != 0) return HTTP_BAD_REQUEST;
    // We only support base64 encoded protobuf messages atm
    if(accept != headers_in.end() && strcasecmp(accept->second.c_str(), "application/grpc-web-text") != 0) return HTTP_BAD_REQUEST;

    r->content_type = apr_pstrdup(r->pool, accept->second.c_str());

    auto content_length = detect_content_length(r);
    if(content_length < 0) return HTTP_LENGTH_REQUIRED;
    auto max_size = cfg->max_message_size < 0 ? 4*1024*1024 : cfg->max_message_size;
    if((content_length*3)/4 > max_size) return HTTP_REQUEST_ENTITY_TOO_LARGE;

    auto str = read_body_base64(r);
    if(str.size() >= 5) str = str.substr(5);
    else str.clear();

    grpc_proxy proxy;
    proxy.set_call_timeout(std::max<int64_t>(cfg->call_timeout_ms, 0));
    if(!proxy.start(proxyname, url)) return HTTP_SERVICE_UNAVAILABLE;
    if(!proxy.send_initial_metadata(headers_in)) return HTTP_SERVICE_UNAVAILABLE;
    if(!str.empty() && !proxy.send_request(str.data(), str.size())) return HTTP_SERVICE_UNAVAILABLE;
    if(!proxy.send_client_close()) return HTTP_SERVICE_UNAVAILABLE;
    std::unordered_multimap<std::string, std::string> headers_out;
    if(!proxy.receive_initial_metadata(headers_out)) return HTTP_SERVICE_UNAVAILABLE;
    base64_encode_stream stream;
    while(proxy.receive_message([&stream, r](const void* data, size_t len){
        uint8_t hdr[5] = {};
        hdr[1] = (len >> 24) & 0xff;
        hdr[2] = (len >> 16) & 0xff;
        hdr[3] = (len >> 8) & 0xff;
        hdr[4] = (len >> 0) & 0xff;
        std::string buf;
        buf.reserve(base64_encode_stream::guess_encoded_size(sizeof(hdr) + len));
        stream.feed(buf, hdr, sizeof(hdr));
        stream.feed(buf, data, len);
        stream.flush(buf);
        ap_rwrite(buf.data(), buf.size(), r);
        ap_rflush(r);
    }));
    grpc_proxy::status status;
    if(!proxy.receive_status(status)) return HTTP_SERVICE_UNAVAILABLE;
    // Write trailer
    {
        std::string trailer;
        trailer += "grpc-status:";
        trailer += std::to_string(status.status);
        trailer += "\r\ngrpc-message:";
        trailer += status.details;
        if(!status.error.empty()) {
            trailer += "\r\ngrpc-error:";
            trailer += status.error;
        }
        trailer += "\r\n";
        for(auto& e : status.metadata) {
            trailer += e.first + ":" + e.second + "\r\n";
        }

        auto len = trailer.size();
        uint8_t hdr[5] = {};
        hdr[0] = 0x80;
        hdr[1] = (len >> 24) & 0xff;
        hdr[2] = (len >> 16) & 0xff;
        hdr[3] = (len >> 8) & 0xff;
        hdr[4] = (len >> 0) & 0xff;
        std::string buf;
        buf.reserve(base64_encode_stream::guess_encoded_size(sizeof(hdr) + len));
        stream.feed(buf, hdr, sizeof(hdr));
        stream.feed(buf, trailer.data(), trailer.size());
        stream.flush(buf);
        ap_rwrite(buf.data(), buf.size(), r);
        ap_rflush(r);
    }

    return DONE;
}

static int proxy_grpc_handler(request_rec *r, proxy_worker *worker, proxy_server_conf *conf, char *url, const char *proxyname, apr_port_t proxyport) {
    if (strncasecmp(url, "grpc://", 7) != 0) return DECLINED;
    url += 7;

    auto pos = strchr(url, '/');
    if(pos) {
        proxyname = apr_pstrndup(r->pool, url, pos-url);
        url = pos;
    }

    if(r->method_number == M_OPTIONS) {
        return proxy_grpc_handler_options(r, worker, conf, url, proxyname, proxyport);
    } else if(r->method_number == M_POST) {
        return proxy_grpc_handler_post(r, worker, conf, url, proxyname, proxyport);
    } else return DECLINED;
}

static void proxy_grpc_child_init(apr_pool_t *pchild, server_rec *s) noexcept {
    static server_rec* server = s;
    gpr_set_log_function([](gpr_log_func_args *args) {
        const char *fname = args->file;
        auto rslash = strrchr((char*)fname, '/');
        if (rslash != NULL) fname = rslash + 1;
        int level = APLOG_ERR;
        switch(args->severity) {
            case gpr_log_severity::GPR_LOG_SEVERITY_DEBUG: level = APLOG_DEBUG; break;
            case gpr_log_severity::GPR_LOG_SEVERITY_INFO: level = APLOG_INFO; break;
            default: break;
        }
        ap_log_error(fname, args->line, APLOG_MODULE_INDEX, level, 0, server, "%s", args->message);
    });
    grpc_proxy::process_init();
    apr_pool_cleanup_register(pchild, nullptr, [](void*)->apr_status_t{
        grpc_proxy::process_deinit();
    }, apr_pool_cleanup_null);
}

static void proxy_grpc_register_hooks(apr_pool_t *p) noexcept {
    proxy_hook_scheme_handler(proxy_grpc_handler, NULL, NULL, APR_HOOK_FIRST);
    ap_hook_child_init(proxy_grpc_child_init, NULL, NULL, APR_HOOK_MIDDLE);
}

/** ========= Config support ========== **/

static const char* proxy_grpc_set_max_message_size(cmd_parms* cmd, void* cfg, const char* arg) noexcept {
    auto* config = static_cast<proxy_grpc_config_t*>(cfg);
    if(config) {
        config->max_message_size = strtol(arg, nullptr, 10);
        if(config->max_message_size < 1024)
            config->max_message_size = 1024;
    }
    return nullptr;
}

static const char* proxy_grpc_set_calltimeout(cmd_parms* cmd, void* cfg, const char* arg) noexcept {
    auto* config = static_cast<proxy_grpc_config_t*>(cfg);
    if(config) {
        config->call_timeout_ms = strtol(arg, nullptr, 10);
        if(config->call_timeout_ms < 0)
            config->call_timeout_ms = 0;
    }
    return nullptr;
}

static void* proxy_grpc_create_dir_conf(apr_pool_t* pool, char* context) noexcept {
    auto config = pool_calloc<proxy_grpc_config_t>(pool);

    if(config) {
        config->call_timeout_ms = -1;
        config->max_message_size = -1;
    }

    return config;
}

static void* proxy_grpc_merge_dir_conf(apr_pool_t* pool, void* BASE, void* ADD) noexcept {
    auto* base = static_cast<proxy_grpc_config_t*>(BASE);
    auto* add = static_cast<proxy_grpc_config_t*>(ADD);
    auto* conf = static_cast<proxy_grpc_config_t*>(proxy_grpc_create_dir_conf(pool, nullptr));

    conf->call_timeout_ms = config_merge(add->call_timeout_ms, base->call_timeout_ms);
    conf->max_message_size = config_merge(add->max_message_size, base->max_message_size);

    return conf;
}
