#include <memory>
#include <cinttypes>

#include "udp_connection.hpp"
#include "udp_packet.hpp"
#include "protocol_def.hpp"
#include "socket_helper.hpp"

namespace arelion {
	udp_connection::udp_connection(std::shared_ptr<asio::ip::udp::socket> udp_socket, const asio::ip::udp::endpoint& net_address)
		: m_socket(udp_socket)
		, m_net_address(net_address)
	{
		init(true);
	}

	udp_connection::udp_connection(int32_t src_port, const uint32_t dst_port, const std::string& address_str) {
		asio::error_code error_code;
		m_net_address = resolve_addr(address_str, dst_port, &error_code);

		const asio::ip::address source_addr = get_any_address(m_net_address.address().is_v6());
		const asio::ip::udp::endpoint source_endpoint = asio::ip::udp::endpoint(source_addr, src_port);

		m_socket.reset(new asio::ip::udp::socket(arelion::netservice, source_endpoint));

		init(false);
	}

	udp_connection::udp_connection(base_connection& conn) {
		reconnect_to(conn);
		init(true);
	}


	void udp_connection::init(bool shared_socket) {
		m_prv_nak_time = std::chrono::high_resolution_clock::now();
		m_prv_unack_resend_time = std::chrono::high_resolution_clock::now();
		m_prv_packet_send_time = std::chrono::high_resolution_clock::now();
		m_prv_packet_recv_time = std::chrono::high_resolution_clock::now();
		m_prv_chunk_created_time = std::chrono::high_resolution_clock::now();
		m_prv_update_time = std::chrono::high_resolution_clock::now();


		m_max_transmission_unit = config::max_transmission_unit;
		m_reconnect_time_secs = config::reconnect_time_secs;
		m_netloss_factor = config::network_loss_factor;

		m_loss_counter = 0;

		m_last_inorder = -1;
		m_last_mid_chunk = -1;

		m_packet_chunk_num = 0;

		m_resent_chunks = 0;
		m_dropped_chunks = 0;

		m_sent_packets = 0;
		m_recv_packets = 0;

		m_sent_overhead = 0;
		m_recv_overhead = 0;


		m_muted = true;
		m_closed = false;
		m_resend = false;
		m_shared_socket = shared_socket;
	}


	void udp_connection::reconnect_to(base_connection& conn) {
		dynamic_cast<udp_connection&>(conn).copy_connection(*this);
	}

	void udp_connection::copy_connection(udp_connection& conn) {
		conn.init_connection(m_net_address, m_socket);
	}

	void udp_connection::init_connection(asio::ip::udp::endpoint address, std::shared_ptr<asio::ip::udp::socket> socket) {
		m_net_address = address;
		m_socket = socket;
	}

	udp_connection::~udp_connection() {
		m_fragment_buffer.delete_data();

		for (auto& it: m_waiting_packets)
			delete it.second;

		flush(true);
	}

	void udp_connection::send_data(std::shared_ptr<const raw_packet> data) {
		assert(data->length > 0);
		m_outgoing_data.push_back(data);
	}

	std::shared_ptr<const raw_packet> udp_connection::peek(uint32_t index) const {
		if (index >= m_msg_queue.size())
			return {};

		return m_msg_queue[index];
	}

	std::shared_ptr<const raw_packet> udp_connection::get_data() {
		if (m_msg_queue.empty())
			return {};

		std::shared_ptr<const raw_packet> msg = m_msg_queue.front();
		m_msg_queue.pop_front();
		return msg;
	}


	void udp_connection::delete_buffer_packet_at(uint32_t index) {
		if (index >= m_msg_queue.size())
			return;

		m_msg_queue.erase(m_msg_queue.begin() + index);
	}

