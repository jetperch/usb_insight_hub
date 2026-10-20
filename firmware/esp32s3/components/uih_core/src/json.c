/*
 * SPDX-FileCopyrightText: Copyright 2026 Jetperch LLC
 * SPDX-License-Identifier: Apache-2.0
 */

#include "uih/json.h"
#include <string.h>

#define DEPTH_MAX (16)

typedef struct {
    const char * p;
    const char * end;
} cursor_s;

static void ws(cursor_s * c) {
    while ((c->p < c->end) && ((*c->p == ' ') || (*c->p == '\t') || (*c->p == '\r') || (*c->p == '\n'))) {
        ++c->p;
    }
}

static bool is_digit(char ch) {
    return (ch >= '0') && (ch <= '9');
}

static bool is_hex(char ch) {
    return is_digit(ch) || ((ch >= 'a') && (ch <= 'f')) || ((ch >= 'A') && (ch <= 'F'));
}

static int scan_string(cursor_s * c) {
    if ((c->p >= c->end) || (*c->p != '"')) {
        return 1;
    }
    ++c->p;
    while (c->p < c->end) {
        unsigned char ch = (unsigned char) *c->p++;
        if (ch == '"') {
            return 0;
        } else if (ch < 0x20) {
            return 1;  // control characters must be escaped
        } else if (ch == '\\') {
            if (c->p >= c->end) {
                return 1;
            }
            char e = *c->p++;
            if (e == 'u') {
                for (int i = 0; i < 4; ++i) {
                    if ((c->p >= c->end) || !is_hex(*c->p++)) {
                        return 1;
                    }
                }
            } else if (!strchr("\"\\/bfnrt", e)) {
                return 1;
            }
        }
    }
    return 1;
}

static int scan_number(cursor_s * c) {
    if ((c->p < c->end) && (*c->p == '-')) {
        ++c->p;
    }
    if ((c->p >= c->end) || !is_digit(*c->p)) {
        return 1;
    }
    if (*c->p == '0') {
        ++c->p;
    } else {
        while ((c->p < c->end) && is_digit(*c->p)) {
            ++c->p;
        }
    }
    if ((c->p < c->end) && (*c->p == '.')) {
        ++c->p;
        if ((c->p >= c->end) || !is_digit(*c->p)) {
            return 1;
        }
        while ((c->p < c->end) && is_digit(*c->p)) {
            ++c->p;
        }
    }
    if ((c->p < c->end) && ((*c->p == 'e') || (*c->p == 'E'))) {
        ++c->p;
        if ((c->p < c->end) && ((*c->p == '+') || (*c->p == '-'))) {
            ++c->p;
        }
        if ((c->p >= c->end) || !is_digit(*c->p)) {
            return 1;
        }
        while ((c->p < c->end) && is_digit(*c->p)) {
            ++c->p;
        }
    }
    // A number must end at a delimiter (catches "01", "1x").
    if ((c->p < c->end) && !strchr(" \t\r\n,]}", *c->p)) {
        return 1;
    }
    return 0;
}

static int scan_literal(cursor_s * c, const char * word) {
    size_t n = strlen(word);
    if (((size_t) (c->end - c->p) < n) || memcmp(c->p, word, n)) {
        return 1;
    }
    c->p += n;
    return 0;
}

static int scan_value(cursor_s * c, uih_json_token_s * token, int depth);

static int scan_container(cursor_s * c, int depth, char open, char close) {
    if (depth >= DEPTH_MAX) {
        return 1;
    }
    ++c->p;  // open
    ws(c);
    if ((c->p < c->end) && (*c->p == close)) {
        ++c->p;
        return 0;
    }
    while (1) {
        uih_json_token_s ignore;
        if (open == '{') {
            if (scan_string(c)) {
                return 1;
            }
            ws(c);
            if ((c->p >= c->end) || (*c->p++ != ':')) {
                return 1;
            }
            ws(c);
        }
        if (scan_value(c, &ignore, depth + 1)) {
            return 1;
        }
        ws(c);
        if (c->p >= c->end) {
            return 1;
        }
        char ch = *c->p++;
        if (ch == close) {
            return 0;
        } else if (ch != ',') {
            return 1;
        }
        ws(c);
    }
}

