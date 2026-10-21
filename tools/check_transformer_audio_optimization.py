"""Verify a fresh full export against preserved v1 artifacts; standard library only."""
import csv
import hashlib
import json
import sys
from pathlib import Path


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def check(fresh, baseline):
    rows = list(csv.DictReader((fresh / 'comparison.csv').open()))
    assert len(rows) == 108
    assert all(r['pass'] == '1' and r['converged'] == '1' and r['solver_failures'] == '0' for r in rows)
    assert all(float(r['maximum_scaled_equation_residual']) <= 1e-13 for r in rows)
    csv_names = ('comparison.csv', 'convergence.csv', 'impulse.csv', 'tones.csv', 'listening.csv')
    files = list(csv_names) + sorted(p.name for p in baseline.glob('*.wav'))
    assert len(files) == 26
    comparisons = {name: {'baseline_sha256': digest(baseline / name), 'fresh_sha256': digest(fresh / name)}
                   for name in files}
    assert all(v['baseline_sha256'] == v['fresh_sha256'] for v in comparisons.values()), comparisons
    result = {
        'cases': len(rows),
        'passed': sum(int(r['pass']) for r in rows),
        'converged': sum(int(r['converged']) for r in rows),
        'solver_failures': sum(int(r['solver_failures']) for r in rows),
        'maximum_scaled_equation_residual': max(float(r['maximum_scaled_equation_residual']) for r in rows),
        'maximum_solver_iterations': max(int(r['maximum_solver_iterations']) for r in rows),
        'worst_band_residual_dbr': max(float(r['band_residual_dbr']) for r in rows),
        'reference_factors': sorted({int(r['reference_factor']) for r in rows}),
        'latency_base_samples': sorted({float(r['latency_samples']) for r in rows}),
        'identical_csvs': len(csv_names),
        'identical_wavs': len(files) - len(csv_names),
        'comparison_scope': 'Full file bytes, including every float sample; timing CSV intentionally excluded',
        'files': comparisons,
    }
    (fresh / 'equivalence.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps({k: v for k, v in result.items() if k != 'files'}, indent=2))


if __name__ == '__main__':
    check(Path(sys.argv[1]), Path(sys.argv[2]))
