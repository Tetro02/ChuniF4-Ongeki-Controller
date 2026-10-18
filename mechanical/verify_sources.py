#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Check mechanical provenance/integrity; this is not a physical fit test."""
from pathlib import Path
import hashlib
import json
import zipfile
from urllib.parse import quote


def main():
    mechanical = Path(__file__).resolve().parent
    root = mechanical.parent
    manifest = json.loads((mechanical/'sources.json').read_text(encoding='utf-8'))
    assert manifest['upstream']['commit'] == 'f7b277a77f77a303357a6cdc2e73a1d9f72dd2f2'
    expected = {'backplate-left-CUT.dxf': 1, 'backplate-right-CUT.dxf': 1,
                'backplate-akari-CUT.dxf': 7, 'backplate-setsuna-CUT.dxf': 7}
    indexed = set()
    for item in manifest['files']:
        p = (root/item['path']).resolve()
        assert p.is_relative_to(mechanical), item['path']
        assert item['path'] not in indexed
        indexed.add(item['path'])
        data = p.read_bytes()
        assert len(data) == item['bytes']
        assert hashlib.sha256(data).hexdigest() == item['sha256'], item['path']
        assert item['source_commit'] == manifest['upstream']['commit']
        assert item['license'] == 'MPL-2.0'
        assert item['source_url'] == (manifest['upstream']['repository']+'/blob/'
                                    +item['source_commit']+'/'+quote(item['upstream_path'],safe='/'))
        if not item['modified']:
            assert hashlib.sha1(b'blob '+str(len(data)).encode()+b'\0'+data).hexdigest() == item['git_blob_sha1']
        else:
            source = mechanical/'upstream/NAGEKI-3.33'/item['upstream_path']
            assert hashlib.sha256(source.read_bytes()).hexdigest() == item['source_sha256']
            lines = data.decode('ascii').splitlines()
            assert len(lines) % 2 == 0
            pairs = [(int(lines[i].strip()), lines[i+1].strip()) for i in range(0,len(lines),2)]
            unit_at = pairs.index((9, '$INSUNITS'))
            assert pairs[unit_at+1] == (70, '4'), p.name
            polylines = []
            for i, pair in enumerate(pairs):
                if pair not in ((0, 'LWPOLYLINE'), (0, 'POLYLINE')):
                    continue
                j = i+1
                while j < len(pairs) and pairs[j][0] != 0:
                    j += 1
                values = pairs[i+1:j]
                assert any(code == 70 and int(value) & 1 for code,value in values), p.name
                polylines.append(values)
            assert len(polylines) == expected[p.name], (p.name,len(polylines))
        if p.suffix.lower() == '.f3d':
            with zipfile.ZipFile(p) as z:
                assert z.testzip() is None
    geometry = {p.relative_to(root).as_posix() for p in mechanical.rglob('*')
                if p.is_file() and p.suffix.lower() in {'.stl','.f3d','.dxf'}}
    assert geometry <= indexed, 'Unindexed mechanical geometry'
    print(f'PASS: {len(indexed)} indexed mechanical files; SHA256/Git blob/source links, 2 F3D archives, 4 modified DXFs (mm, closed contours).')
    print('Mechanical dimensions, spring support and physical fit still require real assembly checks.')


if __name__ == '__main__':
    main()
