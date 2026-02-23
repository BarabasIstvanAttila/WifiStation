#pragma once

/**
 * @file image_uploader.h
 * @brief Image upload module.
 *
 * Single Responsibility: transfer one JPEG frame to a remote HTTP/HTTPS
 * endpoint.  Knows nothing about schedules, cameras, or WiFi.
 *
 * Design constraints driven by the ESP32 memory model:
 *
 *   • PSRAM cannot be used for DMA or crypto operations.
 *   • The TLS stack (mbedTLS) needs ~40–80 KB of internal SRAM.
 *   • Frame buffers can be 50–300 KB (at UXGA JPEG).
 *   • We therefore NEVER copy the full frame into internal SRAM.
 *     Instead we stream the PSRAM buffer in small chunks directly into
 *     the TLS send buffer using esp_http_client_write() in a loop.
 *
 * Endpoint compatibility:
 *   • HTTP PUT  — works with AWS S3 presigned URLs, Azure Blob SAS URLs,
 *                 and any REST endpoint that accepts a raw body.
 *   • HTTP POST — works with custom servers and multipart-aware services.
 *
 * The caller owns the frame lifecycle; this module only reads frame->fb->buf.
 * The frame must remain valid for the duration of image_uploader_send().
 */

#include "esp_err.h"
#include "image_capture.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Configuration ────────────────────────────────────────────────────────── */

#define IMAGE_UPLOADER_URL_MAX_LEN      512
#define IMAGE_UPLOADER_MAX_CHUNK        4096   /* bytes read from PSRAM per write */
#define IMAGE_UPLOADER_CRED_MAX_LEN     128    /* max length for username or password */

typedef enum {
    UPLOAD_METHOD_PUT,   /* S3 presigned URL, Azure SAS, plain REST PUT */
    UPLOAD_METHOD_POST,  /* Custom server, multipart, form POST          */
} upload_method_t;

typedef struct {
    char            endpoint_url[IMAGE_UPLOADER_URL_MAX_LEN];
    upload_method_t method;
    size_t          chunk_size;      /* bytes per write call, ≤ IMAGE_UPLOADER_MAX_CHUNK */
    int             timeout_ms;      /* total HTTP transaction timeout                   */
    uint8_t         retry_count;     /* retries on transient failure                     */
    bool            skip_tls_verify; /* true only for development / self-signed certs    */

    /**
     * HTTP Basic Authentication credentials.
     * When username[0] != '\0' the uploader computes
     *   Authorization: Basic base64("<username>:<password>")
     * and adds it to every request.
     *
     * Leave username empty ("") to send no Authorization header.
     * Credentials are transmitted over TLS — never use Basic Auth over plain HTTP.
     */
    char            username[IMAGE_UPLOADER_CRED_MAX_LEN];
    char            password[IMAGE_UPLOADER_CRED_MAX_LEN];
} image_uploader_config_t;

/* ── Result descriptor ────────────────────────────────────────────────────── */

typedef struct {
    int      http_status;   /* HTTP response status code, -1 if not received */
    uint32_t bytes_sent;    /* JPEG bytes transferred over the wire           */
    uint8_t  attempts;      /* how many attempts were made (1 = first try OK) */
} upload_result_t;

/* ── Public API ───────────────────────────────────────────────────────────── */

/**
 * @brief Configure the uploader.  Must be called once before send.
 */
esp_err_t image_uploader_init(const image_uploader_config_t *cfg);

/**
 * @brief Upload @p frame to the configured endpoint.
 *
 * Streams frame->fb->buf in chunks.  Retries up to cfg->retry_count times
 * on connection errors or 5xx responses.
 *
 * @param[in]  frame      Borrowed frame (must stay valid during this call).
 * @param[out] result     Optional — pass NULL to discard.
 * @return ESP_OK on HTTP 2xx, ESP_FAIL otherwise.
 */
esp_err_t image_uploader_send(const capture_frame_t *frame,
                               upload_result_t       *result);

/**
 * @brief Release resources.  Safe to call even if init was never called.
 */
void image_uploader_deinit(void);

#ifdef __cplusplus
}
#endif