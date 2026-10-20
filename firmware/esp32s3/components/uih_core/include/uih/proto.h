/*
 * SPDX-FileCopyrightText: Copyright 2026 Jetperch LLC
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 *
 * @brief Protocol v1 framing, request parsing and message writing.
 *
 * See doc/protocol.md.  Messages are written into a caller buffer as one
 * complete framed line, including the trailing newline.  If the buffer is
 * too small, the writer sets overflow and leaves an empty buffer, so a
 * truncated line is never sent.
 */

#ifndef UIH_PROTO_H_
#define UIH_PROTO_H_

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define UIH_PROTO_VERSION (1)
#define UIH_PROTO_LINE_MAX (256)    ///< request line limit, excluding the line ending
#define UIH_PORT_COUNT (3)
#define UIH_PORT_ALL (0x07)         ///< port mask with all three ports
#define UIH_STREAM_RATE_MAX (50)

typedef enum {
    UIH_PROTO_OK = 0,
    UIH_PROTO_FORMAT,
    UIH_PROTO_CRC,
    UIH_PROTO_PARSE,
    UIH_PROTO_OVERFLOW,
    UIH_PROTO_ARG,
    UIH_PROTO_OP,
    UIH_PROTO_IO,
    UIH_PROTO_NOT_APPLIED,
    UIH_PROTO_BUSY,
} uih_proto_code_e;

typedef enum {
    UIH_OP_PING,
    UIH_OP_INFO,
    UIH_OP_STATUS,
    UIH_OP_SET,
    UIH_OP_STREAM,
    UIH_OP_BOOTLOADER,
    UIH_OP_RESET,
} uih_op_e;

typedef struct {
    uih_op_e op;
    char op_name[24];   ///< as received, for error responses
    uint32_t seq;
    bool seq_valid;
    uint8_t port_mask;  ///< set: bit n-1 selects port n
    int8_t power;       ///< set: -1 unchanged, 0 off, 1 on
    int8_t data;        ///< set: -1 unchanged, 0 off, 1 on
    uint8_t rate_hz;    ///< stream
} uih_request_s;

typedef struct {
    uint8_t port;       ///< 1..3
    bool power;
    bool data;
    bool fault;
    int32_t mv;
    int32_t ma_x10;     ///< current in 0.1 mA, positive flows to the device
} uih_port_state_s;

typedef struct {
    bool vhost;
    bool vext;
    bool usb3;
} uih_aux_s;

typedef struct {
    const char * fw;
    const char * git;
    const char * idf;
    const char * serial;
    int32_t stm8_version;   ///< -1 if the STM8 did not answer
    bool meter;
    const char * display;   ///< "starting", "ok" or "error: ..."
    const char * reset;
    uint32_t uptime_ms;
} uih_info_s;

typedef struct {
    char * buf;
    size_t size;
    size_t len;
    bool overflow;
    bool first;   ///< no field written yet at the current level
} uih_writer_s;

const char * uih_proto_code_str(uih_proto_code_e code);
const char * uih_op_str(uih_op_e op);

/**
 * @brief Split a framed line into its JSON and check the CRC.
 *
 * @param line The line, with or without its trailing CR/LF.
 * @return UIH_PROTO_OK, UIH_PROTO_FORMAT or UIH_PROTO_CRC.
 */
uih_proto_code_e uih_proto_frame_decode(const char * line, size_t len,
                                        const char ** json, size_t * json_len);

/**
 * @brief Parse and validate a request.
 *
 * @return UIH_PROTO_OK, or UIH_PROTO_PARSE, UIH_PROTO_ARG or UIH_PROTO_OP.
 *      req->seq_valid tells whether req->seq can be echoed.
 */
uih_proto_code_e uih_proto_request_parse(const char * json, size_t len, uih_request_s * req);

// Message writers.  Begin, add fields, then uih_msg_end() to close and frame.
void uih_msg_rsp_begin(uih_writer_s * w, char * buf, size_t size, uint32_t seq,
                       const char * op, const char * error_code);
void uih_msg_event_begin(uih_writer_s * w, char * buf, size_t size, uint32_t n,
                         uint32_t t_ms, const char * event);
void uih_msg_str(uih_writer_s * w, const char * key, const char * value);
void uih_msg_u32(uih_writer_s * w, const char * key, uint32_t value);
void uih_msg_bool(uih_writer_s * w, const char * key, bool value);
void uih_msg_ports(uih_writer_s * w, const char * key, const uih_port_state_s * ports, size_t count);
void uih_msg_aux(uih_writer_s * w, const uih_aux_s * aux);
void uih_msg_info(uih_writer_s * w, const uih_info_s * info);
void uih_msg_end(uih_writer_s * w);

// Complete messages.
void uih_msg_status(uih_writer_s * w, char * buf, size_t size, uint32_t n, uint32_t t_ms,
                    const uih_port_state_s * ports, size_t count);
void uih_msg_error(uih_writer_s * w, char * buf, size_t size, uih_proto_code_e code,
                   bool seq_valid, uint32_t seq);

#ifdef __cplusplus
}
#endif

#endif  /* UIH_PROTO_H_ */
