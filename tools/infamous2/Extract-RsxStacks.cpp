// Offline ETL reader for RSX-thread CPU samples with their stacks. Does not touch the game process.
// Extends Extract-RsxCpu.cpp: for every sample of one thread inside [lo, hi], collects the StackWalk
// stacks (kernel and user parts share the sample timestamp) and reports
//   <out>.inclusive.csv  samples whose stack contains each rpcs3.exe function (inclusive)
//   <out>.callers.csv    for samples outside rpcs3.exe (kernel, ntdll, driver...), the first rpcs3.exe frame
//   <out>.edges.csv      for those samples, leaf module -> first rpcs3.exe frame -> its rpcs3.exe caller
// Payload layouts: Microsoft's KernelTraceEventParser SampledProfile/ImageLoad/StackWalk.
#define NOMINMAX
#include <windows.h>
#include <evntrace.h>
#include <evntcons.h>
#include <dbghelp.h>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

const GUID perf_guid{0xce1dbfb4,0x137e,0x4da6,{0x87,0xb0,0x3f,0x59,0xaa,0x10,0x2c,0xbc}};
const GUID image_guid{0x2cb15d1d,0x5fc1,0x11d2,{0xab,0xe1,0x00,0xa0,0xc9,0x11,0xf5,0x18}};
const GUID stack_guid{0xdef2fe46,0x7bd6,0x4b80,{0xbd,0x94,0xf5,0x7f,0xe2,0x0d,0x0c,0xe3}};

uint32_t pid, tid;
uint64_t lo, hi, events = 0, samples = 0, nonprocess = 0, stack_events = 0, stack_matched = 0;
// Pass 1 (FILETIME stamps) and pass 2 (raw QPC stamps, as in StackWalk payloads) see the same
// thread samples in the same order; the k-th sample of pass 2 takes its window test from pass 1.
bool raw_pass = false;
std::vector<uint64_t> filetime_stamps;
size_t raw_index = 0;
std::map<int, uint64_t> stack_opcodes;

struct image { uint64_t base, size; std::wstring path; };
std::vector<image> images;

struct sample { uint64_t ip = 0; std::vector<uint64_t> frames; };
std::unordered_map<uint64_t, sample> by_stamp;

template <class T> T read(const unsigned char* p) { T v; std::memcpy(&v, p, sizeof(v)); return v; }

void WINAPI record(EVENT_RECORD* e)
{
	++events;
	const auto* b = static_cast<const unsigned char*>(e->UserData);
	const size_t n = e->UserDataLength;
	const size_t ptr = (e->EventHeader.Flags & EVENT_HEADER_FLAG_32_BIT_HEADER) ? 4 : 8;

	if (raw_pass && IsEqualGUID(e->EventHeader.ProviderId, image_guid) && e->EventHeader.EventDescriptor.Version >= 1)
	{
		if (n < ptr * 2 + 4 || read<uint32_t>(b + ptr * 2) != pid) return;
		const size_t nameoff = e->EventHeader.EventDescriptor.Version >= 2 ? 44 + 3 * (ptr - 4) : 12 + 2 * (ptr - 4);
		if (nameoff >= n || (n - nameoff) % 2) return;
		std::wstring path;
		for (size_t o = nameoff; o + 2 <= n; o += 2) { wchar_t c = read<uint16_t>(b + o); if (!c) break; path += c; }
		const uint64_t base = ptr == 8 ? read<uint64_t>(b) : read<uint32_t>(b);
		const uint64_t size = ptr == 8 ? read<uint64_t>(b + ptr) : read<uint32_t>(b + ptr);
		if (std::none_of(images.begin(), images.end(), [&](const image& i) { return i.base == base && i.path == path; })) images.push_back({base, size, path});
		return;
	}

	if (raw_pass && IsEqualGUID(e->EventHeader.ProviderId, stack_guid))
	{
		++stack_events;
		++stack_opcodes[e->EventHeader.EventDescriptor.Opcode];
		// StackWalk_Event: EventTimeStamp u64, StackProcess u32, StackThread u32, Stack[] pointers.
		if (e->EventHeader.EventDescriptor.Opcode != 32 || n < 16) return;
		if (read<uint32_t>(b + 12) != tid) return;
		const auto it = by_stamp.find(read<uint64_t>(b));
		if (it == by_stamp.end()) return;
		++stack_matched;
		for (size_t o = 16; o + ptr <= n; o += ptr) it->second.frames.push_back(ptr == 8 ? read<uint64_t>(b + o) : read<uint32_t>(b + o));
		return;
	}

	if (!IsEqualGUID(e->EventHeader.ProviderId, perf_guid) || e->EventHeader.EventDescriptor.Opcode != 46) return;
	if (n < ptr + 8) return;
	if (read<uint32_t>(b + ptr) != tid) return;
	const auto stamp = static_cast<uint64_t>(e->EventHeader.TimeStamp.QuadPart);
	if (!raw_pass)
	{
		filetime_stamps.push_back(stamp);
		return;
	}
	if (raw_index >= filetime_stamps.size()) return;
	const auto filetime = filetime_stamps[raw_index++];
	if (filetime < lo || filetime > hi) return;
	if (b[ptr + 6] & 3) { ++nonprocess; return; } // DPC/ISR sample is not charged to the game.
	++samples;
	by_stamp[stamp].ip = ptr == 8 ? read<uint64_t>(b) : read<uint32_t>(b);
}

