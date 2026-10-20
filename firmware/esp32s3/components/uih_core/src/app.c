/*
 * SPDX-FileCopyrightText: Copyright 2026 Jetperch LLC
 * SPDX-License-Identifier: Apache-2.0
 */

#include "uih/app.h"
#include <string.h>

static uint32_t now_ms(uih_app_s * app) {
    return app->hub->ops.time_ms(app->hub->ops.ctx);
}

static void send(uih_app_s * app, const uih_writer_s * w, bool droppable) {
    if (w->len) {
        app->pf.tx(app->pf.ctx, w->buf, w->len, droppable);
    }
}

static void on_hub_event(void * ctx, const uih_hub_event_s * ev) {
    uih_app_s * app = (uih_app_s *) ctx;
    uih_writer_s w;
    static const char * const NAMES[] = {
        [UIH_HUB_EVENT_BUTTON] = "button",
        [UIH_HUB_EVENT_FAULT] = "fault",
        [UIH_HUB_EVENT_STM8_RESET] = "stm8_reset",
    };
    uih_msg_event_begin(&w, app->buf, sizeof(app->buf), app->event_n++, now_ms(app), NAMES[ev->type]);
    switch (ev->type) {
        case UIH_HUB_EVENT_BUTTON:
            uih_msg_u32(&w, "port", ev->port);
            uih_msg_bool(&w, "power", ev->value);
            break;
        case UIH_HUB_EVENT_FAULT:
            uih_msg_u32(&w, "port", ev->port);
            uih_msg_bool(&w, "fault", ev->value);
            break;
        case UIH_HUB_EVENT_STM8_RESET:
            uih_msg_bool(&w, "defaults", true);
            break;
    }
    uih_msg_end(&w);
    send(app, &w, false);
}

void uih_app_init(uih_app_s * app, uih_hub_s * hub, const uih_app_platform_s * platform) {
    memset(app, 0, sizeof(*app));
    app->pf = *platform;
    app->hub = hub;
    app->reset_reason = "other";
    hub->ops.event = on_hub_event;
    hub->ops.event_ctx = app;
}

void uih_app_boot(uih_app_s * app, bool poweron_reset, const char * reset_reason) {
    app->reset_reason = reset_reason;
    bool defaults = uih_hub_boot(app->hub, poweron_reset);
    app->last_poll_ms = now_ms(app);
    uih_writer_s w;
    uih_msg_event_begin(&w, app->buf, sizeof(app->buf), app->event_n++, now_ms(app), "boot");
    uih_msg_str(&w, "reset", reset_reason);
    uih_msg_bool(&w, "defaults", defaults);
    uih_msg_end(&w);
    send(app, &w, false);
}

static void send_error(uih_app_s * app, uih_proto_code_e code, bool seq_valid, uint32_t seq) {
    uih_writer_s w;
    uih_msg_error(&w, app->buf, sizeof(app->buf), code, seq_valid, seq);
    send(app, &w, false);
}

void uih_app_overflow(uih_app_s * app) {
    send_error(app, UIH_PROTO_OVERFLOW, false, 0);
}

static void respond_error(uih_app_s * app, const uih_request_s * req, uih_proto_code_e code) {
    uih_writer_s w;
    uih_msg_rsp_begin(&w, app->buf, sizeof(app->buf), req->seq, req->op_name, uih_proto_code_str(code));
    uih_msg_end(&w);
    send(app, &w, false);
}

