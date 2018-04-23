#ifndef ARELION_UDP_LISTENER_HDR
#define ARELION_UDP_LISTENER_HDR

#include <asio/ip/udp.hpp>

#include <cstdint>
#include <memory>
#include <string>

#include <map>
#include <queue>


namespace arelion {
	class udp_connection;

	// handles multiple connections on a shared UDP socket
	class udp_listener {
	public:
		// open a local socket and make it ready for listening
		udp_listener(uint16_t port, const std::string& ip = "");
		udp_listener(const udp_listener&) = delete;

		// close the socket and delete all connections
		~udp_listener();

		udp_listener& operator = (const udp_listener&) = delete;

		/**
		 * Try to bind a socket to a local address and port.
		 * If no IP or an empty one is given, this method will use
		 * the IP v6 any address "::", if IP v6 is supported.
		 * @param  port the port to bind the socket to
		 * @param  socket the socket to be bound
		 * @param  ip local IP (v4 or v6) to bind to,
		 *         the default value "" results in the v6 any address "::",
		 *         or the v4 equivalent "0.0.0.0", if v6 is no supported
		 */
		static std::string try_bind_socket(uint16_t port, std::shared_ptr<asio::ip::udp::socket>& skt, const std::string& ip = "");

		// receive data from socket and hand it to the associated udp_connection
		void update();

		void set_accepting_connections(const bool enable) { m_accept_new_connections = enable; }
		bool is_accepting_connections() const { return m_accept_new_connections; }
		bool has_incoming_connections() const { return (!m_waiting_conns.empty()); }

		// initiate a connection to ip:port
		std::shared_ptr<udp_connection> spawn_connection(const std::string& ip, uint16_t port);

		std::weak_ptr<udp_connection> preview_connection() { return (m_waiting_conns.front()); }
		std::shared_ptr<udp_connection> accept_connection();

		void reject_connection() { m_waiting_conns.pop(); }
		void update_connections();

	private:
		// do we accept packets from (and create a connection for) unknown senders?
		bool m_accept_new_connections = false;

		// socket being listened on
		std::shared_ptr<asio::ip::udp::socket> m_socket;

		std::vector<uint8_t> m_recv_buffer;

		// all connections
		std::map< asio::ip::udp::endpoint, std::weak_ptr<udp_connection> > m_active_conns;
		std::map< std::string, uint32_t> m_dropped_ips;

		std::queue< std::shared_ptr<udp_connection> > m_waiting_conns;
	};
}

#endif

