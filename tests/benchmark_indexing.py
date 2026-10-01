"""Compare local portable builds on fresh libraries; never touch a user library."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import sqlite3
import statistics
import subprocess


def output_digest(database):
    digest = hashlib.sha256()
    with sqlite3.connect(database.as_uri() + '?mode=ro', uri=True) as connection:
        if connection.execute('pragma integrity_check').fetchone()[0] != 'ok':
            raise RuntimeError('Benchmark database failed its integrity check')
        rows = connection.execute(
            'select p.name,p.embedding,p.model,p.tags,p.ocr,a.descriptor '
            'from photos p join appearances a on a.photo_id=p.id order by p.path')
        for row in rows:
            for value in row:
                data = value if isinstance(value, bytes) else str(value).encode('utf-8')
                digest.update(len(data).to_bytes(8, 'little'))
                digest.update(data)
    return digest.hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline', required=True, type=Path)
    parser.add_argument('--candidate', required=True, type=Path)
    parser.add_argument('--images', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path,
                        help='New directory for isolated libraries and reports')
    parser.add_argument('--repeats', type=int, default=3)
    parser.add_argument('--require-identical', action='store_true',
                        help='Fail if embeddings, descriptors, labels or OCR differ')
    args = parser.parse_args()
    if args.repeats < 1:
        parser.error('--repeats must be positive')
    if not args.images.is_dir() or not args.baseline.is_file() or not args.candidate.is_file():
        parser.error('Both executables and the fixture directory must exist')
    args.output = args.output.resolve()
    args.output.mkdir(parents=True, exist_ok=False)
    runs = []
    expected = None
    for pair in range(args.repeats):
        # Reverse paired order to reduce systematic warm-cache bias.
        order = ['baseline', 'candidate'] if pair % 2 == 0 else ['candidate', 'baseline']
        for name in order:
            directory = args.output / f'{pair + 1}-{name}'
            report_path = args.output / f'{pair + 1}-{name}.json'
            subprocess.run([
                str(getattr(args, name).resolve()), '--headless', '--data-dir', str(directory),
                '--index', str(args.images.resolve()), '--report', str(report_path)
            ], check=True, creationflags=subprocess.CREATE_NO_WINDOW if os.name == 'nt' else 0)
            report = json.loads(report_path.read_text(encoding='utf-8'))
            index = report['index']
            if index['failed'] or index['cancelled'] or not index['changed']:
                raise RuntimeError(f'Incomplete indexing: {report_path}')
            digest = output_digest(directory / 'library.sqlite')
            signature = (index['changed'], digest)
            if expected is None:
                expected = signature
            if args.require_identical and signature != expected:
                raise RuntimeError('Index outputs differ between benchmark runs')
            result = {'pair': pair + 1, 'build': name, 'images': index['changed'],
                      'index_ms': report['index_ms'], 'profile': index['profile'],
                      'output_sha256': digest}
            runs.append(result)
            print(json.dumps(result), flush=True)
    summary = {'runs': runs, 'outputs_identical': len({r['output_sha256'] for r in runs}) == 1}
    for name in ['baseline', 'candidate']:
        group = [r for r in runs if r['build'] == name]
        summary[name] = {
            'median_index_ms': statistics.median(r['index_ms'] for r in group),
            'median_preprocess_worker_ms': statistics.median(r['profile']['preprocess_ms'] for r in group)
        }
    (args.output / 'summary.json').write_text(json.dumps(summary, indent=2), encoding='utf-8')
    print(json.dumps({k: v for k, v in summary.items() if k != 'runs'}))


if __name__ == '__main__':
    main()
