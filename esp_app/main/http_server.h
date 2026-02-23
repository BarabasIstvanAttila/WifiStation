#pragma once

/**
 * @file http_server.h
 * @brief HTTP server module.
 *
 * Single Responsibility: own the HTTP lifecycle and route registration.
 *
 * Route handlers are registered via a table (Open/Closed for C) — adding
 * a new endpoint never requires editing server internals, only the table
 * in the caller.
 *
 * The server does NOT know how to capture images; it receives a function
 * pointer to the capture logic (Dependency Inversion for C).
 */

#include "esp_err.h"
#include "esp_http_server.h"
#include "image_capture.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── Configuration ────────────────────────────────────────────────────────── */

#define HTTP_SERVER_DEFAULT_PORT          80
#define HTTP_SERVER_STREAM_BOUNDARY       "frame"
#define HTTP_SERVER_STREAM_CONTENT_TYPE   \
    "multipart/x-mixed-replace;boundary=" HTTP_SERVER_STREAM_BOUNDARY

typedef struct {
    uint16_t port;
    uint16_t max_uri_handlers;   /* default 8 */
    size_t   stack_size;         /* task stack, default 8192 */
} http_server_config_t;

/* ── Route descriptor (Open/Closed table entry) ───────────────────────────── */

typedef struct {
    const char       *uri;
    httpd_method_t    method;
    esp_err_t       (*handler)(httpd_req_t *req);
} http_route_t;

/* ── Public API ───────────────────────────────────────────────────────────── */

/**
 * @brief Start the HTTP server and register @p routes.
 *
 * @param cfg         Server configuration (NULL = defaults).
 * @param routes      Array of route descriptors.
 * @param route_count Number of entries in @p routes.
 */
esp_err_t http_server_start(const http_server_config_t *cfg,
                            const http_route_t         *routes,
                            size_t                      route_count);

/**
 * @brief Stop the server and release resources.
 */
void http_server_stop(void);

/**
 * @brief True if the server is currently running.
 */
bool http_server_is_running(void);

/* ── Built-in route handlers (register via the route table) ──────────────── */

/**
 * @brief JPEG snapshot handler — GET /capture
 *        Returns a single JPEG frame.
 */
esp_err_t http_handler_capture(httpd_req_t *req);

/**
 * @brief MJPEG stream handler — GET /stream
 *        Streams MJPEG until the client disconnects.
 */
esp_err_t http_handler_stream(httpd_req_t *req);

/**
 * @brief Status / health-check handler — GET /status
 *        Returns a JSON object with camera and capture stats.
 */
esp_err_t http_handler_status(httpd_req_t *req);

#ifdef __cplusplus
}
#endif