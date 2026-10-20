#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright 2026 Jetperch LLC
# SPDX-License-Identifier: Apache-2.0

"""Run `uih update-loop`.  Requires the host package (host/python)."""

import sys

from usb_insight_hub.cli import main

if __name__ == '__main__':
    sys.exit(main(['update-loop'] + sys.argv[1:]))
