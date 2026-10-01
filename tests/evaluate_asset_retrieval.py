"""Evaluate a dedicated procedural fixture library (optional Python/NumPy tooling)."""
import argparse
import json
import sqlite3
from pathlib import Path

import numpy as np

parser = argparse.ArgumentParser()
parser.add_argument("database", type=Path)
parser.add_argument("truth", type=Path)
parser.add_argument("--output", type=Path)
args = parser.parse_args()
truth = json.loads(args.truth.read_text())
with sqlite3.connect(args.database.resolve().as_uri() + "?mode=ro", uri=True) as database:
    rows = database.execute(
        "SELECT p.name,p.embedding,a.descriptor FROM photos p "
        "JOIN appearances a ON a.photo_id=p.id ORDER BY p.id"
    ).fetchall()
if len(rows) != len(truth) or {row[0] for row in rows} != set(truth):
    raise SystemExit("Use a dedicated fixture library containing exactly the generated assets.")
families = np.array([truth[name]["family"] for name, _, _ in rows])
report = {"scope": "Procedural icons only; relevant means same shape and color. Not a real-world quality benchmark.", "queries": len(rows)}
for label, offset in [("subject", 1), ("appearance", 2)]:
    matrix = np.array([np.frombuffer(row[offset], dtype=np.float32) for row in rows])
    matrix /= np.linalg.norm(matrix, axis=1, keepdims=True)
    similarity = matrix @ matrix.T
    np.fill_diagonal(similarity, -np.inf)
    nearest = np.argsort(-similarity, axis=1)[:, :3]
    relevant = families[nearest] == families[:, None]
    report[label] = {"precision_at_1": float(relevant[:, 0].mean()), "recall_at_3": float(relevant.sum(axis=1).mean() / 3)}
appearance = np.array([np.frombuffer(row[2], dtype=np.float32) for row in rows])
shapes = np.array([family.split('-', 1)[1] for family in families])
colors = np.array([family.split('-', 1)[0] for family in families])
report['intent_scope'] = 'Shape relevance ignores color; palette relevance ignores shape. Procedural fixture only.'
for name, columns, groups, k in [
    ('shape', list(range(192, 256)) + list(range(320, 512)), shapes, 15),
    ('palette', list(range(192)) + list(range(256, 320)), colors, 23),
    ('balanced_for_shape', list(range(512)), shapes, 15),
    ('balanced_for_palette', list(range(512)), colors, 23),
]:
    matrix = appearance[:, columns].copy()
    matrix /= np.maximum(np.linalg.norm(matrix, axis=1, keepdims=True), 1e-12)
    scores = matrix @ matrix.T
    np.fill_diagonal(scores, -np.inf)
    nearest = np.argsort(-scores, axis=1)[:, :k]
    relevant = groups[nearest] == groups[:, None]
    report[name] = {'precision_at_1': float(relevant[:, 0].mean()),
                    f'recall_at_{k}': float(relevant.sum(axis=1).mean() / k)}
rendered = json.dumps(report, indent=2)
if args.output:
    args.output.write_text(rendered, encoding="utf-8")
print(rendered)