static void handle(uih_app_s * app, const uih_request_s * req) {
    uih_writer_s w;
    uih_port_state_s ports[UIH_PORT_COUNT];
    uih_aux_s aux;
    switch (req->op) {
        case UIH_OP_SET: {
            uih_proto_code_e rc = uih_hub_set(app->hub, req->port_mask, req->power, req->data);
            if (rc != UIH_PROTO_OK) {
                respond_error(app, req, rc);
                return;
            }
            uih_hub_snapshot(app->hub, ports, NULL);
            uih_port_state_s selected[UIH_PORT_COUNT];
            size_t count = 0;
            for (int i = 0; i < UIH_PORT_COUNT; ++i) {
                if (req->port_mask & (1u << i)) {
                    selected[count++] = ports[i];
                }
            }
            uih_msg_rsp_begin(&w, app->buf, sizeof(app->buf), req->seq, req->op_name, NULL);
            uih_msg_ports(&w, "ports", selected, count);
            break;
        }
        case UIH_OP_INFO: {
            uih_info_s info = app->pf.info;
            info.stm8_version = app->hub->stm8_version;
            info.meter = app->hub->meter_ok;
            info.reset = app->reset_reason;
            info.uptime_ms = now_ms(app);
            uih_msg_rsp_begin(&w, app->buf, sizeof(app->buf), req->seq, req->op_name, NULL);
            uih_msg_info(&w, &info);
            break;
        }
        case UIH_OP_STATUS:
            uih_hub_snapshot(app->hub, ports, &aux);
            uih_msg_rsp_begin(&w, app->buf, sizeof(app->buf), req->seq, req->op_name, NULL);
            uih_msg_ports(&w, "ports", ports, UIH_PORT_COUNT);
            uih_msg_aux(&w, &aux);
            break;
        case UIH_OP_STREAM:
            app->rate_hz = req->rate_hz;
            app->next_status_ms = now_ms(app) + (req->rate_hz ? 1000u / req->rate_hz : 0);
            uih_msg_rsp_begin(&w, app->buf, sizeof(app->buf), req->seq, req->op_name, NULL);
            uih_msg_u32(&w, "rate_hz", req->rate_hz);
            break;
        case UIH_OP_BOOTLOADER:
        case UIH_OP_RESET:
            app->action = (req->op == UIH_OP_BOOTLOADER) ? UIH_APP_ACTION_BOOTLOADER : UIH_APP_ACTION_RESET;
            app->action_ms = now_ms(app) + UIH_APP_ACTION_DELAY_MS;
            uih_msg_rsp_begin(&w, app->buf, sizeof(app->buf), req->seq, req->op_name, NULL);
            break;
        case UIH_OP_PING:
        default:
            uih_msg_rsp_begin(&w, app->buf, sizeof(app->buf), req->seq, req->op_name, NULL);
            break;
    }
    uih_msg_end(&w);
    send(app, &w, false);
}

void uih_app_line(uih_app_s * app, const char * line, size_t len) {
    if (!len) {
        return;
    }
    const char * json;
    size_t json_len;
    uih_proto_code_e rc = uih_proto_frame_decode(line, len, &json, &json_len);
    if (rc != UIH_PROTO_OK) {
        send_error(app, rc, false, 0);
        return;
    }
    uih_request_s req;
    rc = uih_proto_request_parse(json, json_len, &req);
    if (rc == UIH_PROTO_OP) {
        respond_error(app, &req, rc);
    } else if (rc != UIH_PROTO_OK) {
        send_error(app, rc, req.seq_valid, req.seq);
    } else {
        handle(app, &req);
    }
}

void uih_app_button(uih_app_s * app, uint8_t port) {
    uih_hub_toggle(app->hub, port);  // emits the button event
}

static bool due(uint32_t now, uint32_t when) {
    return (int32_t) (now - when) >= 0;
}

void uih_app_tick(uih_app_s * app) {
    uint32_t now = now_ms(app);
    if ((now - app->last_poll_ms) >= UIH_APP_POLL_MS) {
        app->last_poll_ms = now;
        uih_hub_poll(app->hub);
    }
    if (app->rate_hz && due(now, app->next_status_ms)) {
        uint32_t period = 1000u / app->rate_hz;
        app->next_status_ms += period;
        if (due(now, app->next_status_ms)) {
            app->next_status_ms = now + period;  // fell behind: do not burst
        }
        uih_port_state_s ports[UIH_PORT_COUNT];
        uih_hub_snapshot(app->hub, ports, NULL);
        uih_writer_s w;
        uih_msg_status(&w, app->buf, sizeof(app->buf), app->status_n++, now, ports, UIH_PORT_COUNT);
        send(app, &w, true);
    }
    if ((app->action != UIH_APP_ACTION_NONE) && due(now, app->action_ms)) {
        uih_app_action_e action = app->action;
        app->action = UIH_APP_ACTION_NONE;
        if ((action == UIH_APP_ACTION_BOOTLOADER) && app->pf.bootloader) {
            app->pf.bootloader(app->pf.ctx);
        } else if ((action == UIH_APP_ACTION_RESET) && app->pf.reset) {
            app->pf.reset(app->pf.ctx);
        }
    }
}
