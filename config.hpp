#ifndef ARELION_CONFIG_HDR
#define ARELION_CONFIG_HDR

namespace config {
	enum {
		MIN_LOSS_FACTOR = 0,
		MID_LOSS_FACTOR = 1,
		MAX_LOSS_FACTOR = 2,
	};

	static constexpr int32_t max_transmission_unit = 1400;
	static constexpr int32_t link_outgoing_bandwidth = 64 * 1024;
	static constexpr int32_t reconnect_time_secs = 15;
	static constexpr int32_t network_timeout_secs = 30;
	static constexpr int32_t initial_network_timeout_secs = 120;
	static constexpr int32_t network_loss_factor = MIN_LOSS_FACTOR;
	static constexpr int32_t udp_chunks_per_sec = 30;
};

#endif

