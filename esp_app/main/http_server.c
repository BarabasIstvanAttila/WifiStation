/**
 * @file http_server.c
 * @brief HTTP server implementation.
 */

#include "http_server.h"
#include "image_capture.h"
#include "camera_hal.h"

#include <stdio.h>
#include <string.h>
#include "esp_log.h"

static const char *TAG = "http_server";

/* ── Internal state ───────────────────────────────────────────────────────── */

static httpd_handle_t s_server = NULL;

/* ── Defaults ─────────────────────────────────────────────────────────────── */

static const http_server_config_t DEFAULT_CFG = {
    .port             = HTTP_SERVER_DEFAULT_PORT,
    .max_uri_handlers = 8,
    .stack_size       = 8192,
};

/* ── MJPEG stream helpers ─────────────────────────────────────────────────── */

/* Part header written before every MJPEG frame */
static const char STREAM_PART_HDR[] =
    "--" HTTP_SERVER_STREAM_BOUNDARY "\r\n"
    "Content-Type: image/jpeg\r\n"
    "Content-Length: %zu\r\n\r\n";

static esp_err_t send_mjpeg_frame(httpd_req_t *req, const capture_frame_t *f)
{
    char part_hdr[128];
    int  hdr_len = snprintf(part_hdr, sizeof(part_hdr),
                            STREAM_PART_HDR, f->fb->len);
    if (hdr_len < 0) return ESP_FAIL;

    if (httpd_resp_send_chunk(req, part_hdr, (ssize_t)hdr_len) != ESP_OK)
        return ESP_FAIL;
    if (httpd_resp_send_chunk(req, (const char *)f->fb->buf,
                              (ssize_t)f->fb->len) != ESP_OK)
        return ESP_FAIL;
    if (httpd_resp_send_chunk(req, "\r\n", 2) != ESP_OK)
        return ESP_FAIL;

    return ESP_OK;
}

/* ── Route handlers ───────────────────────────────────────────────────────── */

esp_err_t http_handler_capture(httpd_req_t *req)
{
    capture_frame_t frame;
    esp_err_t err = image_capture_acquire_frame(&frame);
    if (err != ESP_OK) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "image/jpeg");
    httpd_resp_set_hdr(req, "Content-Disposition",
                       "inline; filename=capture.jpg");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

    err = httpd_resp_send(req,
                          (const char *)frame.fb->buf,
                          (ssize_t)frame.fb->len);

    image_capture_return_frame(&frame);
    return err;
}

esp_err_t http_handler_stream(httpd_req_t *req)
{
    httpd_resp_set_type(req, HTTP_SERVER_STREAM_CONTENT_TYPE);
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "X-Framerate", "60");

    ESP_LOGI(TAG, "Stream started for client");

    while (true) {
        capture_frame_t frame;
        esp_err_t err = image_capture_acquire_frame(&frame);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Frame acquisition failed; stopping stream");
            break;
        }

        err = send_mjpeg_frame(req, &frame);
        image_capture_return_frame(&frame);

        if (err != ESP_OK) {
            ESP_LOGI(TAG, "Client disconnected");
            break;
        }
    }

    /* Zero-length chunk signals end of chunked response */
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

esp_err_t http_handler_status(httpd_req_t *req)
{
    char buf[256];
    int  len = snprintf(buf, sizeof(buf),
        "{"
        "\"camera_ready\":%s,"
        "\"frames_captured\":%"PRIu32","
        "\"server_running\":true"
        "}",
        camera_hal_is_ready() ? "true" : "false",
        image_capture_total_count());

    if (len < 0 || (size_t)len >= sizeof(buf)) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, buf, (ssize_t)len);
}

/* ── Lifecycle ────────────────────────────────────────────────────────────── */

esp_err_t http_server_start(const http_server_config_t *cfg,
                            const http_route_t         *routes,
                            size_t                      route_count)
{
    if (s_server) {
        ESP_LOGW(TAG, "Server already running");
        return ESP_OK;
    }
    if (!routes || route_count == 0) return ESP_ERR_INVALID_ARG;

    const http_server_config_t *c = cfg ? cfg : &DEFAULT_CFG;

    httpd_config_t drv_cfg  = HTTPD_DEFAULT_CONFIG();
    drv_cfg.server_port     = c->port;
    drv_cfg.max_uri_handlers= c->max_uri_handlers;
    drv_cfg.stack_size      = c->stack_size;

    esp_err_t err = httpd_start(&s_server, &drv_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start failed: %s", esp_err_to_name(err));
        return err;
    }

    /* Register every route in the table — O/C principle: add routes here */
    for (size_t i = 0; i < route_count; i++) {
        httpd_uri_t uri = {
            .uri     = routes[i].uri,
            .method  = routes[i].method,
            .handler = routes[i].handler,
            .user_ctx= NULL,
        };
        err = httpd_register_uri_handler(s_server, &uri);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to register route %s: %s",
                     routes[i].uri, esp_err_to_name(err));
            http_server_stop();
            return err;
        }
        ESP_LOGI(TAG, "Registered route: %s", routes[i].uri);
    }

    ESP_LOGI(TAG, "HTTP server running on port %d", c->port);
    return ESP_OK;
}

void http_server_stop(void)
{
    if (!s_server) return;
    httpd_stop(s_server);
    s_server = NULL;
    ESP_LOGI(TAG, "HTTP server stopped");
}

bool http_server_is_running(void) { return s_server != NULL; }