static int scan_value(cursor_s * c, uih_json_token_s * token, int depth) {
    if (c->p >= c->end) {
        return 1;
    }
    const char * start = c->p;
    int rc;
    switch (*c->p) {
        case '"': token->type = UIH_JSON_STRING; rc = scan_string(c); break;
        case '{': token->type = UIH_JSON_OBJECT; rc = scan_container(c, depth, '{', '}'); break;
        case '[': token->type = UIH_JSON_ARRAY; rc = scan_container(c, depth, '[', ']'); break;
        case 't': token->type = UIH_JSON_TRUE; rc = scan_literal(c, "true"); break;
        case 'f': token->type = UIH_JSON_FALSE; rc = scan_literal(c, "false"); break;
        case 'n': token->type = UIH_JSON_NULL; rc = scan_literal(c, "null"); break;
        default: token->type = UIH_JSON_NUMBER; rc = scan_number(c); break;
    }
    token->ptr = start;
    token->len = (size_t) (c->p - start);
    return rc;
}

int uih_json_object_parse(const char * text, size_t len, uih_json_member_fn fn, void * user) {
    cursor_s c = {text, text + len};
    ws(&c);
    if ((c.p >= c.end) || (*c.p != '{')) {
        return 1;
    }
    ++c.p;
    ws(&c);
    if ((c.p < c.end) && (*c.p == '}')) {
        ++c.p;
    } else {
        while (1) {
            uih_json_token_s key;
            uih_json_token_s value;
            key.type = UIH_JSON_STRING;
            key.ptr = c.p;
            if (scan_string(&c)) {
                return 1;
            }
            key.len = (size_t) (c.p - key.ptr);
            ws(&c);
            if ((c.p >= c.end) || (*c.p++ != ':')) {
                return 1;
            }
            ws(&c);
            if (scan_value(&c, &value, 1)) {
                return 1;
            }
            if (fn(user, &key, &value)) {
                return 1;
            }
            ws(&c);
            if (c.p >= c.end) {
                return 1;
            }
            char ch = *c.p++;
            if (ch == '}') {
                break;
            } else if (ch != ',') {
                return 1;
            }
            ws(&c);
        }
    }
    ws(&c);
    return (c.p == c.end) ? 0 : 1;
}

int uih_json_int(const uih_json_token_s * token, int64_t * value) {
    if ((token->type != UIH_JSON_NUMBER) || (token->len == 0)) {
        return 1;
    }
    const char * p = token->ptr;
    const char * end = p + token->len;
    bool negative = false;
    if (*p == '-') {
        negative = true;
        ++p;
    }
    if (p >= end) {
        return 1;
    }
    uint64_t v = 0;
    for (; p < end; ++p) {
        if (!is_digit(*p)) {
            return 1;  // fraction or exponent
        }
        uint64_t digit = (uint64_t) (*p - '0');
        if (v > (UINT64_C(9223372036854775807) - digit) / 10) {
            return 1;
        }
        v = v * 10 + digit;
    }
    *value = negative ? -(int64_t) v : (int64_t) v;
    return 0;
}

static int hex_value(char ch) {
    if (is_digit(ch)) {
        return ch - '0';
    } else if ((ch >= 'a') && (ch <= 'f')) {
        return ch - 'a' + 10;
    }
    return ch - 'A' + 10;
}

int uih_json_string_decode(const uih_json_token_s * token, char * buf, size_t size) {
    if ((token->type != UIH_JSON_STRING) || (token->len < 2) || (size == 0)) {
        return -1;
    }
    const char * p = token->ptr + 1;
    const char * end = token->ptr + token->len - 1;  // closing quote
    size_t n = 0;
    while (p < end) {
        char ch = *p++;
        if (ch == '\\') {
            char e = *p++;
            switch (e) {
                case 'b': ch = '\b'; break;
                case 'f': ch = '\f'; break;
                case 'n': ch = '\n'; break;
                case 'r': ch = '\r'; break;
                case 't': ch = '\t'; break;
                case 'u': {
                    int code = 0;
                    for (int i = 0; i < 4; ++i) {
                        code = (code << 4) | hex_value(*p++);
                    }
                    if (code > 0x7F) {
                        return -1;
                    }
                    ch = (char) code;
                    break;
                }
                default: ch = e; break;  // " \ /
            }
        }
        if (n + 1 >= size) {
            return -1;
        }
        buf[n++] = ch;
    }
    buf[n] = 0;
    return (int) n;
}

bool uih_json_string_equals(const uih_json_token_s * token, const char * str) {
    char buf[32];
    int n = uih_json_string_decode(token, buf, sizeof(buf));
    return (n >= 0) && (0 == strcmp(buf, str));
}
