#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <utility>

// Opt-in wall-time diagnostics. These durations include preemption and are not
// CPU-time measurements. No emulator, renderer or logging dependencies belong here.
namespace rsx::diagnostics
{
	enum class bucket : std::size_t
	{
		wake_delay,
		offloader_sync,
		semaphore_wait,
		gpu_submit_wait,
		gpu_fence_wait,
		count,
	};

	inline constexpr std::size_t bucket_count = static_cast<std::size_t>(bucket::count);
	// Values match FIFO::state, without depending on RSXFIFO.h.
	inline constexpr unsigned fifo_state_count = 6;
	inline constexpr unsigned unknown_fifo_state = std::numeric_limits<unsigned>::max();

	struct bucket_sample
	{
		std::uint64_t calls = 0;
		std::uint64_t inclusive_ns = 0;
		std::uint64_t exclusive_ns = 0;
	};

	struct snapshot
	{
		std::uint64_t elapsed_ns = 0;
		std::uint64_t flips = 0;
		std::uint64_t wake_requested_us = 0;
		std::uint64_t wake_div2_calls = 0;
		std::array<bucket_sample, bucket_count> bucket_samples{};
		std::array<std::uint64_t, fifo_state_count> fifo_ns{};
		std::array<std::uint64_t, fifo_state_count> fifo_transitions{};
		unsigned current_fifo_state = unknown_fifo_state;
	};

	inline bool enabled() noexcept
	{
		// Cached once per process. Only the exact value "1" opts in.
		static const bool value = []
		{
#ifdef _MSC_VER
			char setting[2]{};
			std::size_t length = 0;
			return getenv_s(&length, setting, sizeof(setting), "RPCS3_RSX_DIAGNOSTICS") == 0 &&
				length == 2 && setting[0] == '1';
#else
			const char* setting = std::getenv("RPCS3_RSX_DIAGNOSTICS");
			return setting && setting[0] == '1' && setting[1] == '\0';
#endif
		}();
		return value;
	}

	// A fake steady clock can be supplied by a standalone test. Each instance is
	// confined to its owning thread; there are no atomics, allocations or locks.
	template <typename Clock = std::chrono::steady_clock>
	class basic_tracker
	{
	public:
		using clock = Clock;
		using time_point = typename clock::time_point;

		class scoped_bucket
		{
		public:
			explicit scoped_bucket(basic_tracker* owner, bucket kind, bool active = true) noexcept
				: m_owner(active && owner && owner->m_enabled && static_cast<std::size_t>(kind) < bucket_count ? owner : nullptr)
				, m_kind(kind)
			{
				if (!m_owner)
				{
					return;
				}

				m_started = clock::now();
				m_owner->ensure_started(m_started);
				m_parent = m_owner->m_top;
				m_owner->m_top = this;
			}

			explicit scoped_bucket(basic_tracker& owner, bucket kind, bool active = true) noexcept
				: scoped_bucket(&owner, kind, active)
			{
			}

			~scoped_bucket() noexcept
			{
				if (!m_owner)
				{
					return;
				}

				const auto elapsed = nanoseconds_between(m_started, clock::now());
				auto& sample = m_owner->m_window.bucket_samples[static_cast<std::size_t>(m_kind)];
				++sample.calls;
				sample.inclusive_ns += elapsed;
				sample.exclusive_ns += elapsed >= m_children_ns ? elapsed - m_children_ns : 0;

				// Lexical RAII lifetimes must close in reverse construction order.
				// Repeated/nested instances of the same bucket are supported.
				m_owner->m_top = m_parent;
				if (m_parent)
				{
					m_parent->m_children_ns += elapsed;
				}
			}

			scoped_bucket(const scoped_bucket&) = delete;
			scoped_bucket& operator=(const scoped_bucket&) = delete;
			scoped_bucket(scoped_bucket&&) = delete;
			scoped_bucket& operator=(scoped_bucket&&) = delete;

		private:
			basic_tracker* m_owner = nullptr;
			scoped_bucket* m_parent = nullptr;
			bucket m_kind;
			time_point m_started{};
			std::uint64_t m_children_ns = 0;
		};

		explicit basic_tracker(bool active = false) noexcept
			: m_enabled(active)
		{
		}

		basic_tracker(const basic_tracker&) = delete;
		basic_tracker& operator=(const basic_tracker&) = delete;
		basic_tracker(basic_tracker&&) = delete;
		basic_tracker& operator=(basic_tracker&&) = delete;

		// Call at RSX thread/emulation entry. Never invalidate a live scope.
		bool reset() noexcept
		{
			if (m_top)
			{
				return false;
			}

			m_window = {};
			m_started = false;
			m_fifo_state = unknown_fifo_state;
			return true;
		}