std::string quote(std::string s) { size_t at = 0; while ((at = s.find('"', at)) != std::string::npos) { s.insert(at, "\""); at += 2; } return '"' + s + '"'; }

int wmain(int argc, wchar_t** argv)
{
	if (argc != 8) { std::cerr << "usage: reader trace.etl pid tid lo_FILETIME hi_FILETIME exe output_prefix\n"; return 2; }
	pid = std::stoul(argv[2]); tid = std::stoul(argv[3]); lo = std::stoull(argv[4]); hi = std::stoull(argv[5]);

	EVENT_TRACE_LOGFILEW logfile{};
	for (const bool raw : {false, true})
	{
		raw_pass = raw;
		logfile = {};
		logfile.LogFileName = argv[1];
		logfile.ProcessTraceMode = PROCESS_TRACE_MODE_EVENT_RECORD | (raw ? PROCESS_TRACE_MODE_RAW_TIMESTAMP : 0);
		logfile.EventRecordCallback = record;
		auto trace = OpenTraceW(&logfile);
		if (trace == INVALID_PROCESSTRACE_HANDLE) { std::cerr << "OpenTrace " << GetLastError(); return 3; }
		const auto status = ProcessTrace(&trace, 1, nullptr, nullptr);
		CloseTrace(trace);
		if (status != ERROR_SUCCESS) { std::cerr << "ProcessTrace " << status; return 4; }
	}
	if (raw_index != filetime_stamps.size()) { std::cerr << "pass mismatch " << raw_index << " != " << filetime_stamps.size() << "\n"; return 10; }

	std::wcerr << L"events=" << events << L" samples=" << samples << L" ISR_DPC=" << nonprocess << L" stack_events=" << stack_events
		<< L" stack_matched=" << stack_matched << L" lost=" << logfile.LogfileHeader.EventsLost << L"\n";
	for (auto [op, count] : stack_opcodes) std::wcerr << L"stack opcode " << op << L": " << count << L"\n";

	const auto match = std::find_if(images.begin(), images.end(), [](const image& i) { auto p = i.path; std::transform(p.begin(), p.end(), p.begin(), towlower); return p.ends_with(L"\\rpcs3.exe"); });
	if (match == images.end()) { std::cerr << "rpcs3.exe image not found\n"; return 5; }

	const auto proc = GetCurrentProcess();
	const std::wstring exe = argv[6], symbolpath = exe.substr(0, exe.find_last_of(L"\\"));
	SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_EXACT_SYMBOLS | SYMOPT_FAIL_CRITICAL_ERRORS);
	if (!SymInitializeW(proc, symbolpath.c_str(), FALSE)) { std::cerr << "SymInitialize " << GetLastError(); return 6; }
	if (!SymLoadModuleExW(proc, nullptr, exe.c_str(), nullptr, match->base, static_cast<DWORD>(match->size), nullptr, 0)) { std::cerr << "SymLoadModule " << GetLastError(); return 7; }
	alignas(SYMBOL_INFO) char probe_buffer[sizeof(SYMBOL_INFO) + MAX_SYM_NAME]{};
	auto* probe = reinterpret_cast<SYMBOL_INFO*>(probe_buffer);
	probe->SizeOfStruct = sizeof(SYMBOL_INFO); probe->MaxNameLen = MAX_SYM_NAME;
	DWORD64 probe_displacement = 0;
	SymFromAddr(proc, match->base + 0x1000, &probe_displacement, probe);
	IMAGEHLP_MODULE64 mod{}; mod.SizeOfStruct = sizeof(mod);
	SymGetModuleInfo64(proc, match->base, &mod);
	if (mod.PdbUnmatched || mod.SymType != SymPdb) { std::cerr << "Exact PDB required\n"; return 8; }

	const auto in_exe = [&](uint64_t a) { return a >= match->base && a < match->base + match->size; };
	std::unordered_map<uint64_t, std::string> names;
	const auto symbol = [&](uint64_t a) -> const std::string&
	{
		auto [it, inserted] = names.try_emplace(a);
		if (inserted)
		{
			alignas(SYMBOL_INFO) char buffer[sizeof(SYMBOL_INFO) + MAX_SYM_NAME]{};
			auto* sym = reinterpret_cast<SYMBOL_INFO*>(buffer);
			sym->SizeOfStruct = sizeof(SYMBOL_INFO); sym->MaxNameLen = MAX_SYM_NAME;
			DWORD64 displacement = 0;
			it->second = SymFromAddr(proc, a, &displacement, sym) ? sym->Name : "[unresolved EXE]";
		}
		return it->second;
	};
	const auto module_of = [&](uint64_t a) -> std::string
	{
		if (a >= 0xFFFF800000000000ull) return "[kernel]";
		if (in_exe(a)) return "rpcs3.exe";
		const auto other = std::find_if(images.begin(), images.end(), [&](const image& i) { return a >= i.base && a < i.base + i.size; });
		if (other == images.end()) return "[untracked]";
		const auto name = other->path.substr(other->path.find_last_of(L"\\") + 1);
		return std::string(name.begin(), name.end());
	};

	std::map<std::string, uint64_t> inclusive, callers, edges;
	uint64_t with_stack = 0, outside = 0, outside_attributed = 0;
	for (auto& [stamp, s] : by_stamp)
	{
		if (s.frames.empty()) continue;
		++with_stack;
		// Frames are leaf first. Kernel part (if any) precedes the user part.
		std::set<std::string> seen;
		for (const auto f : s.frames)
		{
			if (in_exe(f) && seen.insert(symbol(f)).second) ++inclusive[symbol(f)];
		}

		if (in_exe(s.ip)) continue;
		++outside;
		const std::string leaf = module_of(s.ip);
		for (size_t i = 0; i < s.frames.size(); ++i)
		{
			if (!in_exe(s.frames[i])) continue;
			++outside_attributed;
			++callers[leaf + " <- " + symbol(s.frames[i])];
			std::string parent = "[root]";
			for (size_t j = i + 1; j < s.frames.size(); ++j)
			{
				if (in_exe(s.frames[j]) && symbol(s.frames[j]) != symbol(s.frames[i])) { parent = symbol(s.frames[j]); break; }
			}
			++edges[leaf + " <- " + symbol(s.frames[i]) + " <- " + parent];
			break;
		}
	}

	const auto dump = [&](const std::map<std::string, uint64_t>& m, const std::wstring& suffix, const char* header, uint64_t denominator)
	{
		std::vector<std::pair<std::string, uint64_t>> v(m.begin(), m.end());
		std::sort(v.begin(), v.end(), [](auto& a, auto& b) { return a.second > b.second; });
		std::ofstream out(std::wstring(argv[7]) + suffix);
		out << header << ",samples,percent_all_rsx_samples\n";
		for (auto& [k, c] : v) out << quote(k) << ',' << c << ',' << (100.0 * c / std::max<uint64_t>(denominator, 1)) << '\n';
	};
	dump(inclusive, L".inclusive.csv", "function", samples);
	dump(callers, L".callers.csv", "leaf_module_and_first_exe_frame", samples);
	dump(edges, L".edges.csv", "leaf_module_first_exe_frame_and_caller", samples);
	SymCleanup(proc);
	std::cerr << "with_stack=" << with_stack << " outside_exe=" << outside << " outside_attributed=" << outside_attributed << "\n";
	return samples && with_stack ? 0 : 9;
}
