#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string_view>

// Standalone accounting only. No PPU/VM, logging, locks or configuration access.
namespace infamous2::light_profile
{
	enum class wait_kind : unsigned { usleep, semaphore };
	inline constexpr unsigned state_count = 6; // 0..4, then unknown register value.
	inline constexpr unsigned unknown_state = 5;
	inline constexpr std::uint64_t report_interval_us = 1'000'000;

	inline bool exact_opt_in(std::string_view value) noexcept { return value == "1"; }

	inline bool identity_matches(std::string_view title, std::string_view version, std::string_view hash) noexcept
	{
		return title == "BCUS98125" && version == "01.04" &&
			hash == "PPU-41a7d047c573c78c0307506c9d1b52d15a66e4f5";
	}

	inline bool site_matches(wait_kind kind, std::uint32_t cia) noexcept
	{
		return cia == (kind == wait_kind::usleep ? 0x00350c7cu : 0x00350ca8u);
	}

	inline unsigned state_from_register(wait_kind kind, std::uint64_t r0) noexcept
	{
		// Exact SC paths leave sign-extended state*4 in r0. Valid states are small.
		if (kind == wait_kind::usleep && r0 <= 12 && !(r0 & 3)) return static_cast<unsigned>(r0 / 4);
		if (kind == wait_kind::semaphore && r0 == 16) return 4;
		return unknown_state;
	}

	struct counters
	{
		std::uint64_t attempts = 0;
		std::uint64_t wall_us_sum = 0;
		std::uint64_t wall_us_max = 0;
		std::uint64_t requested_us = 0; // Original usleep argument; not effective host sleep.
		std::uint64_t again_exits = 0;
	};

	struct snapshot
	{
		std::uint64_t window_index = 0;
		std::uint64_t window_wall_us = 0;
		std::array<std::array<counters, state_count>, 2> by_kind_state{};
	};

	template <typename Clock = std::chrono::steady_clock>
	class basic_tracker
	{
	public:
		using time_point = typename Clock::time_point;

		bool empty() const noexcept { return m_attempts == 0; }
		const snapshot& pending() const noexcept { return m_window; }

		void reset() noexcept
		{
			m_started = false;
			m_attempts = 0;
			m_next_index = 1;
			m_window = {};
		}

		time_point begin() noexcept
		{
			const auto now = Clock::now();
			if (!m_started) { m_started = true; m_epoch = now; }
			return now;
		}

		// Returns one aggregate only after >=1s has elapsed and an attempt returns.
		// A blocked attempt is not split across windows; no destructor flush occurs.
		bool finish(time_point started, wait_kind kind, unsigned state, std::uint64_t requested_us,
			bool again, snapshot& ready) noexcept
		{
			const auto now = Clock::now();
			const auto wall_us = elapsed_us(started, now);
			auto& sample = m_window.by_kind_state[static_cast<unsigned>(kind)][state < state_count ? state : unknown_state];
			++sample.attempts;
			++m_attempts;
			sample.wall_us_sum += wall_us;
			if (wall_us > sample.wall_us_max) sample.wall_us_max = wall_us;
			if (kind == wait_kind::usleep) sample.requested_us += requested_us;
			sample.again_exits += again ? 1 : 0;
			const auto elapsed = elapsed_us(m_epoch, now);
			if (elapsed < report_interval_us) return false;
			m_window.window_index = m_next_index++;
			m_window.window_wall_us = elapsed;
			ready = m_window;
			m_window = {};
			m_attempts = 0;
			m_epoch = now;
			return true;
		}

	private:
		static std::uint64_t elapsed_us(time_point start, time_point end) noexcept
		{
			if (end <= start) return 0;
			return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(end - start).count());
		}
		bool m_started = false;
		std::uint64_t m_attempts = 0;
		std::uint64_t m_next_index = 1;
		time_point m_epoch{};
		snapshot m_window{};
	};
}