	void udp_connection::update() {
		const net_time_point cur_update_time{std::chrono::high_resolution_clock::now()};
		const net_time_range dif_update_time{cur_update_time - m_prv_update_time}; // ns
		const net_time_range   max_poll_time{10ll * 1000ll * 1000ll}; // 10ms

		// convert dt to ms
		m_outgoing_bw_tracker.update_time(dif_update_time.count() / (1000ll * 1000ll));

		if (!m_shared_socket && !m_closed) {
			// NB: duplicated in udp_listener
			netservice.poll();

			size_t bytes_available = 0;

			while ((bytes_available = m_socket->available()) > 0) {
				m_recv_buffer.clear();
				m_recv_buffer.resize(bytes_available, 0);

				asio::ip::udp::endpoint udp_endpoint;
				asio::ip::udp::socket::message_flags msg_flags = 0;
				asio::error_code error_code;

				const size_t bytes_received = m_socket->receive_from(asio::buffer(m_recv_buffer), udp_endpoint, msg_flags, error_code);

				if (check_error_code(error_code))
					break;

				if (bytes_received < udp_packet::hdr_size())
					continue;

				udp_packet data(&m_recv_buffer[0], bytes_received);

				if (is_using_address(udp_endpoint))
					process_raw_packet(data);

				// make sure we do not get stuck here
				if ((std::chrono::high_resolution_clock::now() - cur_update_time).count() > max_poll_time.count())
					break;
			}
		}

		m_prv_update_time = cur_update_time;

		flush(false);
	}

	void udp_connection::process_raw_packet(udp_packet& pkt) {
		m_prv_packet_recv_time = std::chrono::high_resolution_clock::now();
		m_data_recv += pkt.calc_size();
		m_recv_overhead += udp_packet::hdr_size();
		m_recv_packets += 1;

		if (emulate_packet_loss(m_loss_counter))
			return;

		if (pkt.calc_checksum(m_crc) != pkt.checksum) {
			fprintf(stderr, "[%s] discarding incoming corrupted packet: CRC %d, LEN %d", __func__, pkt.checksum, pkt.calc_size());
			return;
		}

		if (pkt.last_continuous < 0 && m_last_inorder >= 0 && (m_unacked_chunks.empty() || m_unacked_chunks[0]->chunk_number > 0)) {
			fprintf(stderr, "[%s] discarding superfluous reconnection attempt", __func__);
			return;
		}

		ack_chunks(pkt.last_continuous);

		if (!m_unacked_chunks.empty()) {
			const int32_t next_cont = pkt.last_continuous + 1;
			const int32_t unack_dif = m_unacked_chunks[0]->chunk_number - next_cont;

			if (-256 <= unack_dif && unack_dif <= 256) {
				if (pkt.nak_type < 0) {
					for (int32_t i = 0; i != -pkt.nak_type; ++i) {
						const int32_t unack_pos = i + unack_dif;

						if (size_t(unack_pos) < m_unacked_chunks.size()) {
							assert(m_unacked_chunks[unack_pos]->chunk_number == next_cont + i);
							request_resend(m_unacked_chunks[unack_pos]);
						}
					}
				} else if (pkt.nak_type > 0) {
					int32_t unack_pos = 0;

					for (size_t i = 0; i != pkt.naks.size(); ++i) {
						if (unack_dif + pkt.naks[i] < 0)
							continue;

						while (unack_pos < (unack_dif + pkt.naks[i])) {
							// if there are gaps in the array, assume further resends are not needed
							if (size_t(unack_pos) < m_unacked_chunks.size())
								m_resend_req_pkts.erase(m_unacked_chunks[unack_pos]->chunk_number);

							unack_pos += 1;
						}

						if (size_t(unack_pos) < m_unacked_chunks.size()) {
							assert(m_unacked_chunks[unack_pos]->chunk_number == (next_cont + pkt.naks[i]));
							request_resend(m_unacked_chunks[unack_pos]);
						}

						unack_pos += 1;
					}
				}
			}
		}

		for (auto ci = pkt.chunks.begin(); ci != pkt.chunks.end(); ++ci) {
			const std::shared_ptr<arelion::udp_packet_chunk>& chunk = *ci;

			if ((m_last_inorder >= chunk->chunk_number) || (m_waiting_packets.find(chunk->chunk_number) != m_waiting_packets.end())) {
				m_dropped_chunks += 1;
				continue;
			}

			m_waiting_packets.emplace(chunk->chunk_number, new raw_packet(&chunk->data[0], chunk->data.size()));
		}


		// process all in-order packets that we have waiting
		for (auto wpi = m_waiting_packets.find(m_last_inorder + 1); wpi != m_waiting_packets.end(); wpi = m_waiting_packets.find(m_last_inorder + 1)) {
			m_wait_buffer.clear();

			if (m_fragment_buffer.data != nullptr) {
				// combine fragment with wait-buffer (packet reassembly)
				m_wait_buffer.resize(m_fragment_buffer.length);
				m_wait_buffer.assign(m_fragment_buffer.data, m_fragment_buffer.data + m_fragment_buffer.length);

				m_fragment_buffer.delete_data();
			}

			m_last_inorder += 1;

			std::copy(wpi->second->data, wpi->second->data + wpi->second->length, std::back_inserter(m_wait_buffer));
			m_waiting_packets.erase(wpi);

			for (uint32_t pos = 0; pos < m_wait_buffer.size(); ) {
				const uint8_t* bufp = &m_wait_buffer[pos];

				const uint32_t msg_length = m_wait_buffer.size() - pos;
				const int32_t pkt_length = proto_def.packet_length(bufp, msg_length);

				// this returns false for zero or invalid pkt_length
				if (proto_def.is_valid_length(pkt_length, msg_length)) {
					m_msg_queue.push_back(std::shared_ptr<const raw_packet>(new raw_packet(bufp, pkt_length)));

					pos += pkt_length;
				} else {
					if (pkt_length >= 0) {
						// partial packet in buffer
						m_fragment_buffer = std::move(raw_packet(bufp, msg_length));
						break;
					}

					fprintf(stderr, "[%s] discarding incoming invalid packet: id %d, len %d", __func__, int32_t(*bufp), pkt_length);

					// skip a single byte until we encounter a valid packet
					pos += 1;
				}
			}
		}
	}

