#ifndef ARELION_UDP_CONNECTION_HDR
#define ARELION_UDP_CONNECTION_HDR

#include <asio/ip/udp.hpp>

#include <chrono>
#include <memory>

#include <deque>
#include <list>
#include <map>
#include <vector>

#include "base_connection.hpp"
#include "bandwidth_tracker.hpp"
#include "config.hpp"
#include "udp_packet.hpp"
#include "util.hpp"


namespace arelion {
	class udp_connection: public base_connection {
	public:
		udp_connection(std::shared_ptr<asio::ip::udp::socket> udp_socket, const asio::ip::udp::endpoint& net_address);
		udp_connection(int32_t src_port, const uint32_t dst_port, const std::string& address_str);
		udp_connection(base_connection& conn);
		~udp_connection();


		void send_data(std::shared_ptr<const raw_packet> data) override;

		std::shared_ptr<const raw_packet> peek(uint32_t index) const override;
		std::shared_ptr<const raw_packet> get_data() override;

		void update() override;
		void delete_buffer_packet_at(uint32_t index) override;

		void flush(const bool forced) override;
		void reconnect_to(base_connection& conn) override;

		bool has_incoming_data() const override { return (!m_msg_queue.empty()); }
		bool check_timeout(int32_t seconds = 0, bool initial = false) const override;
		bool can_reconnect() const override { return (m_reconnect_time_secs > 0); }
		bool needs_reconnect() override;

		uint32_t get_packet_queue_size() const override { return (m_msg_queue.size()); }

		std::string get_statistics() const override;
		std::string get_full_address() const override;


		// strips and parses udp header, then adds raw data to m_waiting_packets
		// udp_connection takes ownership of the packet and will delete it later
		void process_raw_packet(udp_packet& packet);

		// connections are silent by default, unmuting allows them to send data
		void unmute() override { m_muted = false; }
		void close(bool flush) override;
		void set_loss_factor(int32_t factor) override {
			m_netloss_factor = util::clamp(factor, int32_t(config::MIN_LOSS_FACTOR), int32_t(config::MAX_LOSS_FACTOR));
		}

		const asio::ip::udp::endpoint& get_endpoint() const { return m_net_address; }

		bool is_using_address(const asio::ip::udp::endpoint& from) const { return (m_net_address == from); }
		bool use_min_loss_factor() const { return (m_netloss_factor == config::MIN_LOSS_FACTOR); }

	private:
		void init(bool shared_socket);

		void init_connection(asio::ip::udp::endpoint address, std::shared_ptr<asio::ip::udp::socket> socket);
		void copy_connection(udp_connection& conn);

		void set_max_transmission_unit(uint32_t max_transmission_unit) {
			m_max_transmission_unit = util::clamp(max_transmission_unit, 300u, udp_packet::max_size());
		}

		// add header to data and send it
		void create_chunk(const uint8_t* data, const uint32_t length, const int32_t chunk_num);
		void send_if_necessary(bool flushed);
		void ack_chunks(int32_t lastAck);

		void request_resend(std::shared_ptr<udp_packet_chunk> ptr);
		void send_packet(udp_packet& pkt);


		#ifdef NETWORK_TEST
		#define PACKET_LOSS_MAX_COUNT     10          // maximum number of packets to drop
		#define PACKET_LOSS_ALLOW_PROB     0.5f       // in [0, 1)
		#define PACKET_LOSS_RESET_PROB     0.01f      // in [0, 1)
		#define PACKET_CORRUPTION_PROB     0.0f       // in [0, 1)
		#define PACKET_MIN_LATENCY       750          // in [milliseconds] minimum latency
		#define PACKET_MAX_LATENCY      1250          // in [milliseconds] maximum latency

		bool emulate_packet_loss(int32_t& loss_ctr) {
			if (m_rng.next() < PACKET_LOSS_ALLOW_PROB)
				return true;

			// fake packet loss with a percentage probability
			if ((loss_ctr == 0) && (m_rng.next() < PACKET_LOSS_RESET_PROB))
				loss_ctr = PACKET_LOSS_MAX_COUNT * m_rng.next();

			return (loss_ctr > 0 && (--loss_ctr) > 0);
		}

