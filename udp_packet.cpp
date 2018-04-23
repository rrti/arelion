#include "udp_packet.hpp"
#include "packet_packer.hpp"
#include "packet_unpacker.hpp"
#include "util.hpp"

namespace arelion {
	void udp_packet_chunk::update_checksum(util::crc32_t& crc) const {
		crc.update(chunk_number);
		crc.update(static_cast<uint32_t>(chunk_size));

		if (data.empty())
			return;

		crc.update(&data[0], data.size());
	}


	udp_packet::udp_packet(const uint8_t* data, uint32_t length) {
		packet_unpacker buf(data, length);
		buf.unpack(last_continuous);
		buf.unpack(nak_type);
		buf.unpack(checksum);

		if (nak_type > 0) {
			naks.reserve(nak_type);

			for (size_t i = 0, n = nak_type; i != n; ++i) {
				if (buf.bytes_remaining() < sizeof(naks[i]))
					break;

				if (naks.size() <= i)
					naks.push_back(0);

				buf.unpack(naks[i]);
			}
		}

		while (buf.bytes_remaining() > udp_packet_chunk::hdr_size()) {
			std::shared_ptr<udp_packet_chunk> chunk(new udp_packet_chunk());

			buf.unpack(chunk->chunk_number);
			buf.unpack(chunk->chunk_size);

			// defective, ignore
			if (buf.bytes_remaining() < chunk->chunk_size)
				break;

			buf.unpack(chunk->data, chunk->chunk_size);
			chunks.push_back(chunk);
		}
	}


	uint32_t udp_packet::calc_size() const {
		uint32_t size = hdr_size() + naks.size();

		for (const auto& chunk: chunks) {
			size += chunk->calc_size();
		}

		return size;
	}

	uint8_t udp_packet::calc_checksum(util::crc32_t& crc) const {
		crc.init_digest();
		crc.update(last_continuous);
		crc.update(static_cast<uint32_t>(nak_type));

		if (!naks.empty())
			crc.update(&naks[0], naks.size());

		for (auto& chunk: chunks) {
			chunk->update_checksum(crc);
		}

		return static_cast<uint8_t>(crc.get_digest());
	}

	void udp_packet::serialize(std::vector<uint8_t>& data) const {
		data.clear();
		data.reserve(calc_size());

		packet_packer buf(data);
		buf.pack(last_continuous);
		buf.pack(nak_type);
		buf.pack(checksum);
		buf.pack(naks);

		for (const auto& chunk: chunks) {
			buf.pack(chunk->chunk_number);
			buf.pack(chunk->chunk_size);
			buf.pack(chunk->data);
		}
	}
}

