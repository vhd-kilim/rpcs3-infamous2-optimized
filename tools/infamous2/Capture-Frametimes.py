"""Capture PresentMon stdout and refresh real presentation statistics every 5s."""
from __future__ import annotations

import argparse
from contextlib import contextmanager
import ctypes
from ctypes import wintypes
from datetime import datetime, timezone
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import threading
import time

PRESENTMON = Path(r'C:\Program Files (x86)\RivaTuner Statistics Server\Plugins\Client\PresentMonDataProvider\PresentMon-2.3.1-x64.exe')
ROOT = Path(r'C:\Users\T-GAMER\rpcs3-dev-tools')


@contextmanager
def target_lifetime(pid):
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    kernel.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
    kernel.OpenProcess.restype = wintypes.HANDLE
    kernel.WaitForSingleObject.argtypes = [wintypes.HANDLE, wintypes.DWORD]
    kernel.WaitForSingleObject.restype = wintypes.DWORD
    kernel.CloseHandle.argtypes = [wintypes.HANDLE]
    handle = kernel.OpenProcess(0x00100000, False, pid)  # SYNCHRONIZE only.
    if not handle:
        raise ctypes.WinError(ctypes.get_last_error())
    try:
        yield lambda: kernel.WaitForSingleObject(handle, 0) == 0
    finally:
        kernel.CloseHandle(handle)


def save_json(path, value):
    temporary = path.with_suffix(path.suffix + '.tmp')
    temporary.write_text(json.dumps(value, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    temporary.replace(path)


def shared_writer(path):
    import msvcrt
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    kernel.CreateFileW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD,
                                  ctypes.c_void_p, wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE]
    kernel.CreateFileW.restype = wintypes.HANDLE
    kernel.CloseHandle.argtypes = [wintypes.HANDLE]
    handle = kernel.CreateFileW(str(path), 0x40000000, 7, None, 1, 0x80, None)
    # CREATE_NEW prevents replacing an earlier capture.
    if handle == ctypes.c_void_p(-1).value:
        raise ctypes.WinError(ctypes.get_last_error())
    try:
        descriptor = msvcrt.open_osfhandle(handle, os.O_WRONLY | os.O_BINARY)
    except BaseException:
        kernel.CloseHandle(handle)
        raise
    return os.fdopen(descriptor, 'wb', buffering=0)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pid', type=int, required=True)
    parser.add_argument('--output-directory', type=Path, required=True)
    parser.add_argument('--test-seconds', type=int, default=0)
    args = parser.parse_args()
    output = args.output_directory.resolve()
    if not output.is_relative_to(ROOT / 'monitoring'):
        raise SystemExit('Output must be inside the project monitoring directory.')
    output.mkdir(parents=True, exist_ok=True)
    specification = importlib.util.spec_from_file_location('presentmon_analysis', ROOT / 'Analyze-PresentMon.py')
    analyzer = importlib.util.module_from_spec(specification)
    specification.loader.exec_module(analyzer)
    capture_path = output / 'presentmon-live.csv'
    summary_path = output / 'frametimes-live.json'
    state_path = output / 'frametime-capture-state.json'
    session_name = f'CodexInfamousLive{args.pid}_{os.getpid()}'
    command = [str(PRESENTMON), '--process_id', str(args.pid), '--output_stdout',
               '--v1_metrics', '--no_console_stats', '--session_name', session_name,
               '--terminate_on_proc_exit']
    if args.test_seconds:
        command += ['--timed', str(args.test_seconds), '--terminate_after_timed']
    counters = {'bytes': 0, 'lines': 0, 'last_line_monotonic': None, 'reader_error': None}
    started = datetime.now(timezone.utc).isoformat()
    with target_lifetime(args.pid) as game_exited, shared_writer(capture_path) as target, (output / 'presentmon-live.stderr.log').open('ab', buffering=0) as errors:
        process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=errors,
                                   stdin=subprocess.DEVNULL, creationflags=subprocess.CREATE_NO_WINDOW)

        def consume():
            try:
                for line in iter(process.stdout.readline, b''):
                    target.write(line)
                    counters['bytes'] += len(line)
                    counters['lines'] += 1
                    counters['last_line_monotonic'] = time.monotonic()
            except OSError as error:
                counters['reader_error'] = str(error)

        reader = threading.Thread(target=consume, daemon=True)
        reader.start()
        try:
            while True:
                if game_exited() and process.poll() is None:
                    # PresentMon cannot always observe process exit without elevation.
                    subprocess.run([str(PRESENTMON), '--session_name', session_name,
                                    '--terminate_existing_session'], stdout=subprocess.DEVNULL,
                                   stderr=errors, timeout=10, creationflags=subprocess.CREATE_NO_WINDOW)
                    process.wait(timeout=10)
                finished = process.poll() is not None
                if finished:
                    reader.join(timeout=3)
                since_line = (time.monotonic() - counters['last_line_monotonic']
                              if counters['last_line_monotonic'] else None)
                state = ('stopped' if finished else 'running' if since_line is not None and since_line < 15
                         else 'waiting_for_frames')
                save_json(state_path, {'state': state, 'process_id': args.pid,
                          'presentmon_process_id': process.pid, 'session_name': session_name,
                          'started_utc': started, 'updated_utc': datetime.now(timezone.utc).isoformat(),
                          'seconds_since_output': since_line, 'exit_code': process.returncode,
                          'csv': str(capture_path), **{k: v for k, v in counters.items() if k != 'last_line_monotonic'}})
                try:
                    report = analyzer.analyze(capture_path, args.pid)
                except (OSError, ValueError) as error:
                    report = {'status': 'error', 'error': str(error)}
                report['capture_state'] = state
                save_json(summary_path, report)
                if finished:
                    break
                time.sleep(5)
        finally:
            if process.poll() is None:
                subprocess.run([str(PRESENTMON), '--session_name', session_name,
                                '--terminate_existing_session'], stdout=subprocess.DEVNULL,
                               stderr=errors, timeout=10, creationflags=subprocess.CREATE_NO_WINDOW)
                process.wait(timeout=10)
            reader.join(timeout=3)


if __name__ == '__main__':
    main()
