#ifndef ARELION_PACKET_UNPACKER_HDR
#define ARELION_PACKET_UNPACKER_HDR

#include "raw_packet.hpp"

#include <cassert>
#include <cstring>

#include <string>
#include <vector>
#include <stdexcept>
#include <memory>

namespace arelion {
	#if 0
	class packet_unpacker {
	public:
		packet_unpacker(std::shared_ptr<const raw_packet> packet, size_t skip_bytes = 0): pkt(packet), pos(skip_bytes) {
			assert(pos <= pkt->length);
		}

		template<typename T>
		void operator >> (T& t) {
			assert((pos + sizeof(T)) <= pkt->length);
			read_bytes(reinterpret_cast<uint8_t*>(&t), sizeof(T));
		}

		template<typename E>
		void operator >> (std::vector<E>& vec) {
			assert((vec.size() * sizeof(E)) <= (pkt->length - pos));
			read_bytes(vec.data(), vec.size() * sizeof(E));
		}

		void operator >> (std::string& str) {
			size_t i = pos;

			for (; i < pkt->length && pkt->data[i] != '\0'; ++i) {
			}

			assert(i < pkt->length);

			str = std::string(reinterpret_cast<char*>(pkt->data + pos));
			pos += (str.size() + 1);
		}

	private:
		void read_bytes(uint8_t* data, size_t size) {
			std::memcpy(data, pkt->data + pos, size);
			pos += size;
		}

	private:
		std::shared_ptr<const raw_packet> pkt;
		size_t pos = 0;
	};

	#else

	class packet_unpacker {
	public:
		packet_unpacker(const uint8_t* data, uint32_t len): m_data(data), m_pos(0), m_len(len) {
		}

		template<typename T>
		void unpack(T& t) {
			assert(m_len >= (m_pos + sizeof(T)));
			t = *reinterpret_cast<const T*>(m_data + m_pos);
			m_pos += sizeof(T);
		}

		void unpack(std::vector<uint8_t>& v, uint8_t unpack_len) {
			std::copy(m_data + m_pos, m_data + m_pos + unpack_len, std::back_inserter(v));
			m_pos += unpack_len;
		}

		uint32_t bytes_remaining() const {
			return (m_len - std::min(m_pos, m_len));
		}

	private:
		const uint8_t* m_data;

		uint32_t m_pos;
		uint32_t m_len;
	};
	#endif
}

#endif

