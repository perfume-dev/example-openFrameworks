"""Fetch only the pinned, BSD-licensed G1 model and its referenced assets."""
import argparse
import hashlib
import json
from pathlib import Path
import urllib.request
import xml.etree.ElementTree as ET

REV = '8161bba264d7fa7c99ca301e91e7fb44737676ad'
BASE = f'https://raw.githubusercontent.com/google-deepmind/mujoco_menagerie/{REV}/unitree_g1/'


def fetch(out, relative):
    path = out / relative
    url = BASE + relative
    content = urllib.request.urlopen(url, timeout=60).read()
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.exists() and path.read_bytes() != content:
        raise RuntimeError(f'Refusing to replace a different model file: {path}')
    path.write_bytes(content)
    return {'path': relative, 'url': url, 'sha256': hashlib.sha256(content).hexdigest(), 'bytes': len(content)}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    records = [fetch(args.output, p) for p in ('g1.xml', 'scene.xml', 'LICENSE', 'README.md')]
    tree = ET.parse(args.output / 'g1.xml')
    for filename in sorted({node.attrib['file'] for node in tree.findall('.//asset/mesh')}):
        if '/' in filename or '..' in filename:
            raise RuntimeError('Unexpected mesh path')
        records.append(fetch(args.output, 'assets/' + filename))
    limits_url = 'https://raw.githubusercontent.com/unitreerobotics/unitree_ros/7d6075f7f58588b189b940130e3edab3c839b2df/robots/g1_description/g1_29dof_rev_1_0.urdf'
    urdf = urllib.request.urlopen(limits_url, timeout=60).read()
    (args.output / 'g1_29dof_rev_1_0.urdf').write_bytes(urdf)
    records.append({'path': 'g1_29dof_rev_1_0.urdf', 'url': limits_url,
                    'sha256': hashlib.sha256(urdf).hexdigest(), 'bytes': len(urdf)})
    manifest = {'repository': 'google-deepmind/mujoco_menagerie', 'commit': REV,
                'model': 'g1_29dof_rev_1_0', 'license': 'BSD-3-Clause', 'files': records}
    (args.output / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print(json.dumps({'files': len(records), 'bytes': sum(r['bytes'] for r in records), 'commit': REV}))


if __name__ == '__main__':
    main()
