/*
 * SPDX-FileCopyrightText: Copyright 2026 Jetperch LLC
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 *
 * @brief CRC-32 (IEEE 802.3), as zlib.crc32.
 */

#ifndef UIH_CRC32_H_
#define UIH_CRC32_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

uint32_t uih_crc32(const uint8_t * data, size_t length);

#ifdef __cplusplus
}
#endif

#endif  /* UIH_CRC32_H_ */