	void udp_connection::flush(const bool forced) {
		if (m_muted)
			return;

		const net_time_point cur_flush_time{std::chrono::high_resolution_clock::now()};
		const net_time_range dif_flush_time{cur_flush_time - m_prv_chunk_created_time}; // ns
		const net_time_range max_chunk_time{(1000ll * 1000ll * 1000ll) / config::udp_chunks_per_sec}; // ns per chunk
		const net_time_range nlf_limit_time{(200 >> m_netloss_factor) * 1000ll * 1000ll}; // (200 >> nlf) ms

		// do not create chunks faster than <udp_chunks_per_sec> Hz
		const bool wait_more = (m_prv_chunk_created_time >= (cur_flush_time - max_chunk_time));

		// subtract dt to reduce send frequency of tiny packets (which barely contribute to outgoing_length)
		const int32_t required_length = (nlf_limit_time - dif_flush_time).count() / (10ll * 1000ll * 1000ll);

		int32_t outgoing_length = 0;

		if (!wait_more) {
			for (auto pi = m_outgoing_data.begin(); (pi != m_outgoing_data.end()) && (outgoing_length <= required_length); ++pi) {
				outgoing_length += (*pi)->length;
			}
		}

		if (forced || (!wait_more && outgoing_length > required_length)) {
			uint8_t buffer[udp_packet::max_size()];
			uint32_t pos = 0;

			// manually fragment packets to respect configured MTU
			bool partial_packet = false;
			bool send_more_data = true;

			do {
				send_more_data  = (m_outgoing_bw_tracker.get_average(true) <= config::link_outgoing_bandwidth);
				send_more_data |= ((config::link_outgoing_bandwidth <= 0) || partial_packet || forced);

				if (!m_outgoing_data.empty() && send_more_data) {
					std::shared_ptr<const raw_packet>& raw_pkt = *(m_outgoing_data.begin());

					if (!partial_packet && !proto_def.is_valid_packet(raw_pkt->data, raw_pkt->length)) {
						// discard invalid outgoing raw packet
						m_outgoing_data.pop_front();
					} else {
						const uint32_t num_chunk_bytes = std::min(udp_packet_chunk::max_size() - pos, raw_pkt->length);

						assert(raw_pkt->length > 0);
						memcpy(buffer + pos, raw_pkt->data, num_chunk_bytes);

						pos += num_chunk_bytes;
						m_recv_overhead += udp_packet::hdr_size();

						m_outgoing_bw_tracker.data_sent(num_chunk_bytes, true);

						if ((partial_packet = (num_chunk_bytes != raw_pkt->length))) {
							// partially transfered
							raw_pkt.reset(new raw_packet(raw_pkt->data + num_chunk_bytes, raw_pkt->length - num_chunk_bytes));
						} else {
							// full packet copied
							m_outgoing_data.pop_front();
						}
					}
				}

				if ((pos > 0) && (m_outgoing_data.empty() || (pos == udp_packet_chunk::max_size()) || !send_more_data)) {
					create_chunk(buffer, pos, m_packet_chunk_num++);
					pos = 0;
				}
			} while (!m_outgoing_data.empty() && send_more_data);
		}

		send_if_necessary(forced);
	}

