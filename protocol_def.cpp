#include "protocol_def.hpp"

#include <cassert>
#include <cstring>
#include <stdexcept>

namespace arelion {
	protocol_def proto_def;


	void protocol_def::clear() {
		std::memset(msg, 0, sizeof(msg_type) * 256);
	}

	void protocol_def::add_type(const uint8_t id, const int32_t msg_length) {
		msg[id].length = msg_length;
	}

	int32_t protocol_def::packet_length(const uint8_t* const buf, const uint32_t buf_length) const {
		if (buf_length == 0)
			return 0;

		const uint8_t msg_id = buf[0];
		const int32_t msg_len = msg[msg_id].length;

		if (msg_len > 0)
			return msg_len;

		switch (msg_len) {
			case 0: { return -2; } break;
			case -1: {
				if (buf_length < 2)
					return 0;
				return ((buf[1] >= 2)? buf[1]: -1);
			} break;
			case -2: {
				if (buf_length < 3)
					return 0;

				const uint16_t* ptr = reinterpret_cast<const uint16_t*>(buf + 1);
				const uint16_t slen = *ptr;

				return ((slen >= 3)? slen: -1);
			} break;

			default: {
			} break;
		}

		// invalid message length
		throw std::runtime_error("[protocol_def] invalid message length");
		return 0;
	}


	bool protocol_def::is_valid_length(const int32_t pkt_length, const uint32_t buf_length) const {
		return (pkt_length > 0) && (buf_length >= uint32_t(pkt_length));
	}

	bool protocol_def::is_valid_packet(const uint8_t* const buf, const uint32_t buf_length) const {
		return (is_valid_length(packet_length(buf, buf_length), buf_length));
	}
}

