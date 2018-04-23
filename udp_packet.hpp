#ifndef ARELION_UDP_PACKET_HDR
#define ARELION_UDP_PACKET_HDR

#include <cstdint>

#include <list>
#include <vector>

#include <memory>


namespace util {
	struct crc32_t;
};

namespace arelion {
	struct udp_packet_chunk {
	public:
		static constexpr uint32_t hdr_size() { return (sizeof(int32_t) + sizeof(uint8_t)); }
		static constexpr uint32_t max_size() { return 254; }

		uint32_t calc_size() const { return (hdr_size() + data.size()); }
		void update_checksum(util::crc32_t& crc) const;

	public:
		int32_t chunk_number = 0;
		uint8_t chunk_size = 0;

		std::vector<uint8_t> data;
	};


	struct udp_packet {
	public:
		udp_packet(const uint8_t* data, uint32_t length);
		udp_packet(int32_t _last_continuous, int8_t _nak_type): last_continuous(_last_continuous), nak_type(_nak_type) {
		}

		static constexpr uint32_t hdr_size() { return (sizeof(int32_t) + sizeof(int8_t) + sizeof(uint8_t)); }
		static constexpr uint32_t max_size() { return 4096; }

		uint32_t calc_size() const;
		uint8_t calc_checksum(util::crc32_t& crc) const;

		void serialize(std::vector<uint8_t>& data) const;

	public:
		int32_t last_continuous = 0;
		/// if < 0, -<nak_type> packets were lost since <last_continuous>
		//  if > 0,  <nak_type> equals the number of no-acknowledge chunks
		int8_t nak_type = 0;
		uint8_t checksum = 0;

		std::vector<uint8_t> naks;
		std::list< std::shared_ptr<udp_packet_chunk> > chunks;
	};
}

#endif

