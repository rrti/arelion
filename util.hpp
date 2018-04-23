#ifndef ARELION_UTIL_HDR
#define ARELION_UTIL_HDR

#include <cstdint>
#include <random>

extern "C" {
	#include <7zCrc.h>
}

namespace util {
	template<typename T> T clamp(T v, T vmin, T vmax) {
		return (std::max(vmin, std::min(v, vmax)));
	}


	struct crc32_t {
	public:
		static void init_static() { CrcGenerateTable(); }
		static uint32_t calc_static(const void* data, size_t size) { return (CrcUpdate(0, data, size)); }

		void init_digest() { m_crc = CRC_INIT_VAL; }
		uint32_t get_digest() const { return (CRC_GET_DIGEST(m_crc)); }

		crc32_t& update(const void* data, size_t size) { m_crc = CrcUpdate(m_crc, data, size); return *this; }

		template<typename T> crc32_t& update(T data) { return (update(&data, sizeof(data))); }
		template<typename T> crc32_t& operator << (T data) { return (update(&data, sizeof(data))); }

	private:
		uint32_t m_crc = CRC_INIT_VAL;
	};


	template<typename T> struct rng11_t {
	public:
		rng11_t(uint64_t seed = 0) { m_sampler.seed(seed); }

		T next() { return ((*this)()); }
		T operator () () { return (m_distrib(m_sampler)); }

	private:
		std::uniform_real_distribution<T> m_distrib;
		std::default_random_engine m_sampler;
	};

	typedef rng11_t<float> rng11f_t;
};

#endif

