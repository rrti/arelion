#include "util.hpp"

namespace util {
	struct static_initializer_t {
		static_initializer_t() { crc32_t::init_static(); }
		~static_initializer_t() {}
	};

	static static_initializer_t init; 
}

