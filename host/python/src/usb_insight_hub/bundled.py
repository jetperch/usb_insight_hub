# SPDX-FileCopyrightText: Copyright 2026 Jetperch LLC
# SPDX-License-Identifier: Apache-2.0

"""The firmware bundled in this package by the release build.

CI builds the firmware (ESP-IDF) and places its images and manifest.json in
the package's ``fw/`` directory before building the wheel, so ``uih flash``
installs the firmware released with this package.  Source checkouts have
no bundled firmware: pass a build directory instead.
"""

import json
import os
import shutil

# In flash order: bootloader, partition table, app (esp.Esptool.flash_firmware).
FILES = ('bootloader.bin', 'partition-table.bin', 'uih_firmware.bin')
MANIFEST = 'manifest.json'
# The same images in an ESP-IDF build directory.
BUILD_FILES = ('bootloader/bootloader.bin', 'partition_table/partition-table.bin',
               'uih_firmware.bin')


def default_root():
    return os.path.join(os.path.dirname(os.path.abspath(__file__)), 'fw')


def files(root=None):
    """Return the image paths in flash order.

    :raise LookupError: if the package has no (complete) bundled firmware.
    """
    root = root or default_root()
    paths = [os.path.join(root, name) for name in FILES]
    if not all(os.path.isfile(p) for p in paths):
        raise LookupError('no bundled firmware in this package (source checkout?); '
                          'pass an ESP-IDF build directory')
    return paths


def manifest(root=None):
    """Return the bundled firmware's manifest: version, git, idf."""
    with open(os.path.join(root or default_root(), MANIFEST)) as f:
        return json.load(f)


def build_files(build_dir):
    """Return the image paths in an ESP-IDF build directory, in flash order.

    :raise LookupError: if an image is missing.
    """
    paths = [os.path.join(build_dir, *rel.split('/')) for rel in BUILD_FILES]
    for p in paths:
        if not os.path.isfile(p):
            raise LookupError(f'missing build output: {p}')
    return paths


def collect(build_dir, out_dir, git, idf, expect_version):
    """Copy a firmware build into ``out_dir`` with its manifest (release builds).

    :param expect_version: the host package version; the firmware must match.
    :return: the manifest.
    :raise ValueError: if the firmware version differs.
    """
    with open(os.path.join(build_dir, 'project_description.json')) as f:
        version = json.load(f)['project_version']
    if version != expect_version:
        raise ValueError(f'firmware version {version} != host package version {expect_version}')
    os.makedirs(out_dir, exist_ok=True)
    for src, name in zip(build_files(build_dir), FILES):
        shutil.copyfile(src, os.path.join(out_dir, name))
    m = {'version': version, 'git': git, 'idf': idf}
    with open(os.path.join(out_dir, MANIFEST), 'w') as f:
        json.dump(m, f, indent=2)
    return m
