#include <cassert>

#include "local_connection.hpp"
#include "protocol_def.hpp"

namespace arelion {
	uint32_t local_connection::num_instances = 0;

	std::deque< std::shared_ptr<const raw_packet> > local_connection::pkt_queues[MAX_INSTANCES];
	std::mutex local_connection::mutexes[MAX_INSTANCES];


	local_connection::local_connection() {
		assert(num_instances < MAX_INSTANCES);

		// clear data that might have been left over
		pkt_queues[m_instance_num = num_instances++].clear();
	}


	void local_connection::close(bool flush) {
		if (!flush)
			return;

		std::lock_guard<std::mutex> scoped_lock(mutexes[m_instance_num]);
		pkt_queues[m_instance_num].clear();
	}

	void local_connection::send_data(std::shared_ptr<const raw_packet> packet) {
		assert(proto_def.is_valid_packet(packet->data, packet->length));

		m_data_sent += packet->length;

		// when sending from A to B we must lock B's queue
		std::lock_guard<std::mutex> scoped_lock(mutexes[remote_instance_idx()]);
		pkt_queues[remote_instance_idx()].push_back(packet);
	}

	std::shared_ptr<const raw_packet> local_connection::get_data() {
		std::lock_guard<std::mutex> scoped_lock(mutexes[m_instance_num]);
		std::deque< std::shared_ptr<const raw_packet> >& pkt_queue = pkt_queues[m_instance_num];

		if (pkt_queue.empty())
			return {};

		std::shared_ptr<const raw_packet> pkt = pkt_queue.front();
		pkt_queue.pop_front();
		m_data_recv += pkt->length;
		return pkt;
	}

	std::shared_ptr<const raw_packet> local_connection::peek(uint32_t index) const {
		std::lock_guard<std::mutex> scoped_lock(mutexes[m_instance_num]);
		std::deque< std::shared_ptr<const raw_packet> >& pkt_queue = pkt_queues[m_instance_num];

		if (index >= pkt_queue.size())
			return {};

		return pkt_queue[index];
	}

	void local_connection::delete_buffer_packet_at(uint32_t index) {
		std::lock_guard<std::mutex> scoped_lock(mutexes[m_instance_num]);
		std::deque< std::shared_ptr<const raw_packet> >& pkt_queue = pkt_queues[m_instance_num];

		if (index >= pkt_queue.size())
			return;

		pkt_queue.erase(pkt_queue.begin() + index);
	}


	std::string local_connection::get_statistics() const {
		char buf[512] = {0};
		char* ptr = &buf[0];

		ptr += snprintf(ptr, sizeof(buf) - (ptr - buf), "[local_connection::%s]\n", __func__);
		ptr += snprintf(ptr, sizeof(buf) - (ptr - buf), "\t%u bytes sent  \n", m_data_sent);
		ptr += snprintf(ptr, sizeof(buf) - (ptr - buf), "\t%u bytes recv'd\n", m_data_recv);

		return buf;
	}


	bool local_connection::has_incoming_data() const {
		std::lock_guard<std::mutex> scoped_lock(mutexes[m_instance_num]);
		return (!pkt_queues[m_instance_num].empty());
	}

	uint32_t local_connection::get_packet_queue_size() const {
		std::lock_guard<std::mutex> scoped_lock(mutexes[m_instance_num]);
		return (!pkt_queues[m_instance_num].size());
	}
}

