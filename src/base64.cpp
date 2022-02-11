#include <base64.h>
#include <array>
#include <cassert>

static constexpr std::array<char, 64> encode_table {
		{'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P',
		'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z', 'a', 'b', 'c', 'd', 'e', 'f',
		'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v',
		'w', 'x', 'y', 'z', '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', '+', '/'}
};

static constexpr std::array<uint8_t, 256> decode_table = {
	 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, 
	 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, 
	 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, 62,  0,  0,  0, 63, 
	52, 53, 54, 55, 56, 57, 58, 59, 60, 61,  0,  0,  0,  0,  0,  0, 
	 0,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 
	15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25,  0,  0,  0,  0,  0, 
	 0, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 
	41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51,  0,  0,  0,  0,  0, 
	 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, 
	 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, 
	 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, 
	 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, 
	 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, 
	 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, 
	 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, 
	 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, 
};

std::string base64_encode_stream::feed(const void* data, size_t len) {
	std::string out;
	feed(out, data, len);
	return out;
}

std::string base64_encode_stream::flush() {
	std::string out;
	flush(out);
	return out;
}

void base64_encode_stream::feed(std::string& out, const void* data, size_t len) {
    auto full_len = guess_encoded_size(m_buffer.size() + len);
    out.reserve(out.size() + full_len);

    auto bin = reinterpret_cast<const char*>(data);
	while(len > 0 && m_buffer.size() != 3 && !m_buffer.empty()) {
		m_buffer += *bin;
		bin++;
		len--;
	}
    if(!m_buffer.empty()) {
		// We stored all our data in the buffer but where unable to fill it
		if(m_buffer.size() != 3) return;
        uint32_t octet_a = static_cast<unsigned char>(m_buffer[0]);
		uint32_t octet_b = static_cast<unsigned char>(m_buffer[1]);
		uint32_t octet_c = static_cast<unsigned char>(m_buffer[2]);

		uint32_t triple = (octet_a << 0x10) + (octet_b << 0x08) + octet_c;
		out += encode_table[(triple >> 3 * 6) & 0x3F];
		out += encode_table[(triple >> 2 * 6) & 0x3F];
		out += encode_table[(triple >> 1 * 6) & 0x3F];
		out += encode_table[(triple >> 0 * 6) & 0x3F];
		
        m_buffer.clear();
    }

	assert(m_buffer.empty());

    // clear incomplete bytes
	size_t fast_size = len - len % 3;
	for (size_t i = 0; i < fast_size;) {
		uint32_t octet_a = static_cast<unsigned char>(bin[i++]);
		uint32_t octet_b = static_cast<unsigned char>(bin[i++]);
		uint32_t octet_c = static_cast<unsigned char>(bin[i++]);

		uint32_t triple = (octet_a << 0x10) + (octet_b << 0x08) + octet_c;

		out += encode_table[(triple >> 3 * 6) & 0x3F];
		out += encode_table[(triple >> 2 * 6) & 0x3F];
		out += encode_table[(triple >> 1 * 6) & 0x3F];
		out += encode_table[(triple >> 0 * 6) & 0x3F];
	}

	for(size_t i = fast_size; i < len; i++) m_buffer += bin[i];
}

void base64_encode_stream::flush(std::string& out) {
	if(m_buffer.empty()) return;

	out.reserve(out.size() + 4);

	auto len = m_buffer.size();
	uint32_t octet_a = static_cast<unsigned char>(m_buffer[0]);
	uint32_t octet_b = len > 1 ? static_cast<unsigned char>(m_buffer[1]) : 0;
	m_buffer.clear();
	uint32_t triple = (octet_a << 0x10) + (octet_b << 0x08);

	switch (len % 3) {
	case 1:
		out += encode_table[(triple >> 3 * 6) & 0x3F];
		out += encode_table[(triple >> 2 * 6) & 0x3F];
		out += '=';
		out += '=';
		break;
	case 2:
		out += encode_table[(triple >> 3 * 6) & 0x3F];
		out += encode_table[(triple >> 2 * 6) & 0x3F];
		out += encode_table[(triple >> 1 * 6) & 0x3F];
		out += '=';
		break;
	default:
		break;
	}
}

std::string base64_decode_stream::feed(const void* data, size_t len) {
	std::string out;
	feed(out, data, len);
	return out;
}

std::string base64_decode_stream::flush() {
	std::string out;
	flush(out);
	return out;
}

static inline void decode_4(std::string& out, const void* pdata) {
    auto data = reinterpret_cast<const uint8_t*>(pdata);
    uint32_t triple = static_cast<uint32_t>(decode_table[data[0]]) << 18;
    triple |= static_cast<uint32_t>(decode_table[data[1]]) << 12;
    triple |= static_cast<uint32_t>(decode_table[data[2]]) << 6;
    triple |= static_cast<uint32_t>(decode_table[data[3]]);
    
    out += static_cast<char>((triple >> 16) & 0xFF);
    out += static_cast<char>((triple >> 8) & 0xFF);
    out += static_cast<char>((triple >> 0) & 0xFF);
}

static inline void decode_4_padding(std::string& out, const void* pdata) {
    auto data = reinterpret_cast<const uint8_t*>(pdata);
    uint32_t triple = static_cast<uint32_t>(decode_table[data[0]]) << 18;
    triple |= static_cast<uint32_t>(decode_table[data[1]]) << 12;
    triple |= static_cast<uint32_t>(decode_table[data[2]]) << 6;
    triple |= static_cast<uint32_t>(decode_table[data[3]]);
    
    out += static_cast<char>((triple >> 16) & 0xFF);
    if(data[2] != '=') out += static_cast<char>((triple >> 8) & 0xFF);
    if(data[3] != '=') out += static_cast<char>((triple >> 0) & 0xFF);
}

void base64_decode_stream::feed(std::string& out, const void* pdata, size_t len) {
    out.reserve(out.size() + ((len+4)*3)/4);
    auto data = reinterpret_cast<const uint8_t*>(pdata);
    while(len > 0 && data[len-1] == '=') len--;
    if(!m_buffer.empty()) {
        while(m_buffer.size() < 4 && len > 0) {
            m_buffer += *data;
            data++;
            len--;
        }
        if(m_buffer.size() == 4) {
            decode_4(out, m_buffer.data());
            m_buffer.clear();
        }
    }
    for (size_t i = 0; i < (len & ~3); i+=4) {
        decode_4(out, &data[i]);
    }
    if(len%4 !=0) m_buffer.append(reinterpret_cast<const char*>(&data[len&~3]), len%4);
}

void base64_decode_stream::flush(std::string& out) {
    if(m_buffer.empty()) return;
    out.reserve(out.size() + 3);
    m_buffer.resize(4, '=');
    decode_4_padding(out, m_buffer.data());
    m_buffer.clear();
}