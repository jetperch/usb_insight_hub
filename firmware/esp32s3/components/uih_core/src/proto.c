/*
 * SPDX-FileCopyrightText: Copyright 2026 Jetperch LLC
 * SPDX-License-Identifier: Apache-2.0
 */

#include "uih/proto.h"
#include "uih/crc32.h"
#include "uih/json.h"
#include <stdio.h>
#include <string.h>

static const char * const CODE_STR[] = {
    [UIH_PROTO_OK] = "ok",
    [UIH_PROTO_FORMAT] = "format",
    [UIH_PROTO_CRC] = "crc",
    [UIH_PROTO_PARSE] = "parse",
    [UIH_PROTO_OVERFLOW] = "overflow",
    [UIH_PROTO_ARG] = "arg",
    [UIH_PROTO_OP] = "op",
    [UIH_PROTO_IO] = "io",
    [UIH_PROTO_NOT_APPLIED] = "not_applied",
    [UIH_PROTO_BUSY] = "busy",
};

static const char * const OP_STR[] = {
    [UIH_OP_PING] = "ping",
    [UIH_OP_INFO] = "info",
    [UIH_OP_STATUS] = "status",
    [UIH_OP_SET] = "set",
    [UIH_OP_STREAM] = "stream",
    [UIH_OP_BOOTLOADER] = "bootloader",
    [UIH_OP_RESET] = "reset",
};

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

const char * uih_proto_code_str(uih_proto_code_e code) {
    return ((unsigned) code < ARRAY_SIZE(CODE_STR)) ? CODE_STR[code] : "unknown";
}

const char * uih_op_str(uih_op_e op) {
    return ((unsigned) op < ARRAY_SIZE(OP_STR)) ? OP_STR[op] : "?";
}

static int hex_digit(char ch) {
    if ((ch >= '0') && (ch <= '9')) {
        return ch - '0';
    } else if ((ch >= 'a') && (ch <= 'f')) {
        return ch - 'a' + 10;
    } else if ((ch >= 'A') && (ch <= 'F')) {
        return ch - 'A' + 10;
    }
    return -1;
}

uih_proto_code_e uih_proto_frame_decode(const char * line, size_t len,
                                        const char ** json, size_t * json_len) {
    while (len && ((line[len - 1] == '\n') || (line[len - 1] == '\r'))) {
        --len;
    }
    size_t sep = len;
    while (sep && (line[sep - 1] != '*')) {
        --sep;
    }
    if (!sep || ((len - sep) != 8)) {
        return UIH_PROTO_FORMAT;  // no '*', or not 8 characters after the last one
    }
    uint32_t crc = 0;
    for (size_t i = sep; i < len; ++i) {
        int d = hex_digit(line[i]);
        if (d < 0) {
            return UIH_PROTO_FORMAT;
        }
        crc = (crc << 4) | (uint32_t) d;
    }
    *json = line;
    *json_len = sep - 1;
    if (uih_crc32((const uint8_t *) line, sep - 1) != crc) {
        return UIH_PROTO_CRC;
    }
    return UIH_PROTO_OK;
}

typedef struct {
    uih_json_token_s op;
    uih_json_token_s seq;
    uih_json_token_s port;
    uih_json_token_s power;
    uih_json_token_s data;
    uih_json_token_s rate_hz;
    bool has_op;
    bool has_seq;
    bool has_port;
    bool has_power;
    bool has_data;
    bool has_rate_hz;
} members_s;

static int on_member(void * user, const uih_json_token_s * key, const uih_json_token_s * value) {
    members_s * m = (members_s *) user;
#define KEY(name) if (uih_json_string_equals(key, #name)) { m->name = *value; m->has_##name = true; return 0; }
    KEY(op)
    KEY(seq)
    KEY(port)
    KEY(power)
    KEY(data)
    KEY(rate_hz)
#undef KEY
    return 0;  // unknown keys are ignored
}

static bool int_in_range(const uih_json_token_s * t, int64_t lo, int64_t hi, int64_t * value) {
    return (0 == uih_json_int(t, value)) && (*value >= lo) && (*value <= hi);
}

static uih_proto_code_e parse_bool(bool present, const uih_json_token_s * t, int8_t * value) {
    *value = -1;
    if (!present) {
        return UIH_PROTO_OK;
    }
    if (t->type == UIH_JSON_TRUE) {
        *value = 1;
    } else if (t->type == UIH_JSON_FALSE) {
        *value = 0;
    } else {
        return UIH_PROTO_ARG;
    }
    return UIH_PROTO_OK;
}