	bool udp_connection::check_timeout(int32_t seconds, bool initial) const {
		int32_t timeout_secs = 0;

		switch (util::clamp(seconds, -1, 1)) {
			case  0: {
				timeout_secs = (m_data_recv > 0 && !initial)? config::network_timeout_secs: config::initial_network_timeout_secs;
			} break;
			case  1: {
				timeout_secs = seconds;
			} break;
			case -1: {
				timeout_secs = m_reconnect_time_secs;
			} break;
			default: {
			} break;
		}

		const net_time_range cur_timeout_dt{std::chrono::high_resolution_clock::now() - m_prv_packet_recv_time};
		const net_time_range max_timeout_dt{timeout_secs * 1000ll * 1000ll * 1000ll};

		return (timeout_secs > 0 && cur_timeout_dt.count() > max_timeout_dt.count());
	}

	bool udp_connection::needs_reconnect() {
		if (!can_reconnect())
			return false;

		if (!check_timeout(-1)) {
			m_reconnect_time_secs = config::reconnect_time_secs;
			return false;
		}
		if (check_timeout(m_reconnect_time_secs)) {
			m_reconnect_time_secs += 1;
			return true;
		}

		return false;
	}


	std::string udp_connection::get_statistics() const {
		char buf[512] = {0};
		char* ptr = &buf[0];
		const char* fmts[] = {
			"\t%u bytes sent   in %u packets (%.3f bytes/packet)\n",
			"\t%u bytes recv'd in %u packets (%.3f bytes/packet)\n",
			"\t{%.3fx, %.3fx} relative protocol overhead {up, down}\n",
			"\t%u incoming chunks dropped, %u outgoing chunks resent\n",
		};

		ptr += snprintf(ptr, sizeof(buf) - (ptr - buf), "[udp_connection::%s]\n", __func__);
		ptr += snprintf(ptr, sizeof(buf) - (ptr - buf), fmts[0], m_data_sent, m_sent_packets, m_data_sent * 1.0f / m_sent_packets);
		ptr += snprintf(ptr, sizeof(buf) - (ptr - buf), fmts[1], m_data_recv, m_recv_packets, m_data_recv * 1.0f / m_recv_packets);
		ptr += snprintf(ptr, sizeof(buf) - (ptr - buf), fmts[2], m_sent_overhead * 1.0f / m_data_sent, m_recv_overhead * 1.0f / m_data_recv);
		ptr += snprintf(ptr, sizeof(buf) - (ptr - buf), fmts[3], m_dropped_chunks, m_resent_chunks);

		return buf;
	}

	std::string udp_connection::get_full_address() const {
		const asio::ip::address& ip = m_net_address.address();
		const std::string& ip_str = ip.to_string();

		char buf[512];
		std::memset(buf, 0, sizeof(buf));
		std::snprintf(buf, sizeof(buf), "[%s]:%u", ip_str.c_str(), m_net_address.port());
		return buf;
	}


	void udp_connection::create_chunk(const uint8_t* data, const uint32_t length, const int32_t chunk_num) {
		assert((length > 0) && (length < 255));

		std::shared_ptr<udp_packet_chunk> chunk(new udp_packet_chunk());

		chunk->chunk_number = chunk_num;
		chunk->chunk_size = length;

		chunk->data.resize(length);
		chunk->data.assign(data, data + length);

		m_new_chunks.push_back(chunk);

		m_prv_chunk_created_time = std::chrono::high_resolution_clock::now();
	}

