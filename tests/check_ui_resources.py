#!/usr/bin/env python3
"""No external packages: bilingual key/format parity, references and palette contrast."""
import re
from pathlib import Path
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]

def strings(folder):
    entries = ET.parse(ROOT / 'app/src/main/res' / folder / 'strings.xml').getroot().findall('string')
    result = {item.attrib['name']: ''.join(item.itertext()) for item in entries}
    assert len(result) == len(entries), f'Duplicate keys in {folder}'
    return result

en, bn = strings('values'), strings('values-bn')
assert en.keys() == bn.keys(), 'Missing language resources'
for key in en:
    params = lambda s: sorted(re.findall(r'%(\d+)\$[sdf]', s))
    assert params(en[key]) == params(bn[key]), f'Format mismatch: {key}'
    assert en[key].strip() and bn[key].strip(), f'Empty resource: {key}'
for source in (ROOT / 'app/src').rglob('*.kt'):
    for key in re.findall(r'R\.string\.(\w+)', source.read_text()):
        assert key in en, f'Unknown string {key}: {source}'

def luminance(rgb):
    channels = [(int(rgb[i:i+2], 16) / 255) for i in (0, 2, 4)]
    channels = [c / 12.92 if c <= .04045 else ((c + .055) / 1.055) ** 2.4 for c in channels]
    return sum(c * w for c, w in zip(channels, (.2126, .7152, .0722)))

import json
palettes = json.loads((ROOT / 'app/src/main/assets/studio_themes.json').read_text())
assert set(palettes) == {'graphite','midnight','ember'}
for name, palette in palettes.items():
    for foreground in ('text','muted','accent'):
        for background in ('background','surface','raised','selection'):
            a, b = sorted((luminance(palette[foreground]),luminance(palette[background])))
            assert (b+.05)/(a+.05) >= 4.5, (name,foreground,background)
print(f'{len(en)} bilingual strings; resource parity and all three theme contrasts passed')

for folder in ('values','values-bn'):
    tree=ET.parse(ROOT / 'app/src/main/res' / folder / 'strings.xml')
    labels=tree.find(".//string-array[@name='color_parameter_names']")
    assert labels is not None and len(labels.findall('item'))==17, 'native v1 color controls need 17 labels'
print('17 color parameter labels in both languages passed')
