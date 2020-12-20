#pragma once
extern "C" {
#include <httpd.h>
#include <apr_pools.h>
}
#include <string>
#include <unordered_map>
#include <algorithm>

template<typename Func>
size_t read_body(Func func, request_rec* r) {

	size_t total_size = 0;
	bool seen_eos = false;
	auto *bb = apr_brigade_create(r->pool, r->connection->bucket_alloc);
    do {
        apr_bucket *bucket = NULL, *last = NULL;

        int rv = ap_get_brigade(r->input_filters, bb, AP_MODE_READBYTES, APR_BLOCK_READ, HUGE_STRING_LEN);
        if (rv != APR_SUCCESS) {
            apr_brigade_destroy(bb);
            break;
        }

        for (bucket = APR_BRIGADE_FIRST(bb);
             bucket != APR_BRIGADE_SENTINEL(bb);
             last = bucket, bucket = APR_BUCKET_NEXT(bucket)) {
            const char *data;
            apr_size_t len;

            if (last) {
                apr_bucket_delete(last);
            }
            if (APR_BUCKET_IS_EOS(bucket)) {
                seen_eos = true;
                break;
            }
            if (bucket->length == 0) {
                continue;
            }

            rv = apr_bucket_read(bucket, &data, &len, APR_BLOCK_READ);
            if (rv != APR_SUCCESS) {
				apr_brigade_destroy(bb);
				seen_eos = true;
				break;
			}

			if(!func(data, len))
				break;
			total_size += len;
        }

        apr_brigade_cleanup(bb);
	} while (!seen_eos);

	return total_size;
}

inline std::unordered_multimap<std::string, std::string> convert_table(apr_table_t* table, bool lower_keys = false) {
    std::unordered_multimap<std::string, std::string> res;
    if(lower_keys) {
        apr_table_do([](void *rec, const char *key, const char *value){
            auto r = static_cast<std::unordered_multimap<std::string, std::string>*>(rec);
            std::string k(key);
            std::transform(k.begin(), k.end(), k.begin(), ::tolower);
            r->emplace(std::move(k), std::string(value));
            return 1;
        }, &res, table, NULL);
    } else {
        apr_table_do([](void *rec, const char *key, const char *value){
            auto r = static_cast<std::unordered_multimap<std::string, std::string>*>(rec);
            r->emplace(std::string(key), std::string(value));
            return 1;
        }, &res, table, NULL);
    }
    return res;
}

inline int64_t detect_content_length(request_rec *r) {
    int64_t result = -1;
    apr_table_do([](void *rec, const char *key, const char *value){
        if(strcasecmp("Content-Length", key) == 0) {
            *static_cast<int64_t*>(rec) = strtoll(value, nullptr, 10);
            return 0;
        }
        return 1;
    }, &result, r->headers_in, NULL);
    return result;
}

std::string read_body_base64(request_rec* r) {
    bool failed = false;
    std::string res;
    read_body([&](const char* data, size_t len) -> bool {
        for(size_t i = 0; i < len; i++) {
            if(data[i] >='A' && data[i] <= 'Z') continue;
            if(data[i] >='a' && data[i] <= 'z') continue;
            if(data[i] >='0' && data[i] <= '9') continue;
            if(data[i] == '+' || data[i] == '/' || data[i] == '=') continue;
            // Invalid character
            failed = true;
            return false;
        }
        res.append(data, len);
        return true;
    }, r);
    if(failed) return "";
    std::string decoded;
    decoded.resize(res.size());
    auto len = apr_base64_decode_binary((unsigned char*)decoded.data(), res.c_str());
    decoded.resize(len);
    return decoded;
}

template<typename T>
T* pool_calloc(apr_pool_t* p) {
    return static_cast<T*>(apr_pcalloc(p, sizeof(T)));
}