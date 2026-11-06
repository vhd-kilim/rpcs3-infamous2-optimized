#pragma once

#include "RSXDiagnostics.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <mutex>

// Opt-in (RPCS3_RSX_DIAGNOSTICS=1) record of guest writes to the NV406E semaphores the RSX waits on.
// SPU DMA/atomic writes are attributed to the writing SPU thread, the job module it last loaded at
// LS 0x4000 and the module whose code is at its PC. Durations are wall time.
namespace rsx::diagnostics::labels
{
	// inFAMOUS 2 semaphores measured in RSX_DIAG as the main RSX waits.
	inline constexpr std::array<std::uint32_t, 2> watched{0x40300870, 0x40300890};

	// Events a module start may follow: other modules' releases plus RSX-side markers.
	inline constexpr const char* rsx_flip_event = "rsx_flip";
	inline constexpr const char* rsx_wait_870_end_event = "rsx_wait_870_end";
	inline constexpr const char* rsx_wait_890_end_event = "rsx_wait_890_end";

	inline void mark_event(const char* name) noexcept;

	enum class path : std::uint8_t
	{
		dma,
		putllc,
		putlluc,
	};

	inline std::uint64_t now_ns() noexcept
	{
		return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
	}

	// Per SPU thread: the job module most recently DMA'd into LS 0x4000.
	struct job_state
	{
		const char* module = nullptr;
		std::uint64_t loaded_ns = 0;
	};

	inline job_state& current_job() noexcept
	{
		thread_local job_state value{};
		return value;
	}

	inline constexpr std::size_t slot_count = 16;

	struct sample
	{
		std::uint32_t address = 0;
		path kind = path::dma;
		const char* job = nullptr; // Last module loaded at LS 0x4000
		const char* code = nullptr; // Module whose code contains the writing PC (nullptr: SPURS kernel or unknown)
		std::uint32_t pc = 0;

		std::uint64_t writes = 0;
		std::uint64_t while_waiting = 0; // RSX was already waiting on this address
		std::uint64_t wait_ns = 0; // Sum of (write - RSX wait begin) for writes while waiting
		std::uint64_t max_wait_ns = 0;
		std::uint64_t job_ns = 0; // Sum of (write - job module load)
		std::uint64_t max_job_ns = 0;

		// Activity of the writing module (see code_dma): first DMA issued by its code since its previous
		// release -> this release, and the idle gap before that first DMA.
		std::uint64_t active_ns = 0;
		std::uint64_t max_active_ns = 0;
		std::uint64_t idle_ns = 0;
		std::uint64_t rsx_before_start = 0; // RSX was already waiting before the module started
		std::uint64_t workers = 0; // Sum over releases of distinct SPU threads whose code DMA'd during the run

		bool same_site(const sample& o) const noexcept
		{
			return address == o.address && kind == o.kind && job == o.job && code == o.code && pc == o.pc;
		}
	};

	struct snapshot
	{
		std::array<sample, slot_count> slots{};
		sample overflow{};
		std::array<std::uint64_t, watched.size()> rsx_waits{};
		std::array<std::uint64_t, watched.size()> rsx_wait_ns{};
		std::uint64_t job_loads = 0;
	};

	class registry
	{
	public:
		void record(const sample& event, std::uint64_t wait_ns, bool waiting, std::uint64_t job_ns,
			std::uint64_t active_ns = 0, std::uint64_t idle_ns = 0, bool rsx_before_start = false, std::uint64_t workers = 0) noexcept
		{
			std::lock_guard lock(m_mutex);
			sample* target = &m_window.overflow;
			for (auto& slot : m_window.slots)
			{
				if (slot.writes && !slot.same_site(event)) continue;
				if (!slot.writes) slot = event;
				target = &slot;
				break;
			}

			target->writes++;
			if (waiting)
			{
				target->while_waiting++;
				target->wait_ns += wait_ns;
				target->max_wait_ns = std::max(target->max_wait_ns, wait_ns);
			}
			if (job_ns)
			{
				target->job_ns += job_ns;
				target->max_job_ns = std::max(target->max_job_ns, job_ns);
			}
			target->active_ns += active_ns;
			target->max_active_ns = std::max(target->max_active_ns, active_ns);
			target->idle_ns += idle_ns;
			target->rsx_before_start += rsx_before_start;
			target->workers += workers;
		}

