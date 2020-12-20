#include <base64.h>
#include <array>
#include <cassert>

static constexpr std::array<char, 64> encode_table {
		{'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P',
		'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z', 'a', 'b', 'c', 'd', 'e', 'f',
		'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v',
		'w', 'x', 'y', 'z', '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', '+', '/'}
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
    if(!m_buffer.empty()) {
        if(m_buffer.size() == 1) { m_buffer += *bin; bin++; len--; }
        m_buffer += *bin;
        bin++; len--;
        assert(m_buffer.size() == 3);
        uint32_t octet_a = static_cast<unsigned char>(m_buffer[0]);
		uint32_t octet_b = static_cast<unsigned char>(m_buffer[1]);
		uint32_t octet_c = static_cast<unsigned char>(m_buffer[2]);
        m_buffer.clear();

		uint32_t triple = (octet_a << 0x10) + (octet_b << 0x08) + octet_c;
		out += encode_table[(triple >> 3 * 6) & 0x3F];
		out += encode_table[(triple >> 2 * 6) & 0x3F];
		out += encode_table[(triple >> 1 * 6) & 0x3F];
		out += encode_table[(triple >> 0 * 6) & 0x3F];
    }

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