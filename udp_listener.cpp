#include <asio.hpp>

#include "udp_listener.hpp"
#include "udp_connection.hpp"
#include "protocol_def.hpp"
#include "socket_helper.hpp"


namespace arelion {
	udp_listener::udp_listener(uint16_t port, const std::string& ip) {
		// resets socket on any exception
		const std::string& err_msg = try_bind_socket(port, m_socket, ip);

		assert(err_msg.empty());

		m_socket->non_blocking(true);
		set_accepting_connections(true);
	}

	udp_listener::~udp_listener() {
		for (const auto& pair: m_dropped_ips) {
			printf("[%s] dropped %u packets from unknown IP %s", __func__, pair.second, (pair.first).c_str());
		}
	}


	std::string udp_listener::try_bind_socket(uint16_t port, std::shared_ptr<asio::ip::udp::socket>& skt, const std::string& ip) {
		std::string error_msg;

		try {
			asio::error_code error_code;

			skt.reset(new asio::ip::udp::socket(netservice));
			skt->open(asio::ip::udp::v6(), error_code);

			// test for IPv6 support
			const bool have_ipv6 = !error_code;

			asio::ip::udp::endpoint endpoint = resolve_addr(ip, port, &error_code);
			asio::ip::address address = endpoint.address();

			if (error_code)
				throw std::runtime_error("[udp_listener] failed to parse hostname \"" + ip + "\": " + error_code.message());

			// use the "any" address
			if (ip.empty())
				endpoint = asio::ip::udp::endpoint(address = get_any_address(have_ipv6), port);

			if (!have_ipv6 && address.is_v6())
				throw std::runtime_error("[udp_listener] IPv6 not supported, can not use address " + address.to_string());

			if (address.is_v4()) {
				if (have_ipv6)
					skt->close();

				skt->open(asio::ip::udp::v4(), error_code);

				if (error_code)
					throw std::runtime_error("[udp_listener] failed to open IPv4 socket: " + error_code.message());
			}

			// bind UDP socket to (possibly loopback) <address> on port <endpoint.port()>
			skt->bind(endpoint);
		} catch (const std::runtime_error& ex) {
			// ex includes asio::system_error and std::range_error
			skt.reset();

			if ((error_msg = ex.what()).empty())
				error_msg = "unknown problem";

			fprintf(stderr, "[udp_listener::%s] binding UDP socket to IP %s failed: %s", __func__, ip.c_str(), error_msg.c_str());
		}

		return error_msg;
	}

	void udp_listener::update() {
		netservice.poll();

		size_t bytes_available = 0;

		while ((bytes_available = m_socket->available()) > 0) {
			m_recv_buffer.clear();
			m_recv_buffer.resize(bytes_available, 0);

			asio::ip::udp::endpoint udp_endpoint;
			asio::ip::udp::socket::message_flags msgFlags = 0;
			asio::error_code error_code;

			const size_t bytes_received = m_socket->receive_from(asio::buffer(m_recv_buffer), udp_endpoint, msgFlags, error_code);

			const auto ci = m_active_conns.find(udp_endpoint);

			// known connection but expired
			if (ci != m_active_conns.end() && ci->second.expired())
				continue;

			if (check_error_code(error_code))
				break;

			if (bytes_received < udp_packet_chunk::hdr_size())
				continue;

			udp_packet pkt(&m_recv_buffer[0], bytes_received);

			if (ci != m_active_conns.end()) {
				ci->second.lock()->process_raw_packet(pkt);
				continue;
			}


			// unknown connection but still have the packet, maybe a new client wants to connect from sender's address
			if (m_accept_new_connections && pkt.last_continuous == -1 && pkt.nak_type == 0)	{
				if (!pkt.chunks.empty() && (*pkt.chunks.begin())->chunk_number == 0) {
					std::shared_ptr<udp_connection> udp_conn(new udp_connection(m_socket, udp_endpoint));
					m_waiting_conns.push(udp_conn);
					m_active_conns[udp_endpoint] = udp_conn;
					udp_conn->process_raw_packet(pkt);
				}

				continue;
			}


			const asio::ip::address& sender_addr = udp_endpoint.address();
			const std::string& sender_ip_str = sender_addr.to_string();

			if (m_dropped_ips.find(sender_ip_str) == m_dropped_ips.end()) {
				// unknown ip, drop packet
				m_dropped_ips[sender_ip_str] = 0;
			} else {
				m_dropped_ips[sender_ip_str] += 1;
			}
		}

		for (auto i = m_active_conns.cbegin(); i != m_active_conns.cend(); ) {
			if (i->second.expired()) {
				i = m_active_conns.erase(i);
				continue;
			}

			i->second.lock()->update();
			++i;
		}
	}


	std::shared_ptr<udp_connection> udp_listener::spawn_connection(const std::string& ip, uint16_t port) {
		std::shared_ptr<udp_connection> new_conn(new udp_connection(m_socket, asio::ip::udp::endpoint(wrap_ip(ip), port)));
		m_active_conns[new_conn->get_endpoint()] = new_conn;
		return new_conn;
	}

	std::shared_ptr<udp_connection> udp_listener::accept_connection() {
		std::shared_ptr<udp_connection> new_conn = m_waiting_conns.front();
		m_waiting_conns.pop();
		m_active_conns[new_conn->get_endpoint()] = new_conn;
		return new_conn;
	}


	void udp_listener::update_connections() {
		for (auto i = m_active_conns.begin(); i != m_active_conns.end(); ) {
			std::shared_ptr<udp_connection> udp_conn = i->second.lock();

			// update registry if an endpoint has changed (i.e. reconnected)
			// note that insertion does not invalidate the current iterator
			if (udp_conn != nullptr && i->first != udp_conn->get_endpoint()) {
				m_active_conns[udp_conn->get_endpoint()] = udp_conn;
				i = m_active_conns.erase(i);
				continue;
			}

			++i;
		}
	}
}

