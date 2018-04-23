#ifndef ARELION_LOCAL_CONNECTION_HDR
#define ARELION_LOCAL_CONNECTION_HDR

#include <deque>
#include <mutex>

#include "base_connection.hpp"

namespace arelion {
	// direct local connection between server and client buffers
	// server and client have to run in the same process instance
	class local_connection: public base_connection {
	public:
		local_connection();
		~local_connection() { num_instances -= 1; }


		void send_data(std::shared_ptr<const raw_packet> packet) override;

		std::shared_ptr<const raw_packet> peek(uint32_t index) const override;
		std::shared_ptr<const raw_packet> get_data() override;

		void delete_buffer_packet_at(uint32_t index) override;
		void reconnect_to(base_connection& /*conn*/) override {}
		void flush(const bool /*forced*/) override {}

		bool has_incoming_data() const override;
		bool check_timeout(int32_t /*seconds*/, bool /*initial*/) const override { return false; }
		bool can_reconnect() const override { return false; }
		bool needs_reconnect() override { return false; }

		void unmute() override {}
		void close(bool flush) override;
		void set_loss_factor(int32_t /*factor*/) override {}

		uint32_t get_packet_queue_size() const override;

		std::string get_statistics() const override;
		std::string get_full_address() const override { return "Localhost"; }

	private:
		static constexpr uint32_t MAX_INSTANCES = 2;

		static std::deque< std::shared_ptr<const raw_packet> > pkt_queues[MAX_INSTANCES];
		static std::mutex mutexes[MAX_INSTANCES];

		uint32_t remote_instance_idx() const { return ((m_instance_num + 1) % MAX_INSTANCES); }

		/// there can be two local connection instances; first
		// represents server->client and second client->server
		static uint32_t num_instances;
		/// which instance we are
		uint32_t m_instance_num = 0;
	};
}

#endif