	void udp_connection::send_if_necessary(bool flushed) {
		const net_time_point curr_send_time{std::chrono::high_resolution_clock::now()};
		const net_time_range diff_send_time{curr_send_time - m_prv_packet_send_time};
		const net_time_range max_unack_time{(400 >> m_netloss_factor) * 1000llu * 1000llu}; // (400 >> nlf) ms

		const net_time_range chunk_delta_time{curr_send_time - m_prv_chunk_created_time};
		const net_time_range unack_delta_time{curr_send_time - m_prv_unack_resend_time};

		int8_t nak_count = 0;
		int32_t rev_index = 0;

		m_dropped_packets.clear();

		{
			int32_t packet_num = m_last_inorder + 1;

			for (const auto& pair: m_waiting_packets) {
				const int32_t diff = pair.first - packet_num;

				for (int32_t i = 0; i < diff; ++i) {
					m_dropped_packets.push_back(packet_num++);
				}

				packet_num++;
			}

			while (!m_dropped_packets.empty() && (m_dropped_packets.back() - (m_last_inorder + 1)) > 255) {
				m_dropped_packets.pop_back();
			}

			uint32_t num_continuous_pkts = 0;

			for (size_t i = 0; i != m_dropped_packets.size(); ++i) {
				if (size_t(m_dropped_packets[i]) != (m_last_inorder + i + 1))
					break;

				num_continuous_pkts += 1;
			}

			if ((num_continuous_pkts < 8) && (curr_send_time - m_prv_nak_time).count() > (max_unack_time.count() * 0.5f)) {
				nak_count = int8_t(std::min(m_dropped_packets.size(), size_t(127)));
				// needs 1 byte per requested packet, so do not spam to often
				m_prv_nak_time = curr_send_time;
			} else {
				nak_count = -int8_t(std::min(127u, num_continuous_pkts));
			}
		}

		if (!m_unacked_chunks.empty() && chunk_delta_time.count() > max_unack_time.count() && unack_delta_time.count() > max_unack_time.count()) {
			// resend last packet if we didn't get an ack within reasonable time
			// and don't plan sending out a new chunk either
			if (m_new_chunks.empty())
				request_resend(*m_unacked_chunks.rbegin());

			m_prv_unack_resend_time = curr_send_time;
		}


		const bool flush_send = (flushed || !m_new_chunks.empty());
		const bool other_send = (use_min_loss_factor() && !m_resend_req_pkts.empty());
		const bool unack_send = (nak_count > 0) || (diff_send_time.count() > (max_unack_time.count() * 0.5f));

		if (!flush_send && !other_send && !unack_send)
			return;

		size_t max_resend_size = m_resend_req_pkts.size();
		size_t unack_prev_size = m_unacked_chunks.size();

		std::map<int32_t, std::shared_ptr<udp_packet_chunk> >::iterator resend_iter_fwd = m_resend_req_pkts.begin();
		std::map<int32_t, std::shared_ptr<udp_packet_chunk> >::iterator resend_iter_mid;
		std::map<int32_t, std::shared_ptr<udp_packet_chunk> >::iterator resend_iter_beg;
		std::map<int32_t, std::shared_ptr<udp_packet_chunk> >::iterator resend_iter_end;
		std::map<int32_t, std::shared_ptr<udp_packet_chunk> >::reverse_iterator resend_iter_rev;

		if (!use_min_loss_factor()) {
			// limit resend to a reasonable number of packet chunks
			max_resend_size = std::min(max_resend_size, size_t(20 * m_netloss_factor));

			resend_iter_mid = m_resend_req_pkts.begin();
			resend_iter_beg = m_resend_req_pkts.begin();
			resend_iter_end = m_resend_req_pkts.end();
			resend_iter_rev = m_resend_req_pkts.rbegin();

			std::advance(resend_iter_beg, (max_resend_size + 3) / 4);

			if (resend_iter_beg != m_resend_req_pkts.end() && m_last_mid_chunk < resend_iter_beg->first)
				m_last_mid_chunk = resend_iter_beg->first - 1;

			std::advance(resend_iter_end, -((max_resend_size + 2) / 4));

			while (resend_iter_mid != m_resend_req_pkts.end() && resend_iter_mid->first <= m_last_mid_chunk) {
				++resend_iter_mid;
			}

			if (resend_iter_mid == m_resend_req_pkts.end() || resend_iter_end == m_resend_req_pkts.end() || resend_iter_mid->first >= resend_iter_end->first)
				resend_iter_mid = resend_iter_beg;
		}

		while (((m_outgoing_bw_tracker.get_average(false) <= config::link_outgoing_bandwidth) || (config::link_outgoing_bandwidth <= 0))) {
			udp_packet pkt(m_last_inorder, nak_count);

			if (nak_count > 0) {
				pkt.naks.resize(nak_count);

				for (uint32_t i = 0; i != pkt.naks.size(); ++i) {
					pkt.naks[i] = m_dropped_packets[i] - (m_last_inorder + 1); // zero means request resend of m_last_inorder + 1
				}

				// one request is enough, unless high loss
				nak_count *= (1 - use_min_loss_factor());
			}


			bool sent = false;

			while (true) {
				const size_t buffer_size = pkt.calc_size();
				const size_t resend_size = ((use_min_loss_factor() || (rev_index == 0)) ? resend_iter_fwd->second->calc_size() : ((rev_index == 1) ? resend_iter_rev->second->calc_size() : resend_iter_mid->second->calc_size())); // resend chunk size

				const bool can_resend = (max_resend_size > 0) && ((buffer_size + resend_size) <= m_max_transmission_unit);
				const bool can_send_new = !m_new_chunks.empty() && ((buffer_size + m_new_chunks[0]->calc_size()) <= m_max_transmission_unit);

				if (!can_resend && !can_send_new)
					break;

				// alternate between send and resend to make sure none is starved
				m_resend = !m_resend;

				if (m_resend && can_resend) {
					if (use_min_loss_factor()) {
						pkt.chunks.push_back(resend_iter_fwd->second);
						resend_iter_fwd = m_resend_req_pkts.erase(resend_iter_fwd);
					} else {
						// on a lossy connection, just keep resending until chunk is acked
						// alternate between sending from front, middle and back of requested
						// chunks, since this improves performance on high latency connections
						switch (rev_index) {
							case 0: {
								pkt.chunks.push_back((resend_iter_fwd++)->second);
							} break;
							case 1: {
								pkt.chunks.push_back((resend_iter_rev++)->second);
							} break;
							case 2:
							case 3: {
								pkt.chunks.push_back(resend_iter_mid->second);
								m_last_mid_chunk = resend_iter_mid->first;

								if ((++resend_iter_mid) == resend_iter_end)
									resend_iter_mid = resend_iter_beg;
							} break;
						}

						rev_index = (rev_index + 1) % 4;
					}

					m_resent_chunks += 1;
					max_resend_size -= 1;

					sent = true;
					continue;
				}

				if (!m_resend && can_send_new) {
					pkt.chunks.push_back(m_new_chunks[0]);
					m_unacked_chunks.push_back(m_new_chunks[0]);
					m_new_chunks.pop_front();

					sent = true;
					continue;
				}
			}

			emulate_packet_corruption(pkt.checksum = pkt.calc_checksum(m_crc));
			send_packet(pkt);

			if (!sent || (max_resend_size == 0 && m_new_chunks.empty()))
				break;
		}

		if (!use_min_loss_factor()) {
			// on a lossy connection the packet will be sent multiple times
			for (size_t i = unack_prev_size; i < m_unacked_chunks.size(); ++i) {
				request_resend(m_unacked_chunks[i]);
			}
		}
	}

