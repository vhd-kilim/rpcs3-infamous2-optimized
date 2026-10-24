"""Soma as janelas RSX_DIAG do percurso e detalha a espera do RSX por endereco de semaforo NV406E.

O tempo do log e relativo ao inicio do processo; o zero vem de process_started_utc (status.json).
As duracoes sao tempo de parede da thread RSX, nao tempo de CPU nem de GPU.
"""
import argparse
import json
import re
from datetime import datetime, timedelta
from pathlib import Path

LINE = re.compile(r'^\S*\s*(\d+):(\d+):(\d+\.\d+)\s.*?RSX_DIAG (\{.*\})\s*$')


def load_json(path):
    return json.loads(path.read_text(encoding='utf-8-sig'))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('run_dir', type=Path)
    parser.add_argument('--log', type=Path, help='copia do RPCS3.log; padrao: run_dir/RPCS3.log')
    parser.add_argument('--seconds', type=float, default=90)
    parser.add_argument('--skip-head', type=float, default=3)
    args = parser.parse_args()
    session = load_json(args.run_dir / 'session.json')
    status = load_json(args.run_dir / 'status.json')
    windows = [w for w in session['route_windows'] if w.get('valid', True) and not w.get('invalid_reason')]
    start = datetime.fromisoformat(windows[-1]['start']) + timedelta(seconds=args.skip_head)
    end = datetime.fromisoformat(windows[-1]['start']) + timedelta(seconds=args.seconds)
    origin = datetime.fromisoformat(status['process_started_utc'])
    lo, hi = (start - origin).total_seconds(), (end - origin).total_seconds()

    wall = flips = sem_incl = sem_calls = 0
    fifo = [0] * 6
    per = {}
    overflow = [0, 0, 0, 0]
    used = 0
    for line in (args.log or args.run_dir / 'RPCS3.log').open(encoding='utf-8', errors='replace'):
        m = LINE.match(line)
        if not m:
            continue
        t = int(m.group(1)) * 3600 + int(m.group(2)) * 60 + float(m.group(3))
        d = json.loads(m.group(4))
        # A janela publicada termina em t e comeca em t - wall; usa so as inteiramente dentro.
        if t - d['wall_us'] / 1e6 < lo or t > hi:
            continue
        used += 1
        wall += d['wall_us']
        flips += d['flips']
        sem_calls += d['semaphore'][0]
        sem_incl += d['semaphore'][1]
        fifo = [a + b for a, b in zip(fifo, d['fifo_us'])]
        for addr, acq, waits, wait_us, max_us in d.get('sem', []):
            e = per.setdefault(addr, [0, 0, 0, 0])
            e[0] += acq; e[1] += waits; e[2] += wait_us; e[3] = max(e[3], max_us)
        overflow = [a + b for a, b in zip(overflow[:3], d.get('sem_overflow', [0, 0, 0, 0])[:3])] + [overflow[3]]
    if not used:
        raise SystemExit('Nenhuma janela RSX_DIAG dentro do percurso.')
    secs = wall / 1e6
    rows = sorted(per.items(), key=lambda kv: -kv[1][2])
    result = {
        'windows_used': used, 'wall_s': round(secs, 2), 'flips': flips, 'fps_from_flips': round(flips / secs, 2),
        'semaphore_wait_total_ms': round(sem_incl / 1000, 1), 'semaphore_wait_pct_wall': round(100 * sem_incl / wall, 2),
        'semaphore_waits': sem_calls,
        'fifo_us_pct': [round(100 * f / wall, 2) for f in fifo],
        'per_address': [{
            'address': f'0x{a:08X}', 'acquires_per_frame': round(v[0] / max(1, flips), 2), 'waits': v[1],
            'wait_pct_of_frames': round(100 * v[1] / max(1, flips), 1), 'wait_total_ms': round(v[2] / 1000, 1),
            'wait_pct_wall': round(100 * v[2] / wall, 2), 'wait_mean_us': round(v[2] / v[1], 1) if v[1] else 0,
            'wait_ms_per_frame': round(v[2] / 1000 / max(1, flips), 3), 'max_us': v[3]} for a, v in rows],
        'overflow': overflow,
    }
    (args.run_dir / 'rsx-semaphores.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
