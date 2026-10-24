#pragma once

#include "Infamous2LightWaits.h"
#include "Emu/Cell/PPUThread.h"
#include "Emu/System.h"
#include "Emu/system_config.h"
#include "util/logs.hpp"
#include <cstdlib>

LOG_CHANNEL(infamous2_light_log, "INFAMOUS2_LIGHT");

namespace infamous2::light_profile
{
	inline bool enabled() noexcept
	{
		static const bool requested = []
		{
#ifdef _MSC_VER
			char value[2]{};
			std::size_t length = 0;
			return getenv_s(&length, value, sizeof(value), "RPCS3_INFAMOUS2_LIGHT_PROFILE") == 0 &&
				length == 2 && exact_opt_in(std::string_view(value, 1));
#else
			const char* value = std::getenv("RPCS3_INFAMOUS2_LIGHT_PROFILE");
			return value && exact_opt_in(value);
#endif
		}();
		return requested;
	}

	struct profile_context
	{
		basic_tracker<> tracker;
		const ppu_thread* owner = nullptr;
		std::uint32_t ppu_id = 0;
		std::uint32_t context_addr = 0;
		unsigned sleep_accuracy = 0;
		std::int64_t usleep_addend = 0;
	};

	inline profile_context& local_context() noexcept
	{
		// One ODR-shared TLS object for both HLE translation units, not one per TU.
		static thread_local profile_context value;
		return value;
	}

	inline void emit(const profile_context& context, const snapshot& value)
	{
		std::string text = fmt::format("INFAMOUS2_LIGHT_WAITS {\"schema\":1,\"title_id\":\"BCUS98125\","
			"\"app_version\":\"01.04\",\"ppu_hash\":\"PPU-41a7d047c573c78c0307506c9d1b52d15a66e4f5\","
			"\"ppu_id\":%u,\"context_addr\":%u,\"window_index\":%llu,\"window_wall_us\":%llu,"
			"\"sleep_accuracy\":%u,\"usleep_addend\":%lld,\"config_sample\":\"first_target_entry_in_window\","
			"\"measure\":\"hle_attempt_wall\",\"state_order\":[0,1,2,3,4,\"unknown\"],"
			"\"counter_order\":[\"attempts\",\"wall_us_sum\",\"wall_us_max\",\"requested_us\",\"again_exits\"],\"usleep\":[",
			context.ppu_id, context.context_addr, value.window_index, value.window_wall_us,
			context.sleep_accuracy, context.usleep_addend);
		for (unsigned kind = 0; kind < 2; ++kind)
		{
			if (kind) text += "],\"semaphore\":[";
			for (unsigned state = 0; state < state_count; ++state)
			{
				if (state) text += ',';
				const auto& sample = value.by_kind_state[kind][state];
				fmt::append(text, "[%llu,%llu,%llu,%llu,%llu]", sample.attempts, sample.wall_us_sum,
					sample.wall_us_max, sample.requested_us, sample.again_exits);
			}
		}
		text += "]}";
		infamous2_light_log.notice("%s", text);
	}

	class wait_scope
	{
	public:
		wait_scope(const ppu_thread& ppu, wait_kind kind, std::uint64_t requested_us = 0) noexcept
			: m_kind(kind), m_requested_us(requested_us)
		{
			// Unrelated syscall sites pay only the CIA comparison, before env/config/clocks.
			if (!site_matches(kind, ppu.cia) || !enabled() ||
				!identity_matches(Emu.GetTitleID(), Emu.GetAppVersion(), Emu.GetExecutableHash())) return;
			auto& context = local_context();
			const auto address = static_cast<std::uint32_t>(ppu.gpr[31]);
			if (context.owner != &ppu || context.ppu_id != ppu.id || context.context_addr != address)
			{
				context.tracker.reset();
				context.owner = &ppu;
				context.ppu_id = ppu.id;
				context.context_addr = address;
			}
			if (context.tracker.empty())
			{
				context.sleep_accuracy = static_cast<unsigned>(g_cfg.core.sleep_timers_accuracy.get());
				context.usleep_addend = g_cfg.core.usleep_addend.get();
			}
			m_state = state_from_register(kind, ppu.gpr[0]);
			m_context = &context;
			m_ppu = &ppu;
			m_started = context.tracker.begin();
		}

		wait_scope(const wait_scope&) = delete;
		wait_scope& operator=(const wait_scope&) = delete;

		~wait_scope() noexcept
		{
			if (!m_context) return;
			snapshot ready;
			// No return-code inference from gpr[3]: binder writes the result afterwards.
			if (m_context->tracker.finish(m_started, m_kind, m_state, m_requested_us,
				!!(m_ppu->state & cpu_flag::again), ready)) emit(*m_context, ready);
		}

	private:
		wait_kind m_kind;
		std::uint64_t m_requested_us = 0;
		unsigned m_state = unknown_state;
		profile_context* m_context = nullptr;
		const ppu_thread* m_ppu = nullptr;
		basic_tracker<>::time_point m_started{};
	};
}
