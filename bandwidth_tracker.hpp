#ifndef ARELION_BANDWITDH_TRACKER_HDR
#define ARELION_BANDWITDH_TRACKER_HDR

#include <cstdint>

namespace arelion {
	struct bandwidth_tracker {
	public:
		void update_time(uint32_t dif_time_ms) {
			if (dif_time_ms <= (m_last_time_ms + 100))
				return;

			m_average = (m_average * 9.0f + m_traffic_since_last_time / float(dif_time_ms - m_last_time_ms) * 1000.0f) / 10.0f;

			m_traffic_since_last_time = 0;
			m_prel_traffic_since_last_time = 0;
			m_last_time_ms = dif_time_ms;
		}

		void data_sent(uint32_t amount, bool prel) {
			if (prel) {
				m_prel_traffic_since_last_time += amount;
			} else {
				m_traffic_since_last_time += amount;
			}
		}

		float get_average(bool prel) const {
			// not an exact average, but good enough
			return (m_average + std::max(m_traffic_since_last_time, m_prel_traffic_since_last_time * prel));
		}

	private:
		uint32_t m_last_time_ms = 0;
		uint32_t m_traffic_since_last_time = 1;
		uint32_t m_prel_traffic_since_last_time = 0;

		float m_average = 0.0f;
	};
}

#endif