uih_proto_code_e uih_proto_request_parse(const char * json, size_t len, uih_request_s * req) {
    members_s m;
    memset(&m, 0, sizeof(m));
    memset(req, 0, sizeof(*req));
    req->power = -1;
    req->data = -1;
    if (uih_json_object_parse(json, len, on_member, &m)) {
        return UIH_PROTO_PARSE;
    }
    int64_t v = 0;
    if (!m.has_seq || !int_in_range(&m.seq, 0, 0xFFFFFFFFLL, &v)) {
        return UIH_PROTO_ARG;
    }
    req->seq = (uint32_t) v;
    req->seq_valid = true;
    if (!m.has_op || (m.op.type != UIH_JSON_STRING)) {
        return UIH_PROTO_ARG;
    }
    if (uih_json_string_decode(&m.op, req->op_name, sizeof(req->op_name)) < 0) {
        req->op_name[0] = 0;
        return UIH_PROTO_OP;
    }
    size_t op;
    for (op = 0; op < ARRAY_SIZE(OP_STR); ++op) {
        if (!strcmp(req->op_name, OP_STR[op])) {
            break;
        }
    }
    if (op >= ARRAY_SIZE(OP_STR)) {
        return UIH_PROTO_OP;
    }
    req->op = (uih_op_e) op;

    if (req->op == UIH_OP_SET) {
        if (!m.has_port) {
            return UIH_PROTO_ARG;
        }
        if ((m.port.type == UIH_JSON_STRING) && uih_json_string_equals(&m.port, "all")) {
            req->port_mask = UIH_PORT_ALL;
        } else if (int_in_range(&m.port, 1, UIH_PORT_COUNT, &v)) {
            req->port_mask = (uint8_t) (1u << (v - 1));
        } else {
            return UIH_PROTO_ARG;
        }
        if (parse_bool(m.has_power, &m.power, &req->power) ||
                parse_bool(m.has_data, &m.data, &req->data)) {
            return UIH_PROTO_ARG;
        }
        if ((req->power < 0) && (req->data < 0)) {
            return UIH_PROTO_ARG;
        }
    } else if (req->op == UIH_OP_STREAM) {
        if (!m.has_rate_hz || !int_in_range(&m.rate_hz, 0, UIH_STREAM_RATE_MAX, &v)) {
            return UIH_PROTO_ARG;
        }
        req->rate_hz = (uint8_t) v;
    }
    return UIH_PROTO_OK;
}

// --- writer -------------------------------------------------------------------

static void w_putn(uih_writer_s * w, const char * s, size_t n) {
    if (w->overflow) {
        return;
    }
    if ((w->len + n + 1) > w->size) {  // keep room for the NUL
        w->overflow = true;
        return;
    }
    memcpy(w->buf + w->len, s, n);
    w->len += n;
    w->buf[w->len] = 0;
}

static void w_puts(uih_writer_s * w, const char * s) {
    w_putn(w, s, strlen(s));
}

static void w_printf_u(uih_writer_s * w, const char * fmt, unsigned long value) {
    char tmp[24];
    int n = snprintf(tmp, sizeof(tmp), fmt, value);
    w_putn(w, tmp, (size_t) n);
}

static void w_string(uih_writer_s * w, const char * s) {
    w_puts(w, "\"");
    for (; *s; ++s) {
        unsigned char ch = (unsigned char) *s;
        if ((ch == '"') || (ch == '\\')) {
            char esc[2] = {'\\', (char) ch};
            w_putn(w, esc, 2);
        } else if (ch < 0x20) {
            w_printf_u(w, "\\u%04lx", ch);
        } else {
            w_putn(w, (const char *) &ch, 1);
        }
    }
    w_puts(w, "\"");
}

static void w_key(uih_writer_s * w, const char * key) {
    if (!w->first) {
        w_puts(w, ",");
    }
    w->first = false;
    w_string(w, key);
    w_puts(w, ":");
}

static void w_begin(uih_writer_s * w, char * buf, size_t size) {
    w->buf = buf;
    w->size = size;
    w->len = 0;
    w->overflow = (size == 0);
    w->first = true;
    if (size) {
        buf[0] = 0;
    }
    w_puts(w, "{");
}

void uih_msg_str(uih_writer_s * w, const char * key, const char * value) {
    w_key(w, key);
    w_string(w, value);
}

void uih_msg_u32(uih_writer_s * w, const char * key, uint32_t value) {
    w_key(w, key);
    w_printf_u(w, "%lu", value);
}

void uih_msg_bool(uih_writer_s * w, const char * key, bool value) {
    w_key(w, key);
    w_puts(w, value ? "true" : "false");
}

static void w_i32(uih_writer_s * w, int32_t value) {
    if (value < 0) {
        w_puts(w, "-");
        w_printf_u(w, "%lu", (unsigned long) (-(int64_t) value));
    } else {
        w_printf_u(w, "%lu", (unsigned long) value);
    }
}

