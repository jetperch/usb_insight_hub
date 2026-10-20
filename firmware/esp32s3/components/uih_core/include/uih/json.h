/*
 * SPDX-FileCopyrightText: Copyright 2026 Jetperch LLC
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 *
 * @brief A minimal, allocation-free JSON reader for protocol requests.
 *
 * Validates a complete JSON document whose top level is an object, and
 * calls back once per top-level member.  Nested values are validated and
 * passed as raw tokens.  Strings keep their quotes and escapes in the
 * token; use uih_json_string_decode() or uih_json_string_equals().
 */

#ifndef UIH_JSON_H_
#define UIH_JSON_H_

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    UIH_JSON_STRING,
    UIH_JSON_NUMBER,
    UIH_JSON_TRUE,
    UIH_JSON_FALSE,
    UIH_JSON_NULL,
    UIH_JSON_OBJECT,
    UIH_JSON_ARRAY,
} uih_json_type_e;

typedef struct {
    uih_json_type_e type;
    const char * ptr;   ///< The raw token text, including string quotes.
    size_t len;
} uih_json_token_s;

/**
 * @brief Called once per top-level member.
 *
 * @return 0 to continue, nonzero to abort the parse.
 */
typedef int (*uih_json_member_fn)(void * user, const uih_json_token_s * key,
                                   const uih_json_token_s * value);

/**
 * @brief Parse a JSON object and call fn for each member.
 *
 * @return 0 on success, nonzero if the document is invalid, nests deeper
 *      than 16 levels, or fn aborted.
 */
int uih_json_object_parse(const char * text, size_t len, uih_json_member_fn fn, void * user);

/**
 * @brief Convert a number token that is a plain integer.
 *
 * @return 0 on success, nonzero for a fraction, exponent, overflow beyond
 *      int64, or a token that is not a number.
 */
int uih_json_int(const uih_json_token_s * token, int64_t * value);

/**
 * @brief Decode a string token into a NUL-terminated ASCII buffer.
 *
 * @return the decoded length, or -1 for a non-string token, a buffer that
 *      is too small, or a \\u escape outside ASCII.
 */
int uih_json_string_decode(const uih_json_token_s * token, char * buf, size_t size);

/// Return true if the string token decodes to str.
bool uih_json_string_equals(const uih_json_token_s * token, const char * str);

#ifdef __cplusplus
}
#endif

#endif  /* UIH_JSON_H_ */
