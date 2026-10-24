"""Read thread CPU times and Windows thread descriptions without injecting code."""
import argparse
import ctypes as c
from ctypes import wintypes as w
from datetime import datetime, timezone
import json
from pathlib import Path
import time

kernel = c.WinDLL('kernel32', use_last_error=True)
class Entry(c.Structure):
    _fields_ = [('size',w.DWORD),('usage',w.DWORD),('tid',w.DWORD),('pid',w.DWORD),
                ('priority',w.LONG),('delta',w.LONG),('flags',w.DWORD)]
kernel.CreateToolhelp32Snapshot.argtypes=[w.DWORD,w.DWORD];kernel.CreateToolhelp32Snapshot.restype=w.HANDLE
kernel.Thread32First.argtypes=[w.HANDLE,c.POINTER(Entry)];kernel.Thread32Next.argtypes=[w.HANDLE,c.POINTER(Entry)]
kernel.OpenThread.argtypes=[w.DWORD,w.BOOL,w.DWORD];kernel.OpenThread.restype=w.HANDLE
kernel.GetThreadTimes.argtypes=[w.HANDLE,c.POINTER(w.FILETIME),c.POINTER(w.FILETIME),c.POINTER(w.FILETIME),c.POINTER(w.FILETIME)]
kernel.GetThreadDescription.argtypes=[w.HANDLE,c.POINTER(c.c_void_p)];kernel.GetThreadDescription.restype=w.LONG
kernel.LocalFree.argtypes=[c.c_void_p];kernel.LocalFree.restype=c.c_void_p
kernel.CloseHandle.argtypes=[w.HANDLE]

def scan(pid):
    result={}
    snapshot=kernel.CreateToolhelp32Snapshot(4,0)
    if snapshot==c.c_void_p(-1).value: raise c.WinError(c.get_last_error())
    entry=Entry();entry.size=c.sizeof(entry)
    try:
        valid=kernel.Thread32First(snapshot,c.byref(entry))
        while valid:
            if entry.pid==pid:
                handle=kernel.OpenThread(0x0800,False,entry.tid)
                if handle:
                    try:
                        created=w.FILETIME();ended=w.FILETIME();system=w.FILETIME();user=w.FILETIME()
                        if kernel.GetThreadTimes(handle,c.byref(created),c.byref(ended),c.byref(system),c.byref(user)):
                            name_pointer=c.c_void_p()
                            name=''
                            if kernel.GetThreadDescription(handle,c.byref(name_pointer))>=0 and name_pointer.value:
                                name=c.wstring_at(name_pointer);kernel.LocalFree(name_pointer)
                            ticks=((system.dwHighDateTime<<32)+system.dwLowDateTime+
                                   (user.dwHighDateTime<<32)+user.dwLowDateTime)
                            result[entry.tid]=(ticks/1e7,name)
                    finally: kernel.CloseHandle(handle)
            valid=kernel.Thread32Next(snapshot,c.byref(entry))
    finally: kernel.CloseHandle(snapshot)
    return result

def group(name):
    upper=name.upper()
    if upper.startswith('PPU['): return 'PPU emulation'
    if 'SPUW' in upper or 'SPU LLVM' in upper: return 'SPU compilation'
    if 'SPU' in upper or 'CELLSPURS' in upper: return 'SPU emulation'
    if 'RSX' in upper: return 'RSX graphics'
    if 'PPU' in upper: return 'PPU emulation'
    return 'Other'

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pid',type=int,required=True);parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--seconds',type=int,default=120);parser.add_argument('--interval',type=float,default=2)
    args=parser.parse_args();previous=scan(args.pid);last=time.monotonic();begin=last;totals={};samples=[]
    while time.monotonic()-begin<args.seconds:
        time.sleep(args.interval);now=time.monotonic();current=scan(args.pid);elapsed=now-last
        if not current: break
        values=[];groups={}
        for tid,(cpu,name) in current.items():
            if tid not in previous: continue
            delta=max(0,cpu-previous[tid][0]);percent=100*delta/elapsed
            record=totals.setdefault(tid,{'thread_id':tid,'name':name,'group':group(name),'cpu_seconds':0,'sampled_seconds':0,'peak_one_core_percent':0})
            record['name']=name;record['cpu_seconds']+=delta;record['sampled_seconds']+=elapsed
            record['peak_one_core_percent']=max(record['peak_one_core_percent'],percent)
            values.append({'thread_id':tid,'name':name,'one_core_percent':round(percent,2),'group':group(name)})
            groups[group(name)]=groups.get(group(name),0)+percent
        samples.append({'utc':datetime.now(timezone.utc).isoformat(),'top_threads':sorted(values,key=lambda r:r['one_core_percent'],reverse=True)[:12],'group_core_equivalents':{k:round(v/100,3) for k,v in groups.items()}})
        ranked=[]
        for record in totals.values():
            ranked.append({**record,'mean_one_core_percent':100*record['cpu_seconds']/max(.001,record['sampled_seconds'])})
        report={'pid':args.pid,'updated_utc':datetime.now(timezone.utc).isoformat(),'elapsed_seconds':now-begin,'method':'GetThreadTimes and GetThreadDescription; 100 percent means one logical core fully occupied.','top_threads':sorted(ranked,key=lambda r:r['cpu_seconds'],reverse=True)[:20],'samples':samples}
        temporary=args.output.with_suffix('.tmp');temporary.write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8');temporary.replace(args.output)
        previous=current;last=now

if __name__=='__main__': main()
