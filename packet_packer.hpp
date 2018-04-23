#ifndef ARELION_PACKET_PACKER_HDR
#define ARELION_PACKET_PACKER_HDR

#include "raw_packet.hpp"

#include <cassert>
#include <cstring>

#include <string>
#include <vector>

namespace arelion {
	#if 0
	class packet_packer: public raw_packet {
	public:
		packet_packer(const uint32_t length): raw_packet(length) {
			id = 0;
			pos = 0;
		}
		packet_packer(const uint32_t length, uint8_t msg_id): raw_packet(length) {
			id = msg_id;
			pos = 0;

			*this << msg_id;
		}

		template<typename T>
		packet_packer& operator << (const T& t) {
			assert((sizeof(T) + pos) <= length);

			write_bytes(&t, sizeof(T));
			return *this;
		}

		packet_packer& operator << (const std::string& str) {
			size_t size = str.size() + 1;

			// truncate if embedded 0's
			if (std::string::npos != str.find_first_of('\0'))
				size = str.find_first_of('\0') + 1;

			// truncate if too long
			if ((pos + size) > length)
				size = static_cast<size_t>(length - pos);

			write_bytes(reinterpret_cast<const uint8_t*>(str.c_str()), size);
			return *this;
		}

		template<typename E>
		packet_packer& operator << (const std::vector<E>& vec) {
			const size_t size = vec.size() * sizeof(E);

			assert(size > 0);
			assert((size + pos) <= length);

			write_bytes(vec.data(), size);
			return *this;
		}

	private:
		uint8_t* get_write_pos() { return (data + pos); }

		void write_bytes(const uint8_t* data, size_t size) {
			std::memcpy(get_write_pos(), data, size);
			pos += size;
		}

	private:
		uint8_t id = 0;
		uint32_t pos = 0;
	};

	#else

	class packet_packer {
	public:
		packet_packer(std::vector<uint8_t>& data): m_data(data) {
		}

		template<typename T>
		void pack(const T& t) {
			const size_t pos = m_data.size();
			m_data.resize(pos + sizeof(T));
			*reinterpret_cast<T*>(&m_data[pos]) = t;
		}

		void pack(const std::vector<uint8_t>& v) {
			std::copy(v.begin(), v.end(), std::back_inserter(m_data));
		}

	private:
		std::vector<uint8_t>& m_data;
	};
	#endif
}

#endif

