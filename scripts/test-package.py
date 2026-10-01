#!/usr/bin/env python3
"""Exercise a deployed application with an isolated, generated image library."""
import argparse
import json
import math
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import zlib


def png(path, color):
    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))
    rows = b''.join(b'\0' + bytes(color) * 64 for _ in range(64))
    path.write_bytes(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>2I5B', 64, 64, 8, 2, 0, 0, 0))
                     + chunk(b'IDAT', zlib.compress(rows)) + chunk(b'IEND', b''))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('executable', type=Path)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--gpu', action='store_true', help='Exercise automatic GPU selection/fallback')
    parser.add_argument('--require-gpu', action='store_true')
    parser.add_argument('--screenshot', action='store_true')
    args = parser.parse_args()
    executable = args.executable.resolve(strict=True)
    output = args.output.resolve() if args.output else Path(tempfile.mkdtemp(prefix='PicLocate-package-'))
    output.mkdir(parents=True, exist_ok=True)
    data = output / 'library'
    if data.exists():
        parser.error('Choose a fresh output directory; existing test libraries are never overwritten')
    fixtures = output / 'images'
    fixtures.mkdir(exist_ok=True)
    for name, color in [('red', (240, 25, 30)), ('blue', (20, 35, 240)), ('green', (15, 220, 45))]:
        png(fixtures / (name + '.png'), color)
    env = os.environ.copy()
    env['PICLOCATE_DISABLE_UPDATE_CHECK'] = '1'
    for name in ('PICLOCATE_MODEL_DIR', 'QT_PLUGIN_PATH',
                 'QT_QPA_PLATFORM_PLUGIN_PATH', 'LD_LIBRARY_PATH', 'DYLD_LIBRARY_PATH'):
        env.pop(name, None)
    env['PATH'] = (os.path.join(os.environ['SystemRoot'], 'System32') if os.name == 'nt' else '/usr/bin:/bin')
    def run(label, *argv):
        result = subprocess.run([str(executable), *map(str, argv)], cwd=output, env=env,
                                capture_output=True, text=True, encoding='utf-8', errors='replace', timeout=240)
        (output / (label + '.stdout.log')).write_text(result.stdout, encoding='utf-8')
        (output / (label + '.stderr.log')).write_text(result.stderr, encoding='utf-8')
        if result.returncode:
            raise RuntimeError(f'{label}: exit {result.returncode}: {result.stderr[-4000:]}')
        return result.stdout
    diagnostics = json.loads(run('diagnostics', '--diagnostics'))
    assert diagnostics['application'] == 'PicLocate', diagnostics
    report = output / 'search.json'
    flags = [] if args.gpu or args.require_gpu else ['--cpu']
    run('index-search', '--headless', '--no-ocr', *flags, '--data-dir', data,
        '--index', fixtures, '--query', 'a solid red image', '--report', report)
    search = json.loads(report.read_text(encoding='utf-8'))
    assert search['library_images'] == 3 and search['index']['failed'] == 0, search
    # Flat colors are a deterministic deployment fixture, not a CLIP relevance benchmark.
    results = search['search']['results']
    assert len(results) == 3 and all(math.isfinite(p['score']) for p in results), search
    assert all(results[i]['score'] >= results[i + 1]['score'] for i in range(len(results) - 1)), search
    if args.require_gpu:
        assert 'GPU' in search['index']['profile']['backend'] and not search['index']['profile']['gpu_fallback'], search
    run('text-search', '--headless', '--data-dir', data, '--text-only', '--query', 'blue',
        '--report', output / 'text.json')
    text = json.loads((output / 'text.json').read_text(encoding='utf-8'))
    assert [p['name'] for p in text['search']['results']] == ['blue.png'], text
    # A reference outside the library should retrieve its indexed counterpart.
    reference = output / 'reference.png'
    png(reference, (15, 220, 45))
    run('appearance', '--headless', '--data-dir', data, '--reference', reference,
        '--appearance', '--report', output / 'appearance.json')
    appearance = json.loads((output / 'appearance.json').read_text(encoding='utf-8'))
    assert appearance['search']['results'][0]['name'] == 'green.png', appearance
    if args.screenshot:
        run('desktop', '--data-dir', data, '--screenshot', output / 'desktop.png', '--exit-after', '5500')
        assert (output / 'desktop.png').stat().st_size > 1000
    summary = dict(diagnostics=diagnostics, indexed=3, semantic_search=True, text_search=True,
                   appearance_search=True, index_profile=search['index']['profile'])
    (output / 'result.json').write_text(json.dumps(summary, indent=2), encoding='utf-8')
    print(json.dumps(summary, indent=2))
    print(f'Package checks passed: {output}')


if __name__ == '__main__':
    main()
