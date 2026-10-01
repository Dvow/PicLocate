"""Compare native preprocessing/embeddings with Pillow and ONNX Runtime.

Use only a disposable test library. First set PICLOCATE_PARITY_DIR to its fixture
directory and run piclocate_tests to export native .f32 tensors beside the images.
This script is a developer check; it is not used by the C++ application.
"""
import argparse
import json
import sqlite3
from pathlib import Path

import numpy as np
import onnxruntime as ort
from PIL import Image, ImageOps


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--library', type=Path, required=True)
    parser.add_argument('--models', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    options = ort.SessionOptions()
    options.intra_op_num_threads = 4
    session = ort.InferenceSession(
        str(args.models / 'vision_model_quantized.onnx'), options,
        providers=['CPUExecutionProvider'])
    comparisons = []
    with sqlite3.connect(args.library) as database:
        for name, path, blob in database.execute('SELECT name,path,embedding FROM photos'):
            image = ImageOps.exif_transpose(Image.open(path)).convert('RGB')
            width, height = image.size
            scale = 224 / min(width, height)
            resized = int(width * scale), int(height * scale)
            image = image.resize(resized, Image.Resampling.BICUBIC)
            left, top = (resized[0] - 224) // 2, (resized[1] - 224) // 2
            image = image.crop((left, top, left + 224, top + 224))
            pixels = np.asarray(image, dtype=np.float32) / 255
            pixels = (pixels - np.array([.48145466, .4578275, .40821073], dtype=np.float32)) / np.array([.26862954, .26130258, .27577711], dtype=np.float32)
            pixels = pixels.transpose(2, 0, 1)
            native_pixels = np.fromfile(path + '.f32', dtype=np.float32).reshape(3, 224, 224)
            reference = session.run(['image_embeds'], {'pixel_values': pixels[None]})[0][0]
            reference /= np.linalg.norm(reference)
            native = np.frombuffer(blob, dtype=np.float32)
            comparisons.append({
                'name': name,
                'pixel_max_absolute_error': float(np.max(np.abs(native_pixels - pixels))),
                'embedding_cosine': float(reference @ native),
                'embedding_max_absolute_error': float(np.max(np.abs(reference - native))),
            })
    args.output.write_text(json.dumps(comparisons, indent=2), encoding='utf-8')
    print(f'Compared {len(comparisons)} images; results in {args.output}')
    if any(row['embedding_cosine'] < .999 for row in comparisons):
        raise SystemExit('Model parity fell below the test tolerance')


if __name__ == '__main__':
    main()
