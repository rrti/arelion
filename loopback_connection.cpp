#include "loopback_connection.hpp"

namespace arelion {
	void loopback_connection::send_data(std::shared_ptr<const raw_packet> data) {
		m_pkt_queue.push_back(data);
	}

	std::shared_ptr<const raw_packet> loopback_connection::peek(uint32_t index) const {
		if (index >= m_pkt_queue.size())
			return {};

		return m_pkt_queue[index];
	}

	void loopback_connection::delete_buffer_packet_at(uint32_t index) {
		if (index >= m_pkt_queue.size())
			return;

		m_pkt_queue.erase(m_pkt_queue.begin() + index);
	}

	std::shared_ptr<const raw_packet> loopback_connection::get_data() {
		if (m_pkt_queue.empty())
			return {};

		std::shared_ptr<const raw_packet> pkt = m_pkt_queue.front();
		m_pkt_queue.pop_front();
		return pkt;
	}
}

