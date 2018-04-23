#ifndef ARELION_LOOPBACK_CONNECTION_HDR
#define ARELION_LOOPBACK_CONNECTION_HDR

#include <deque>

#include "base_connection.hpp"

namespace arelion {
	// dummy queue-like connection, bounces everything back to the sender
	class loopback_connection: public base_connection {
	public:
		void send_data(std::shared_ptr<const raw_packet> data) override;

		std::shared_ptr<const raw_packet> peek(uint32_t index) const override;
		std::shared_ptr<const raw_packet> get_data() override;

		void delete_buffer_packet_at(uint32_t index) override;
		void flush(const bool /*forced*/) override {}
		void reconnect_to(base_connection& /*conn*/) override {}

		bool has_incoming_data() const override { return (!m_pkt_queue.empty()); }
		bool check_timeout(int32_t /*seconds*/, bool /*initial*/) const override { return false; }
		bool can_reconnect() const override { return false; }
		bool needs_reconnect() override { return false; }

		void unmute() override {}
		void close(bool /*flush*/) override {}
		void set_loss_factor(int32_t /*factor*/) override {}

		std::string get_statistics() const override { return "N/A"; }
		std::string get_full_address() const override { return "Loopback"; }

	private:
		std::deque< std::shared_ptr<const raw_packet> > m_pkt_queue;
	};
}

#endif

