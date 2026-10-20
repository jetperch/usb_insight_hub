#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright 2026 Jetperch LLC
# SPDX-License-Identifier: Apache-2.0

"""Bundle a firmware build into the host package (release builds).

Copies the images and writes fw/manifest.json in host/python's package, so
the wheel carries the firmware it was released with.  Fails if the
firmware version differs from the host package version.

    python tools/bundle_firmware.py BUILD_DIR --git abc1234 --idf v6.1
"""

import argparse
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'host', 'python', 'src'))

from usb_insight_hub import bundled  # noqa: E402
from usb_insight_hub.version import __version__  # noqa: E402


def main():
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument('build_dir')
    p.add_argument('--git', required=True, help='firmware git commit (short hash)')
    p.add_argument('--idf', required=True, help='ESP-IDF version')
    args = p.parse_args()
    m = bundled.collect(args.build_dir, bundled.default_root(), args.git, args.idf,
                        expect_version=__version__)
    print(f'bundled firmware {m} into {bundled.default_root()}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
