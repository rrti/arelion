#ifndef ARELION_BASE_CONNECTION_HDR
#define ARELION_BASE_CONNECTION_HDR

#include <chrono>
#include <string>
#include <memory>

#include "raw_packet.hpp"

namespace arelion {
	typedef std::chrono::high_resolution_clock::time_point net_time_point;
	typedef std::chrono::nanoseconds net_time_range;

	class base_connection {
	public:
		base_connection();
		virtual ~base_connection() {}

		// send packet to remote instance
		virtual void send_data(std::shared_ptr<const raw_packet> data) = 0;

		virtual std::shared_ptr<const raw_packet> peek(uint32_t index) const = 0;
		virtual std::shared_ptr<const raw_packet> get_data() = 0;

		// check for unacked packets, timeout, etc
		virtual void update() {}
		// delete a packet from the buffer
		virtual void delete_buffer_packet_at(uint32_t index) = 0;

		// flush the underlying buffer (if any) to the network
		virtual void flush(const bool forced = false) = 0;
		virtual void reconnect_to(base_connection& conn) = 0;

		virtual bool has_incoming_data() const = 0;
		virtual bool check_timeout(int32_t seconds = 0, bool initial = false) const = 0;
		virtual bool can_reconnect() const = 0;
		virtual bool needs_reconnect() = 0;

		virtual uint32_t get_packet_queue_size() const { return 0; }

		virtual void unmute() = 0;
		virtual void close(bool flush = false) = 0;
		virtual void set_loss_factor(int32_t factor) = 0;

		virtual std::string get_statistics() const = 0;
		virtual std::string get_full_address() const = 0;

	protected:
		uint32_t m_data_sent = 0;
		uint32_t m_data_recv = 0;
	};
}

#endif