		void emulate_packet_corruption(uint8_t& pkt_crc) {
			if (m_rng.next() >= PACKET_CORRUPTION_PROB)
				return;

			pkt_crc = uint8_t(m_rng.next());
		}

		bool emulate_latency(const std::vector<uint8_t>& data, const asio::ip::udp::socket::message_flags& msg_flags, asio::error_code& error_code, bool cond) {
			#if (PACKET_MAX_LATENCY > 0)
			const net_time_point   cur_time{std::chrono::high_resolution_clock::now()};
			const net_time_range delay_time{int64_t(1000ll * 1000ll * (PACKET_MIN_LATENCY + (PACKET_MAX_LATENCY - PACKET_MIN_LATENCY) * m_rng.next()))};

			for (auto di = m_delayed_packets.begin(); di != m_delayed_packets.end(); ) {
				if ((cur_time - di->first).count() > 0) {
					m_socket->send_to(asio::buffer(di->second), m_net_address, msg_flags, error_code);
					di = m_delayed_packets.erase(di);
					continue;
				}

				++di;
			}

			if (cond)
				m_delayed_packets[cur_time + delay_time] = data;

			#endif
			return cond;
		}

		#else

		bool emulate_packet_loss(int32_t& /*loss_ctr*/) const { return false; }
		void emulate_packet_corruption(uint8_t& /*pkt_crc*/) const {}
		bool emulate_latency(const std::vector<uint8_t>&, const asio::ip::udp::socket::message_flags&, asio::error_code&, bool cond) { return cond; }
		#endif

	private:
		// outgoing data (without header) waiting to be sent
		std::list< std::shared_ptr<const raw_packet> > m_outgoing_data;
		// packets we have received but not yet read
		std::map<int, raw_packet*> m_waiting_packets;

		// newly created and not yet sent
		std::deque< std::shared_ptr<udp_packet_chunk> > m_new_chunks;
		// packets the other side did not ack until now
		std::deque< std::shared_ptr<udp_packet_chunk> > m_unacked_chunks;

		// packets the other side missed
		std::map<int32_t, std::shared_ptr<udp_packet_chunk> > m_resend_req_pkts;

		// complete packets we received but did not yet consume
		std::deque< std::shared_ptr<const raw_packet> > m_msg_queue;

		std::vector<uint8_t> m_send_buffer;
		std::vector<uint8_t> m_recv_buffer;
		std::vector<uint8_t> m_wait_buffer;

		std::vector<int> m_dropped_packets;

		#ifdef	NETWORK_TEST
		std::map< net_time_point, std::vector<uint8_t> > m_delayed_packets;
		#endif

		util::crc32_t m_crc;
		util::rng11f_t m_rng;


		std::shared_ptr<asio::ip::udp::socket> m_socket;

		// address of the other end
		asio::ip::udp::endpoint m_net_address;


		raw_packet m_fragment_buffer;
		bandwidth_tracker m_outgoing_bw_tracker;


		net_time_point m_prv_chunk_created_time;
		net_time_point m_prv_packet_send_time;
		net_time_point m_prv_packet_recv_time;

		net_time_point m_prv_unack_resend_time;
		net_time_point m_prv_nak_time;

		net_time_point m_prv_update_time;


		// maximum size of packets to send
		uint32_t m_max_transmission_unit = 0;

		int32_t m_reconnect_time_secs = 0;
		int32_t m_netloss_factor = 0;

		int32_t m_loss_counter = 0;

		int32_t m_last_inorder = 0;
		int32_t m_last_mid_chunk = 0;

		uint32_t m_packet_chunk_num = 0;

		uint32_t m_resent_chunks = 0;
		uint32_t m_dropped_chunks = 0;

		uint32_t m_sent_packets = 0;
		uint32_t m_recv_packets = 0;

		uint32_t m_sent_overhead = 0;
		uint32_t m_recv_overhead = 0;

		bool m_muted = false;
		bool m_closed = false;
		bool m_resend = false;
		bool m_shared_socket = true;
		bool m_log_messages = false;
	};
}

#endif