	void udp_connection::send_packet(udp_packet& pkt) {
		pkt.serialize(m_send_buffer);
		m_outgoing_bw_tracker.data_sent(m_send_buffer.size(), false);

		asio::ip::udp::socket::message_flags msg_flags = 0;
		asio::error_code error_code;

		if (!emulate_latency(m_send_buffer, msg_flags, error_code, emulate_packet_loss(m_loss_counter)))
			m_socket->send_to(asio::buffer(m_send_buffer), m_net_address, msg_flags, error_code);

		if (check_error_code(error_code))
			return;

		m_prv_packet_send_time = std::chrono::high_resolution_clock::now();
		m_data_sent += m_send_buffer.size();
		m_sent_packets += 1;
	}

	void udp_connection::ack_chunks(int32_t last_ack) {
		while (!m_unacked_chunks.empty() && (last_ack >= (*m_unacked_chunks.begin())->chunk_number)) {
			m_unacked_chunks.pop_front();
		}

		// resend requested and later acked, happens every now and then
		while (!m_resend_req_pkts.empty() && last_ack >= m_resend_req_pkts.begin()->first) {
			m_resend_req_pkts.erase(m_resend_req_pkts.begin());
		}
	}

	void udp_connection::request_resend(std::shared_ptr<udp_packet_chunk> ptr) {
		// filter out duplicates
		if (m_resend_req_pkts.find(ptr->chunk_number) != m_resend_req_pkts.end())
			return;

		m_resend_req_pkts[ptr->chunk_number] = ptr;
	}

	void udp_connection::close(bool flush_) {
		if (m_closed)
			return;

		flush(flush_);
		m_muted = true;

		if (!m_shared_socket) {
			try {
				m_socket->close();
			} catch (const asio::system_error& ex) {
				fprintf(stderr, "[%s] failed closing UDP connection: %s", __func__, ex.what());
			}
		}

		// FIXME: only if no exception?
		m_closed = true;
	}
}

