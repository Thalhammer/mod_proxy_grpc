#pragma once
#include <string>

class base64_encode_stream {
public:
    std::string m_buffer;
    std::string feed(const void* data, size_t len);
    std::string flush();
    void feed(std::string& out, const void* data, size_t len);
    void flush(std::string& out);

    static constexpr size_t guess_encoded_size(size_t inlen) noexcept { return ((inlen + 2)/3) + 4; }
};

class base64_decode_stream {
public:
    std::string m_buffer;
    std::string feed(const void* data, size_t len);
    std::string flush();
    void feed(std::string& out, const void* data, size_t len);
    void flush(std::string& out);

    static constexpr size_t guess_encoded_size(size_t inlen) noexcept { return ((inlen + 2)/3) + 4; }
};