		void rsx_wait_finished(std::size_t index, std::uint64_t ns) noexcept
		{
			std::lock_guard lock(m_mutex);
			m_window.rsx_waits[index]++;
			m_window.rsx_wait_ns[index] += ns;
		}

		void job_loaded() noexcept
		{
			std::lock_guard lock(m_mutex);
			m_window.job_loads++;
		}

		snapshot take() noexcept
		{
			std::lock_guard lock(m_mutex);
			const snapshot result = m_window;
			m_window = {};
			return result;
		}

		// RSX side: begin timestamp of the current wait per watched address (0 = not waiting).
		std::array<std::atomic<std::uint64_t>, watched.size()> wait_begin{};

	private:
		std::mutex m_mutex;
		snapshot m_window{};
	};

	inline registry& global() noexcept
	{
		static registry value;
		return value;
	}

	inline int watched_index(std::uint32_t address) noexcept
	{
		for (std::size_t i = 0; i < watched.size(); i++)
		{
			if (watched[i] == address) return static_cast<int>(i);
		}
		return -1;
	}

	// RSX thread: an NV406E acquire on address starts/stops waiting.
	inline void rsx_wait_begin(std::uint32_t address) noexcept
	{
		if (const int i = watched_index(address); i >= 0)
		{
			global().wait_begin[i].store(now_ns());
		}
	}

	inline void rsx_wait_end(std::uint32_t address) noexcept
	{
		if (const int i = watched_index(address); i >= 0)
		{
			const auto begin = global().wait_begin[i].exchange(0);
			if (begin)
			{
				const auto now = now_ns();
				global().rsx_wait_finished(i, now > begin ? now - begin : 0);
				mark_event(i == 0 ? rsx_wait_870_end_event : rsx_wait_890_end_event);
			}
		}
	}

	class rsx_wait final
	{
	public:
		rsx_wait(std::uint32_t address, bool active) noexcept
			: m_address(address), m_active(active)
		{
			if (m_active) rsx_wait_begin(m_address);
		}

		~rsx_wait() noexcept
		{
			if (m_active) rsx_wait_end(m_address);
		}

		rsx_wait(const rsx_wait&) = delete;
		rsx_wait& operator=(const rsx_wait&) = delete;

	private:
		std::uint32_t m_address;
		bool m_active;
	};

	// Job code stays cached in LS between runs, so a module's start is taken from the first DMA its code
	// issues after its previous semaphore release (any SPU thread).
	struct activity
	{
		std::atomic<const char*> module{nullptr};
		std::atomic<std::uint64_t> first_ns{0};
		std::atomic<std::uint64_t> last_release_ns{0};
		std::atomic<std::uint64_t> threads{0}; // SPU threads (id & 63) whose code DMA'd during the current run
	};

	inline activity* find_activity(const char* module) noexcept
	{
		static std::array<activity, 48> table{};
		for (auto& a : table)
		{
			// Names are compared by content: the same name can come from different string literals.
			const auto same = [&](const char* m) { return m == module || (m && std::strcmp(m, module) == 0); };
			const char* m = a.module.load();
			if (same(m)) return &a;
			if (!m && a.module.compare_exchange_strong(m, module)) return &a;
			if (same(m)) return &a; // Raced with another thread inserting the same module
		}
		return nullptr;
	}

	inline void mark_event(const char* name) noexcept
	{
		if (auto* a = find_activity(name))
		{
			a->last_release_ns.store(now_ns());
		}
	}

	// Which event most recently preceded each start of a module (module x predecessor).
	struct start_cause
	{
		const char* module = nullptr;
		const char* after = nullptr;
		std::uint64_t starts = 0;
		std::uint64_t delta_ns = 0; // Sum of (start - predecessor event)
	};

