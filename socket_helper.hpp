#ifndef ARELION_SOCKET_HELPER_HDR
#define ARELION_SOCKET_HELPER_HDR

#include <asio/io_service.hpp>
#include <asio/ip/udp.hpp>


namespace arelion {
	extern asio::io_service netservice;

	bool check_error_code(asio::error_code& error_code);

	asio::ip::udp::endpoint resolve_addr(const std::string& host, int32_t port, asio::error_code* error_code);
	asio::ip::udp::endpoint resolve_addr(const std::string& host, const std::string& port, asio::error_code* error_code);

	asio::ip::address wrap_ip(const std::string& ip, asio::error_code* error_code = nullptr);
	asio::ip::udp::resolver::iterator wrap_resolve(
		asio::ip::udp::resolver& resolver,
		asio::ip::udp::resolver::query& query,
		asio::error_code* error_code = nullptr
	);

	asio::ip::address get_any_address(const bool ip_v6);
}

#endif

