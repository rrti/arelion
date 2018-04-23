#ifndef ARELION_PROTOCOL_DEF_HDR
#define ARELION_PROTOCOL_DEF_HDR

#include <cstdint>

namespace arelion {
	class protocol_def {
	public:
		void clear();
		void add_type(const uint8_t id, const int32_t msg_length);

		// <  -1: invalid id
		// == -1: invalid length
		// ==  0: unknown length, buffer too short
		// >   0: actual length
		int32_t packet_length(const uint8_t* const buf, const uint32_t buf_length) const;

		bool is_valid_length(const int32_t pkt_length, const uint32_t buf_length) const;
		bool is_valid_packet(const uint8_t* const buf, const uint32_t buf_length) const;

	private:
		struct msg_type {
			int32_t length = 0;
		};

		msg_type msg[256];
	};

	extern protocol_def proto_def;
}

#endif

