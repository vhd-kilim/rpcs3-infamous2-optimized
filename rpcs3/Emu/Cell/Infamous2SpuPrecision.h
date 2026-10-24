#pragma once

#include "Emu/System.h"
#include "Emu/system_config.h"
#include "Emu/Memory/vm.h"
#include "util/logs.hpp"

#include <cstdlib>
#include <cstring>
#include <string_view>

// Opt-in per-module SPU XFloat override for inFAMOUS 2 (BCUS98125 01.04).
// RPCS3_INFAMOUS2_RELAXED=postfx[,light] compiles only SPU programs whose code is a
// verbatim copy of the listed EBOOT job modules with Relaxed accuracy; all other
// programs keep the configured accuracy (global Relaxed deforms NPC skinning).
namespace infamous2::spu_precision
{
	struct module_range
	{
		const char* name;
		const char* group;
		u32 va;
		u32 size;
	};

	// Job modules are loaded at LS 0x4000 (module byte 0 = LS 0x4000).
	inline constexpr u32 module_ls_base = 0x4000;

	inline constexpr module_range modules[]
	{
		{"postfx1", "postfx", 0x0087D500, 0x380},
		{"postfx2", "postfx", 0x0087D880, 0xC00},
		{"postfx3", "postfx", 0x0087E480, 0x800},
		{"postfx4", "postfx", 0x0087EC80, 0xC00},
		{"postfx5", "postfx", 0x0087F880, 0x1580},
		{"spulight", "light", 0x00875500, 0x5C60},
		{"edgepost", "edgepost", 0x0087B180, 0x2310},
		{"visprog", "vis", 0x0082F200, 0x2D40},
		{"visb", "vis", 0x00828C00, 0x65D0},
		{"visprogocc", "vis", 0x00823C00, 0x4FB0},
		{"spudraw", "spudraw", 0x00836300, 0x15B70},
		// Mesh/skinning: global Relaxed deforms NPC heads; split to find which one does it.
		{"spumesh", "mesh", 0x00880E00, 0xB6B0},
		{"edgegeomsort", "mesh", 0x0088C500, 0xAA0},
		{"edgeanim", "anim", 0x0088D000, 0x1A9A0},
		// Visual-only effects.
		{"psysblip", "fx", 0x00813600, 0x7980},
		{"psysrip", "fx", 0x0081AF80, 0x8C10},
		{"psysspark", "fx", 0x00831F80, 0x4350},
		{"psysltg", "fx", 0x0084BE80, 0x5E80},
		{"psysdivot", "fx", 0x00851D00, 0x1E30},
		{"fog", "fx", 0x00853B80, 0x2540},
		{"occlusion", "fx", 0x0085FF00, 0x9A0},
		{"deepwater", "fx", 0x0086F400, 0x4940},
		{"dyntexlife", "fx", 0x00873D80, 0x1720},
		{"forestrender", "fx", 0x008BD880, 0x17A0},
		{"forestupdate", "fx", 0x008BF080, 0x1320},
		{"drawblip", "fx", 0x008C0400, 0x6610},
		{"swoosh", "fx", 0x008C9C00, 0x31B0},
		// Gameplay-affecting simulation; never enabled by the test packages.
		{"move", "sim", 0x00860900, 0xEAE0},
		{"traffic", "sim", 0x00856100, 0x9DD0},
		{"physclip", "sim", 0x008B7680, 0x61C0},
		{"pedspn", "sim", 0x008C6A80, 0x3170},
		{"xpgen", "sim", 0x008A7A00, 0xFC40},
	};

	inline bool group_requested(std::string_view list, std::string_view group) noexcept
	{
		while (!list.empty())
		{
			const auto comma = list.find(',');
			if (list.substr(0, comma) == group) return true;
			if (comma == std::string_view::npos) break;
			list.remove_prefix(comma + 1);
		}
		return false;
	}

	inline const std::string& requested_groups()
	{
		static const std::string value = []
		{
#ifdef _MSC_VER
			char buffer[64]{};
			std::size_t length = 0;
			return getenv_s(&length, buffer, sizeof(buffer), "RPCS3_INFAMOUS2_RELAXED") == 0 && length ? std::string(buffer) : std::string();
#else
			const char* env = std::getenv("RPCS3_INFAMOUS2_RELAXED");
			return std::string(env ? env : "");
#endif
		}();
		return value;
	}

	inline bool identity_matches()
	{
		return Emu.GetTitleID() == "BCUS98125" && Emu.GetAppVersion() == "01.04" &&
			Emu.GetExecutableHash() == "PPU-41a7d047c573c78c0307506c9d1b52d15a66e4f5";
	}

	// Every nonzero program word must equal the module word at the same LS offset.
	// Zero words are analyser holes and are not used as evidence.
	inline const module_range* match_module(u32 lower_bound, const u32* data, usz words)
	{
		if (!words || lower_bound < module_ls_base) return nullptr;

		for (const auto& m : modules)
		{
			if (!group_requested(requested_groups(), m.group)) continue;

			const u32 offset = lower_bound - module_ls_base;
			if (offset >= m.size || words * 4 > m.size - offset) continue;
			if (!vm::check_addr(m.va + offset, vm::page_readable, static_cast<u32>(words * 4))) continue;

			const auto* src = vm::_ptr<const u8>(m.va + offset);
			usz nonzero = 0;
			bool same = true;

			for (usz i = 0; i < words; i++)
			{
				if (!data[i]) continue;
				nonzero++;
				if (std::memcmp(&data[i], src + i * 4, 4) != 0) { same = false; break; }
			}

			if (same && nonzero * 2 >= words) return &m;
		}

		return nullptr;
	}

	// Diagnostics only: name the job module (any group) whose code is in LS around pc.
	// Compares the aligned 64-byte block containing pc with the EBOOT copy.
	inline const char* identify_ls(const u8* ls, u32 pc)
	{
		if (!ls || pc < module_ls_base || pc >= 0x40000) return nullptr;

		const u32 block = pc & ~63u;
		const u32 offset = block - module_ls_base;
		for (const auto& m : modules)
		{
			if (offset + 64 > m.size || !vm::check_addr(m.va + offset, vm::page_readable, 64)) continue;
			if (std::memcmp(ls + block, vm::_ptr<const u8>(m.va + offset), 64) == 0) return m.name;
		}

		return nullptr;
	}

	inline xfloat_accuracy select(u32 lower_bound, const u32* data, usz words, xfloat_accuracy configured)
	{
		if (requested_groups().empty() || configured == xfloat_accuracy::relaxed || !identity_matches())
		{
			return configured;
		}

		if (const auto m = match_module(lower_bound, data, words))
		{
			spu_log.notice("INFAMOUS2_SPU_PRECISION: Relaxed for %s (lower_bound=0x%x, words=%u)", m->name, lower_bound, words);
			return xfloat_accuracy::relaxed;
		}

		return configured;
	}
}
