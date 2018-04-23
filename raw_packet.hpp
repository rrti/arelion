#ifndef ARELION_RAW_PACKET_HDR
#define ARELION_RAW_PACKET_HDR

#include <cassert>
#include <cstdint>
#include <cstring>

#include <utility>

namespace arelion {
	class raw_packet {
	public:
		raw_packet() = default;
		raw_packet(const raw_packet& p) = delete;

		raw_packet(const uint8_t* const raw_data, const uint32_t raw_length): length(raw_length) {
			assert(length > 0);
			data = new uint8_t[length];
			std::memcpy(data, raw_data, length);
		}
		raw_packet(const uint32_t raw_length): length(raw_length) {
			if (length == 0)
				return;

			data = new uint8_t[length];
		}

		raw_packet(raw_packet&& p) { *this = std::move(p); }
		~raw_packet() { delete_data(); }

		raw_packet& operator = (const raw_packet& p) = delete;
		raw_packet& operator = (raw_packet&& p) {
			delete_data();

			data = p.data;
			p.data = nullptr;

			length = p.length;
			p.length = 0;
			return *this;
		}

		void delete_data() {
			if (length == 0)
				return;

			delete[] data;
			data = nullptr;

			length = 0;
		}

		uint8_t* data = nullptr;
		uint32_t length = 0;
	};
}

#endif

