// Offline ETL sampled-IP reader. Does not open, suspend, inject into or debug the game.
// Payload layouts: Microsoft's KernelTraceEventParser SampledProfileTraceData/ImageLoadTraceData.
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
#include <string>
#include <vector>

const GUID perf_guid{0xce1dbfb4,0x137e,0x4da6,{0x87,0xb0,0x3f,0x59,0xaa,0x10,0x2c,0xbc}};
const GUID image_guid{0x2cb15d1d,0x5fc1,0x11d2,{0xab,0xe1,0x00,0xa0,0xc9,0x11,0xf5,0x18}};
uint32_t pid, tid; uint64_t lo, hi, events=0, samples=0, nonprocess=0, malformed=0;
struct image { uint64_t base,size; std::wstring path; };
std::vector<image> images;
std::map<uint64_t,uint64_t> ips;
template<class T> T read(const unsigned char* p) { T v; std::memcpy(&v,p,sizeof(v)); return v; }
void WINAPI record(EVENT_RECORD* e) {
    ++events;
    const auto* b=static_cast<const unsigned char*>(e->UserData);
    const size_t n=e->UserDataLength;
    const size_t ptr=(e->EventHeader.Flags & EVENT_HEADER_FLAG_32_BIT_HEADER)?4:8;
    if (IsEqualGUID(e->EventHeader.ProviderId,image_guid) && e->EventHeader.EventDescriptor.Version>=1) {
        if (n<ptr*2+4 || read<uint32_t>(b+ptr*2)!=pid) return;
        size_t nameoff=e->EventHeader.EventDescriptor.Version>=2 ? 44+3*(ptr-4) : 12+2*(ptr-4);
        if (nameoff>=n || (n-nameoff)%2) return;
        std::wstring path; for(size_t o=nameoff;o+2<=n;o+=2) { wchar_t c=read<uint16_t>(b+o); if(!c)break; path+=c; }
        const uint64_t base=ptr==8?read<uint64_t>(b):read<uint32_t>(b);
        const uint64_t size=ptr==8?read<uint64_t>(b+ptr):read<uint32_t>(b+ptr);
        if (std::none_of(images.begin(),images.end(),[&](const image& i){return i.base==base && i.path==path;})) images.push_back({base,size,path});
        return;
    }
    if (!IsEqualGUID(e->EventHeader.ProviderId,perf_guid) || e->EventHeader.EventDescriptor.Opcode!=46) return;
    if (n<ptr+8) {++malformed;return;}
    if (read<uint32_t>(b+ptr)!=tid) return;
    const auto stamp=static_cast<uint64_t>(e->EventHeader.TimeStamp.QuadPart);
    if(stamp<lo || stamp>hi) return;
    if (b[ptr+6]&3) {++nonprocess;return;} // DPC/ISR sample is not charged to the game.
    ++samples;
    ++ips[ptr==8?read<uint64_t>(b):read<uint32_t>(b)];
}
std::string quote(std::string s) {size_t at=0;while((at=s.find('"',at))!=std::string::npos){s.insert(at,"\"");at+=2;}return '"'+s+'"';}
int wmain(int argc,wchar_t** argv) {
    if(argc!=8){std::cerr<<"usage: reader trace.etl pid tid lo_FILETIME hi_FILETIME exe output.csv\n";return 2;}
    pid=std::stoul(argv[2]);tid=std::stoul(argv[3]);lo=std::stoull(argv[4]);hi=std::stoull(argv[5]);
    EVENT_TRACE_LOGFILEW logfile{};logfile.LogFileName=argv[1];logfile.ProcessTraceMode=PROCESS_TRACE_MODE_EVENT_RECORD;logfile.EventRecordCallback=record;
    auto trace=OpenTraceW(&logfile);if(trace==INVALID_PROCESSTRACE_HANDLE){std::cerr<<"OpenTrace "<<GetLastError();return 3;}
    auto status=ProcessTrace(&trace,1,nullptr,nullptr);CloseTrace(trace);if(status!=ERROR_SUCCESS){std::cerr<<"ProcessTrace "<<status;return 4;}
    std::wcerr<<L"events="<<events<<L" samples="<<samples<<L" ISR_DPC="<<nonprocess<<L" malformed="<<malformed<<L" lost="<<logfile.LogfileHeader.EventsLost<<L" modules="<<images.size()<<L"\n";
    const auto match=std::find_if(images.begin(),images.end(),[](const image& i){auto p=i.path;std::transform(p.begin(),p.end(),p.begin(),towlower);return p.ends_with(L"\\rpcs3.exe");});
    if(match==images.end()){for(auto& m:images)std::wcerr<<m.path<<L"\n";return 5;}
    std::wcerr<<L"image_base="<<std::hex<<match->base<<L" image_size="<<match->size<<std::dec<<L"\n";
    const auto proc=GetCurrentProcess();std::wstring exe=argv[6],symbolpath=exe.substr(0,exe.find_last_of(L"\\"));
    SymSetOptions(SYMOPT_UNDNAME|SYMOPT_DEFERRED_LOADS|SYMOPT_LOAD_LINES|SYMOPT_EXACT_SYMBOLS|SYMOPT_FAIL_CRITICAL_ERRORS);
    if(!SymInitializeW(proc,symbolpath.c_str(),FALSE)){std::cerr<<"SymInitialize "<<GetLastError();return 6;}
    if(!SymLoadModuleExW(proc,nullptr,argv[6],nullptr,match->base,static_cast<DWORD>(match->size),nullptr,0)){std::cerr<<"SymLoadModule "<<GetLastError();return 7;}
    // Force the deferred load before checking that the exact private PDB was found.
    alignas(SYMBOL_INFO) char probe_buffer[sizeof(SYMBOL_INFO)+MAX_SYM_NAME]{};
    auto* probe=reinterpret_cast<SYMBOL_INFO*>(probe_buffer);probe->SizeOfStruct=sizeof(SYMBOL_INFO);probe->MaxNameLen=MAX_SYM_NAME;
    DWORD64 probe_displacement=0;SymFromAddr(proc,match->base+0x1000,&probe_displacement,probe);
    IMAGEHLP_MODULE64 mod{};mod.SizeOfStruct=sizeof(mod);SymGetModuleInfo64(proc,match->base,&mod);
    std::cerr<<"symbols_type="<<mod.SymType<<" unmatched="<<mod.PdbUnmatched<<" pdb="<<mod.LoadedPdbName<<"\n";
    if(mod.PdbUnmatched || mod.SymType!=SymPdb){std::cerr<<"Exact PDB required\n";return 8;}
    struct row {uint64_t count=0;std::string file;DWORD line=0;}; std::map<std::string,row> totals;
    uint64_t outside=0,unresolved=0;
    std::wstring ip_path=std::wstring(argv[7])+L".ips.csv";
    std::ofstream ipout(ip_path);ipout<<"address,module,function,samples,file,line\n";
    for(auto [ip,count]:ips) {
        if(ip<match->base || ip>=match->base+match->size){
            outside+=count;
            auto other=std::find_if(images.begin(),images.end(),[&](const image& i){return ip>=i.base && ip<i.base+i.size;});
            std::string module="[kernel or untracked]";
            if(other!=images.end()){auto name=other->path.substr(other->path.find_last_of(L"\\")+1);module.assign(name.begin(),name.end());}
            totals["[outside EXE: "+module+"]"].count+=count;
            ipout<<"0x"<<std::hex<<ip<<std::dec<<','<<quote(module)<<",,"<<count<<",,\n";
            continue;
        }
        alignas(SYMBOL_INFO) char buffer[sizeof(SYMBOL_INFO)+MAX_SYM_NAME]{};auto* sym=reinterpret_cast<SYMBOL_INFO*>(buffer);sym->SizeOfStruct=sizeof(SYMBOL_INFO);sym->MaxNameLen=MAX_SYM_NAME;
        DWORD64 displacement=0;std::string name;
        if(SymFromAddr(proc,ip,&displacement,sym))name=sym->Name;else{name="[unresolved EXE]";unresolved+=count;}
        auto& r=totals[name];r.count+=count;
        IMAGEHLP_LINE64 line{};line.SizeOfStruct=sizeof(line);DWORD d=0;
        const bool has_line=SymGetLineFromAddr64(proc,ip,&d,&line);
        if(r.file.empty() && has_line){r.file=line.FileName;r.line=line.LineNumber;}
        ipout<<"0x"<<std::hex<<ip<<std::dec<<",rpcs3.exe,"<<quote(name)<<','<<count<<','<<quote(has_line?line.FileName:"")<<','<<(has_line?line.LineNumber:0)<<'\n';
    }
    std::vector<std::pair<std::string,row>> sorted(totals.begin(),totals.end());std::sort(sorted.begin(),sorted.end(),[](auto&a,auto&b){return a.second.count>b.second.count;});
    std::ofstream out(argv[7]);out<<"function,samples,percent_all_rsx_samples,representative_file,representative_line\n";
    for(auto& [name,r]:sorted)out<<quote(name)<<','<<r.count<<','<<(100.0*r.count/std::max<uint64_t>(samples,1))<<','<<quote(r.file)<<','<<r.line<<'\n';
    SymCleanup(proc);std::cerr<<"outside="<<outside<<" unresolved="<<unresolved<<"\n";
    return out && samples ? 0:9;
}