static void w_tenths(uih_writer_s * w, int32_t value_x10) {
    int64_t v = value_x10;
    if (v < 0) {
        w_puts(w, "-");
        v = -v;
    }
    w_printf_u(w, "%lu", (unsigned long) (v / 10));
    w_puts(w, ".");
    w_printf_u(w, "%lu", (unsigned long) (v % 10));
}

void uih_msg_ports(uih_writer_s * w, const char * key, const uih_port_state_s * ports, size_t count) {
    w_key(w, key);
    w_puts(w, "[");
    for (size_t i = 0; i < count; ++i) {
        const uih_port_state_s * p = &ports[i];
        if (i) {
            w_puts(w, ",");
        }
        w_printf_u(w, "{\"port\":%lu", p->port);
        w_puts(w, p->power ? ",\"power\":true" : ",\"power\":false");
        w_puts(w, p->data ? ",\"data\":true" : ",\"data\":false");
        w_puts(w, p->fault ? ",\"fault\":true" : ",\"fault\":false");
        w_puts(w, ",\"mV\":");
        w_i32(w, p->mv);
        w_puts(w, ",\"mA\":");
        w_tenths(w, p->ma_x10);
        w_puts(w, "}");
    }
    w_puts(w, "]");
}

void uih_msg_aux(uih_writer_s * w, const uih_aux_s * aux) {
    w_key(w, "aux");
    w_puts(w, aux->vhost ? "{\"vhost\":true" : "{\"vhost\":false");
    w_puts(w, aux->vext ? ",\"vext\":true" : ",\"vext\":false");
    w_puts(w, aux->usb3 ? ",\"usb3\":true}" : ",\"usb3\":false}");
}

void uih_msg_info(uih_writer_s * w, const uih_info_s * info) {
    uih_msg_str(w, "fw", info->fw);
    uih_msg_str(w, "git", info->git);
    uih_msg_u32(w, "proto", UIH_PROTO_VERSION);
    uih_msg_str(w, "idf", info->idf);
    uih_msg_str(w, "serial", info->serial);
    w_key(w, "stm8");
    if (info->stm8_version < 0) {
        w_puts(w, "null");
    } else {
        w_i32(w, info->stm8_version);
    }
    uih_msg_bool(w, "meter", info->meter);
    uih_msg_str(w, "display", info->display ? info->display : "starting");
    uih_msg_str(w, "reset", info->reset);
    uih_msg_u32(w, "uptime_ms", info->uptime_ms);
}

void uih_msg_rsp_begin(uih_writer_s * w, char * buf, size_t size, uint32_t seq,
                       const char * op, const char * error_code) {
    w_begin(w, buf, size);
    uih_msg_str(w, "type", "rsp");
    uih_msg_u32(w, "seq", seq);
    uih_msg_str(w, "op", op);
    if (error_code) {
        uih_msg_str(w, "status", "error");
        uih_msg_str(w, "code", error_code);
    } else {
        uih_msg_str(w, "status", "ok");
    }
}

void uih_msg_event_begin(uih_writer_s * w, char * buf, size_t size, uint32_t n,
                         uint32_t t_ms, const char * event) {
    w_begin(w, buf, size);
    uih_msg_str(w, "type", "event");
    uih_msg_u32(w, "n", n);
    uih_msg_u32(w, "t", t_ms);
    uih_msg_str(w, "event", event);
}

void uih_msg_end(uih_writer_s * w) {
    w_puts(w, "}");
    if (!w->overflow) {
        uint32_t crc = uih_crc32((const uint8_t *) w->buf, w->len);
        w_printf_u(w, "*%08lx\n", crc);
    }
    if (w->overflow) {
        w->len = 0;
        if (w->size) {
            w->buf[0] = 0;
        }
    }
}

void uih_msg_status(uih_writer_s * w, char * buf, size_t size, uint32_t n, uint32_t t_ms,
                    const uih_port_state_s * ports, size_t count) {
    w_begin(w, buf, size);
    uih_msg_str(w, "type", "status");
    uih_msg_u32(w, "n", n);
    uih_msg_u32(w, "t", t_ms);
    uih_msg_ports(w, "ports", ports, count);
    uih_msg_end(w);
}

void uih_msg_error(uih_writer_s * w, char * buf, size_t size, uih_proto_code_e code,
                   bool seq_valid, uint32_t seq) {
    w_begin(w, buf, size);
    uih_msg_str(w, "type", "error");
    uih_msg_str(w, "code", uih_proto_code_str(code));
    if (seq_valid) {
        uih_msg_u32(w, "seq", seq);
    }
    uih_msg_end(w);
}
