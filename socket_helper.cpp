#include "socket_helper.hpp"

namespace arelion {
	asio::io_service netservice;

	bool check_error_code(asio::error_code& error_code) {
		// connection reset can happen if host did not start up before client wants to connect
		// try_again should only ever occur with async sockets, but testing indicates otherwise
		if (!error_code || error_code.value() == asio::error::connection_reset || error_code.value() == asio::error::try_again)
			return false;

		fprintf(stderr, "[%s] network error %i: %s", __func__, error_code.value(), error_code.message().c_str());
		return true;
	}


	asio::ip::udp::endpoint resolve_addr(const std::string& host, int32_t port, asio::error_code* error_code) {
		char buf[16];
		std::memset(buf, 0, sizeof(buf));
		std::snprintf(buf, sizeof(buf), "%d", port);
		return (resolve_addr(host, buf, error_code));
	}

	asio::ip::udp::endpoint resolve_addr(const std::string& host, const std::string& port, asio::error_code* error_code) {
		assert(error_code != nullptr);

		asio::ip::address tempAddr = wrap_ip(host, error_code);
		asio::error_code& ec = *error_code;

		if (!ec)
			return asio::ip::udp::endpoint(tempAddr, std::atoi(port.c_str()));

		// wrap_resolve() might clear the error
		asio::error_code ecc = ec;
		asio::io_service io_service;
		asio::ip::udp::resolver resolver(io_service);
		asio::ip::udp::resolver::query query(host, port);
		asio::ip::udp::resolver::iterator iter = wrap_resolve(resolver, query, error_code);
		asio::ip::udp::resolver::iterator end;

		if (!ec && iter != end)
			return *iter;

		if (!ec)
			ec = ecc;

		return asio::ip::udp::endpoint(tempAddr, 0);
	}


	asio::ip::address wrap_ip(const std::string& ip, asio::error_code* error_code) {
		asio::ip::address addr;

		if (error_code == nullptr) {
			addr = asio::ip::address::from_string(ip);
		} else {
			addr = asio::ip::address::from_string(ip, *error_code);
		}

		return addr;
	}

	asio::ip::udp::resolver::iterator wrap_resolve(asio::ip::udp::resolver& resolver, asio::ip::udp::resolver::query& query, asio::error_code* error_code) {
		asio::ip::udp::resolver::iterator resolveIt;

		if (error_code == nullptr) {
			resolveIt = resolver.resolve(query);
		} else {
			resolveIt = resolver.resolve(query, *error_code);
		}

		return resolveIt;
	}


	asio::ip::address get_any_address(const bool ip_v6) {
		if (ip_v6)
			return asio::ip::address_v6::any();

		return asio::ip::address_v4::any();
	}
}

