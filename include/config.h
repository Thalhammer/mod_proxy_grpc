#pragma once
#include <cstdint>

typedef struct proxy_grpc_config_bool {
	bool value;
	bool initialized = false;
	proxy_grpc_config_bool& operator=(bool b) {
		this->value = b;
		this->initialized = true;
		return *this;
	}
	operator bool() const {
		return initialized && value;
	}
} proxy_grpc_config_bool_t;

typedef struct proxy_grpc_config {
	int64_t call_timeout_ms;
    int64_t max_message_size;
} proxy_grpc_config_t;


inline int64_t config_merge(int64_t add, int64_t old) {
    return add > 0 ? add : old;
}
inline proxy_grpc_config_bool_t config_merge(proxy_grpc_config_bool_t add, proxy_grpc_config_bool_t old) {
    return add.initialized ? add : old;
}