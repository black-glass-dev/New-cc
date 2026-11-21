#!/usr/bin/env python3
import json
from pathlib import Path
plan = json.loads((Path(__file__).resolve().parents[1]/'docs/plans/readiness.json').read_text())
assert sum(item['weight'] for item in plan['items']) == 100
assert len({item['id'] for item in plan['items']}) == len(plan['items'])
for item in plan['items']:
    assert item['status'] in ('verified','in_progress','planned','blocked')
    assert item['weight'] > 0 and item['evidence']
done = sum(item['weight'] for item in plan['items'] if item['status'] == 'verified')
print(f"Weighted roadmap readiness: {done}% credited, {100-done}% remaining")
print('Estimate with explicit weights, not elapsed effort, LOC or a promise of full professional parity.')
