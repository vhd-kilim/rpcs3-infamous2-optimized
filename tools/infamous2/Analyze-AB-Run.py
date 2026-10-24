"""Recorta a janela valida do percurso de uma rodada A/B e resume frametimes e CPU por thread.

Frames: PresentMon v1, TimeInSeconds relativo ao inicio da sessao ETW (started_utc do
frametime-capture-state.json). Threads: amostras de Profile-Rpcs3Threads.py (top 12 por amostra).
"""
import argparse
import csv
import json
import math
from datetime import datetime, timedelta
from pathlib import Path


def load_json(path):
    return json.loads(path.read_text(encoding='utf-8-sig'))


def percentile(values, q):
    ordered = sorted(values)
    index = min(len(ordered) - 1, max(0, math.ceil(q * len(ordered)) - 1))
    return ordered[index]


def frame_stats(run_dir, start, end, skip_head):
    state = load_json(run_dir / 'frametime-capture-state.json')
    origin = datetime.fromisoformat(state['started_utc'])
    lo = (start - origin).total_seconds() + skip_head
    hi = (end - origin).total_seconds()
    intervals, gpu = [], []
    pid = state['process_id']
    with (run_dir / 'presentmon-live.csv').open(newline='', encoding='utf-8', errors='replace') as source:
        for row in csv.DictReader(source):
            try:
                if int(row['ProcessID']) != pid or row['Dropped'] == '1':
                    continue
                t = float(row['TimeInSeconds'])
                if lo <= t <= hi:
                    intervals.append(float(row['msBetweenPresents']))
                    gpu.append(float(row['msGPUActive']))
            except (KeyError, ValueError):
                continue
    if not intervals:
        return {'intervals': 0}
    worst = sorted(intervals, reverse=True)[:max(1, math.ceil(len(intervals) * 0.01))]
    return {
        'intervals': len(intervals),
        'covered_seconds': round(sum(intervals) / 1000, 2),
        'fps_mean': round(1000 * len(intervals) / sum(intervals), 2),
        'one_percent_low_fps': round(1000 / (sum(worst) / len(worst)), 2),
        'p50_ms': round(percentile(intervals, 0.50), 2),
        'p99_ms': round(percentile(intervals, 0.99), 2),
        'p999_ms': round(percentile(intervals, 0.999), 2),
        'max_ms': round(max(intervals), 2),
        'over_50ms': sum(i > 50 for i in intervals),
        'gpu_active_mean_ms': round(sum(gpu) / len(gpu), 2),
    }


def thread_stats(run_dir, start, end):
    report = load_json(run_dir / 'threads.json')
    per_thread, samples = {}, 0
    for sample in report['samples']:
        moment = datetime.fromisoformat(sample['utc'])
        if not (start <= moment <= end):
            continue
        samples += 1
        for record in sample['top_threads']:
            entry = per_thread.setdefault(record['name'] or str(record['thread_id']), [])
            entry.append(record['one_core_percent'])
    # Uma thread fora do top 12 numa amostra conta como 0 nessa amostra.
    ranked = sorted(((name, sum(v) / samples) for name, v in per_thread.items()), key=lambda x: -x[1])
    return {'samples': samples, 'mean_one_core_percent': {name: round(v, 2) for name, v in ranked[:14]}}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('run_dir', type=Path)
    parser.add_argument('--seconds', type=float, default=90)
    parser.add_argument('--skip-head', type=float, default=3, help='descarta o atraso da mensagem no chat')
    args = parser.parse_args()
    session = load_json(args.run_dir / 'session.json')
    windows = [w for w in session['route_windows'] if w.get('valid', True) and not w.get('invalid_reason')]
    if not windows:
        raise SystemExit('Nenhuma janela valida em session.json')
    start = datetime.fromisoformat(windows[-1]['start'])
    end = start + timedelta(seconds=args.seconds)
    result = {
        'variant': session['variant'], 'run': session['run'], 'sha256': session['sha256'],
        'window': {'start': start.isoformat(), 'end': end.isoformat(), 'skip_head_s': args.skip_head},
        'frames': frame_stats(args.run_dir, start, end, args.skip_head),
        'threads': thread_stats(args.run_dir, start, end),
    }
    (args.run_dir / 'ab-summary.json').write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')
    print(json.dumps(result, ensure_ascii=False, indent=2))


if __name__ == '__main__':
    main()