		void fifo_state_changed(unsigned state) noexcept
		{
			// Checking the state before reading the clock also makes repeated
			// notifications cheap; callers should still instrument transitions only.
			if (!m_enabled || state >= fifo_state_count || state == m_fifo_state)
			{
				return;
			}
			fifo_state_changed(state, clock::now());
		}

		void fifo_state_changed(unsigned state, time_point now) noexcept
		{
			if (!m_enabled || state >= fifo_state_count || state == m_fifo_state)
			{
				return;
			}

			ensure_started(now);
			account_fifo(now);
			m_fifo_state = state;
			m_fifo_since = now;
			++m_window.fifo_transitions[state];
		}

		void flip() noexcept
		{
			if (!m_enabled)
			{
				return;
			}
			if (!m_started)
			{
				ensure_started(clock::now());
			}
			++m_window.flips;
		}

		// actual_us is the already divided/rounded delay used by the caller.
		void requested_wake_delay(std::uint64_t actual_us, std::uint64_t divisor = 1) noexcept
		{
			if (m_enabled)
			{
				m_window.wake_requested_us += actual_us;
				m_window.wake_div2_calls += divisor == 2;
			}
		}

		// Call once per guest flip or another coarse event, not per RSX command.
		// A live scope defers publication intact until a subsequent tick/flush.
		template <typename Consumer>
		bool tick(Consumer&& consume, bool force = false)
		{
			if (!m_enabled || m_top)
			{
				return false;
			}
			return publish(std::forward<Consumer>(consume), clock::now(), force);
		}

		template <typename Consumer>
		bool flush(Consumer&& consume)
		{
			if (!m_enabled || m_top || !m_started)
			{
				return false;
			}
			return publish(std::forward<Consumer>(consume), clock::now(), true);
		}

	private:
		static std::uint64_t nanoseconds_between(time_point begin, time_point end) noexcept
		{
			const auto result = std::chrono::duration_cast<std::chrono::nanoseconds>(end - begin).count();
			return result > 0 ? static_cast<std::uint64_t>(result) : 0;
		}

		void ensure_started(time_point now) noexcept
		{
			if (!m_started)
			{
				m_started = true;
				m_epoch = now;
			}
		}

		void account_fifo(time_point now) noexcept
		{
			if (m_fifo_state < fifo_state_count)
			{
				m_window.fifo_ns[m_fifo_state] += nanoseconds_between(m_fifo_since, now);
				m_fifo_since = now;
			}
		}

		template <typename Consumer>
		bool publish(Consumer&& consume, time_point now, bool force)
		{
			ensure_started(now);
			const auto elapsed = nanoseconds_between(m_epoch, now);
			if (elapsed == 0 || (!force && elapsed < 1'000'000'000))
			{
				return false;
			}

			account_fifo(now);
			m_window.elapsed_ns = elapsed;
			m_window.current_fifo_state = m_fifo_state;
			const auto result = m_window;
			m_window = {};
			m_epoch = now;
			std::forward<Consumer>(consume)(result);
			return true;
		}

		bool m_enabled;
		bool m_started = false;
		time_point m_epoch{};
		time_point m_fifo_since{};
		unsigned m_fifo_state = unknown_fifo_state;
		snapshot m_window{};
		scoped_bucket* m_top = nullptr;
	};

	using tracker = basic_tracker<>;

	inline tracker& local() noexcept
	{
		thread_local tracker value(enabled());
		return value;
	}

	class scope final
	{
	public:
		explicit scope(bucket kind, bool active = true) noexcept
			: m_scope(active && enabled() ? &local() : nullptr, kind)
		{
		}

		scope(const scope&) = delete;
		scope& operator=(const scope&) = delete;
		scope(scope&&) = delete;
		scope& operator=(scope&&) = delete;

	private:
		tracker::scoped_bucket m_scope;
	};

	inline void fifo_state_changed(unsigned state) noexcept
	{
		if (enabled())
		{
			local().fifo_state_changed(state);
		}
	}

	inline void flip() noexcept
	{
		if (enabled())
		{
			local().flip();
		}
	}

	inline bool reset() noexcept
	{
		return !enabled() || local().reset();
	}

	inline void requested_wake_delay(std::uint64_t actual_us, std::uint64_t divisor = 1) noexcept
	{
		if (enabled())
		{
			local().requested_wake_delay(actual_us, divisor);
		}
	}

	template <typename Consumer>
	bool tick(Consumer&& consume, bool force = false)
	{
		return enabled() && local().tick(std::forward<Consumer>(consume), force);
	}

	template <typename Consumer>
	bool flush(Consumer&& consume)
	{
		return enabled() && local().flush(std::forward<Consumer>(consume));
	}
}
