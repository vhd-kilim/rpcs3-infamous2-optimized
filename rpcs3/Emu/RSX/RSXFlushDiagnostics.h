#pragma once

#include "RSXDiagnostics.h"

#include <array>
#include <cstdint>
#include <mutex>
#include <string_view>

// Opt-in (RPCS3_RSX_DIAGNOSTICS=1) record of texture-cache flushes forced by guest
// memory faults. Producers are PPU/SPU/other threads, so unlike the thread-local RSX
// tracker this is a shared, mutex-guarded window. Durations are wall time.
namespace rsx::diagnostics::flushes
{
	enum class source : std::uint8_t
	{
		other,
		ppu,
		spu,
		rsx,
		offloader,
	};

	// Distinct (source, pc, lr, first section, direction) sites per window; the rest go to overflow.
	inline constexpr std::size_t slot_count = 16;

	struct sample
	{
		source kind = source::other;
		bool writing = false;
		std::uint32_t pc = 0;
		std::uint32_t lr = 0;
		std::uint32_t base = 0; // First flushed section
		std::uint32_t context_mask = 0; // texture_upload_context bits of all flushed sections
		std::uint32_t format = 0;
		std::uint16_t width = 0;
		std::uint16_t height = 0;
		std::array<char, 48> thread{};
		std::array<char, 16> module{};

		std::uint64_t events = 0;
		std::uint64_t sections = 0;
		std::uint64_t synchronized = 0; // Sections already read back (speculatively) when the fault arrived
		std::uint64_t bytes = 0;
		std::uint64_t service_ns = 0; // Queue lock + waiting for the RSX to serve the request
		std::uint64_t transfer_ns = 0; // flush_all: GPU readback and write to guest memory
		std::uint64_t max_ns = 0;

		bool same_site(const sample& other) const noexcept
		{
			return kind == other.kind && writing == other.writing && pc == other.pc && lr == other.lr && base == other.base;
		}
	};

	inline void copy_label(std::array<char, 48>& out, std::string_view text) noexcept
	{
		out = {};
		for (std::size_t i = 0; i < text.size() && i + 1 < out.size(); i++)
		{
			const char c = text[i];
			// Kept JSON-safe without escaping.
			out[i] = (c == '"' || c == '\\' || static_cast<unsigned char>(c) < 0x20) ? '_' : c;
		}
	}

	inline void copy_label(std::array<char, 16>& out, std::string_view text) noexcept
	{
		out = {};
		for (std::size_t i = 0; i < text.size() && i + 1 < out.size(); i++)
		{
			out[i] = text[i];
		}
	}

	struct snapshot
	{
		std::array<sample, slot_count> slots{};
		sample overflow{};
		std::uint64_t rsx_services = 0;
		std::uint64_t rsx_queue_flush_ns = 0; // RSX flush_command_queue before releasing producers
		std::uint64_t rsx_consumer_wait_ns = 0; // RSX waiting for producers to finish their transfer
		std::uint64_t rsx_max_consumer_wait_ns = 0;
		std::uint64_t prefetch_copies = 0; // Speculative readbacks queued by the RSX
		std::uint64_t prefetch_submits = 0; // Early submits of those copies
		std::uint64_t early_submits = 0; // RPCS3_INFAMOUS2_EARLY_SUBMIT submits
	};

	class registry
	{
	public:
		void record(const sample& event) noexcept
		{
			std::lock_guard lock(m_mutex);

			sample* target = &m_window.overflow;
			for (auto& slot : m_window.slots)
			{
				if (slot.events && !slot.same_site(event))
				{
					continue;
				}

				if (!slot.events)
				{
					slot = event;
					slot.events = slot.sections = slot.synchronized = slot.bytes = slot.service_ns = slot.transfer_ns = slot.max_ns = 0;
				}

				target = &slot;
				break;
			}

			target->events++;
			target->sections += event.sections;
			target->synchronized += event.synchronized;
			target->bytes += event.bytes;
			target->service_ns += event.service_ns;
			target->transfer_ns += event.transfer_ns;
			const auto total = event.service_ns + event.transfer_ns;
			if (total > target->max_ns)
			{
				target->max_ns = total;
			}
		}

		void rsx_serviced(std::uint64_t queue_flush_ns, std::uint64_t consumer_wait_ns) noexcept
		{
			std::lock_guard lock(m_mutex);
			m_window.rsx_services++;
			m_window.rsx_queue_flush_ns += queue_flush_ns;
			m_window.rsx_consumer_wait_ns += consumer_wait_ns;
			if (consumer_wait_ns > m_window.rsx_max_consumer_wait_ns)
			{
				m_window.rsx_max_consumer_wait_ns = consumer_wait_ns;
			}
		}

		void prefetched(std::uint64_t copies) noexcept
		{
			std::lock_guard lock(m_mutex);
			m_window.prefetch_copies += copies;
		}

		void prefetch_submitted() noexcept
		{
			std::lock_guard lock(m_mutex);
			m_window.prefetch_submits++;
		}

		void early_submitted() noexcept
		{
			std::lock_guard lock(m_mutex);
			m_window.early_submits++;
		}

		snapshot take() noexcept
		{
			std::lock_guard lock(m_mutex);
			const snapshot result = m_window;
			m_window = {};
			return result;
		}

	private:
		std::mutex m_mutex;
		snapshot m_window{};
	};

	inline registry& global() noexcept
	{
		static registry value;
		return value;
	}

	inline std::uint64_t ns_between(std::chrono::steady_clock::time_point begin, std::chrono::steady_clock::time_point end) noexcept
	{
		const auto value = std::chrono::duration_cast<std::chrono::nanoseconds>(end - begin).count();
		return value > 0 ? static_cast<std::uint64_t>(value) : 0;
	}

	inline std::uint64_t elapsed_ns(std::chrono::steady_clock::time_point since) noexcept
	{
		return ns_between(since, std::chrono::steady_clock::now());
	}
}
