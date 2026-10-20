# SPDX-FileCopyrightText: Copyright 2026 Jetperch LLC
# SPDX-License-Identifier: Apache-2.0

import json
import os

import pytest

from usb_insight_hub import bundled


def _make(root, **manifest):
    os.makedirs(root, exist_ok=True)
    for name in bundled.FILES:
        with open(os.path.join(root, name), 'wb') as f:
            f.write(b'\x00')
    with open(os.path.join(root, 'manifest.json'), 'w') as f:
        json.dump({'version': '0.1.1', 'git': 'abc1234', 'idf': 'v6.1', **manifest}, f)


def test_files_in_flash_order(tmp_path):
    root = str(tmp_path / 'fw')
    _make(root)
    paths = bundled.files(root)
    assert [os.path.basename(p) for p in paths] == list(bundled.FILES)
    assert bundled.manifest(root)['git'] == 'abc1234'


def test_missing_firmware(tmp_path):
    with pytest.raises(LookupError, match='no bundled firmware'):
        bundled.files(str(tmp_path / 'none'))
    root = str(tmp_path / 'partial')
    _make(root)
    os.remove(os.path.join(root, 'uih_firmware.bin'))
    with pytest.raises(LookupError):
        bundled.files(root)


def test_default_root_is_in_the_package():
    assert bundled.default_root().endswith(os.path.join('usb_insight_hub', 'fw'))


def _build_dir(root, version='0.1.1'):
    for rel in bundled.BUILD_FILES:
        path = os.path.join(root, *rel.split('/'))
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, 'wb') as f:
            f.write(rel.encode())
    with open(os.path.join(root, 'project_description.json'), 'w') as f:
        json.dump({'project_version': version}, f)
    return root


def test_build_files(tmp_path):
    build = _build_dir(str(tmp_path / 'build'))
    paths = bundled.build_files(build)
    assert [os.path.basename(p) for p in paths] == list(bundled.FILES)
    os.remove(paths[1])
    with pytest.raises(LookupError, match='partition-table.bin'):
        bundled.build_files(build)


def test_collect(tmp_path):
    build = _build_dir(str(tmp_path / 'build'))
    out = str(tmp_path / 'fw')
    m = bundled.collect(build, out, git='e61a86e', idf='v6.1', expect_version='0.1.1')
    assert m == {'version': '0.1.1', 'git': 'e61a86e', 'idf': 'v6.1'}
    assert bundled.manifest(out) == m
    with open(bundled.files(out)[0], 'rb') as f:
        assert f.read() == b'bootloader/bootloader.bin'


def test_collect_rejects_version_mismatch(tmp_path):
    build = _build_dir(str(tmp_path / 'build'), version='0.1.0')
    with pytest.raises(ValueError, match='0.1.0.*0.1.1'):
        bundled.collect(build, str(tmp_path / 'fw'), git='x', idf='v6.1',
                        expect_version='0.1.1')