	struct start_causes
	{
		std::mutex mutex;
		std::array<start_cause, 32> slots{};
	};

	inline start_causes& causes() noexcept
	{
		static start_causes value;
		return value;
	}

	inline void record_start(const char* module, std::uint64_t now) noexcept
	{
		// Find the latest event (release/marker) before now among all tracked entries.
		const char* after = nullptr;
		std::uint64_t latest = 0;
		for (const char* name : {rsx_flip_event, rsx_wait_870_end_event, rsx_wait_890_end_event})
		{
			if (auto* a = find_activity(name); a)
			{
				const auto t = a->last_release_ns.load();
				if (t && t <= now && t > latest) { latest = t; after = name; }
			}
		}

		for (const char* m : {"spulight", "edgepost", "postfx1", "postfx2", "postfx3", "postfx4", "postfx5"})
		{
			if (std::strcmp(m, module) == 0) continue;
			if (auto* a = find_activity(m); a)
			{
				const auto t = a->last_release_ns.load();
				if (t && t <= now && t > latest) { latest = t; after = m; }
			}
		}

		if (!after) return;

		auto& c = causes();
		std::lock_guard lock(c.mutex);
		for (auto& s : c.slots)
		{
			if (s.starts && (s.module != module || s.after != after)) continue;
			s.module = module;
			s.after = after;
			s.starts++;
			s.delta_ns += now - latest;
			return;
		}
	}

	inline std::array<start_cause, 32> take_causes() noexcept
	{
		auto& c = causes();
		std::lock_guard lock(c.mutex);
		const auto result = c.slots;
		c.slots = {};
		return result;
	}

	// SPU thread: a DMA issued by code of module (module names are static strings).
	inline void code_dma(const char* module, std::uint32_t thread_slot) noexcept
	{
		auto* a = find_activity(module);
		if (!a)
		{
			return;
		}

		if (!a->first_ns.load())
		{
			// Tail DMAs right after the release still belong to the finished run.
			const auto now = now_ns();
			if (now - a->last_release_ns.load() < 100'000) return;
			std::uint64_t expected = 0;
			if (a->first_ns.compare_exchange_strong(expected, now))
			{
				record_start(module, now);
			}
		}

		a->threads.fetch_or(1ull << (thread_slot & 63));
	}

	// SPU thread: a guest write to [eal, eal + size).
	// Called only when diagnostics are enabled. Returns quickly for unrelated ranges.
	inline void spu_write(std::uint32_t eal, std::uint32_t size, path kind, std::uint32_t pc, const char* code) noexcept
	{
		for (std::size_t i = 0; i < watched.size(); i++)
		{
			const auto address = watched[i];
			if (address < eal || address - eal + 4 > size) continue;

			sample event{};
			event.address = address;
			event.kind = kind;
			event.job = current_job().module;
			event.code = code;
			event.pc = pc;

			const auto now = now_ns();
			const auto begin = global().wait_begin[i].load();
			const auto loaded = current_job().loaded_ns;

			std::uint64_t active = 0, idle = 0, workers = 0;
			bool rsx_first = false;
			if (auto* a = code ? find_activity(code) : nullptr)
			{
				workers = std::popcount(a->threads.exchange(0));
				const auto first = a->first_ns.exchange(0);
				const auto previous = a->last_release_ns.exchange(now);
				if (first && now > first)
				{
					active = now - first;
					idle = previous && first > previous ? first - previous : 0;
					rsx_first = begin && begin < first;
				}
			}

			global().record(event, begin && now > begin ? now - begin : 0, begin != 0, loaded && now > loaded ? now - loaded : 0, active, idle, rsx_first, workers);
		}
	}

	inline bool range_has_watched(std::uint32_t eal, std::uint32_t size) noexcept
	{
		for (const auto address : watched)
		{
			if (address >= eal && address - eal + 4 <= size) return true;
		}
		return false;
	}
}
