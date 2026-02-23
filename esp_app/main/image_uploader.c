/**
 * @file image_uploader.c
 * @brief Image upload — chunked PSRAM-to-HTTPS streaming implementation.
 *
 * Memory discipline:
 *   The internal SRAM scratch buffer is exactly chunk_size bytes (≤4096).
 *   Each iteration copies one chunk from PSRAM into this buffer, then hands
 *   the buffer to esp_http_client_write() which feeds it into the TLS stack.
 *   The TLS stack itself lives in internal SRAM (~60 KB typical with the
 *   ESP-IDF certificate bundle).  The JPEG data in PSRAM is never fully
 *   replicated in SRAM — only one chunk at a time.
 */

#include "image_uploader.h"

#include <string.h>
#include <stdio.h>
#include <inttypes.h>
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "mbedtls/base64.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "image_uploader";

/* ── Internal state ───────────────────────────────────────────────────────── */

static bool                    s_initialised = false;
static image_uploader_config_t s_cfg;

/* Scratch buffer in internal SRAM — allocated once, reused across retries. */
static uint8_t *s_chunk_buf  = NULL;
static size_t   s_chunk_size = 0;

/*
 * Pre-computed Basic Auth header value: "Basic <base64(user:pass)>"
 * Built once in image_uploader_init().  Empty string = no auth.
 *
 * Max size: "Basic " (6) + base64( 128 + ":" + 128 ) ≈ 6 + 344 = 350
 */
#define AUTH_HDR_MAX_LEN  360
static char s_auth_header[AUTH_HDR_MAX_LEN];

/* ── Helpers ──────────────────────────────────────────────────────────────── */

/*
 * Build the Basic Auth header value from username and password.
 * Writes into s_auth_header.  Clears the header if username is empty.
 */
static void build_auth_header(const char *username, const char *password)
{
    memset(s_auth_header, 0, sizeof(s_auth_header));

    if (!username || username[0] == '\0') {
        return; /* no auth — leave header empty */
    }

    /*
     * Credentials string: "username:password"
     * Maximum: 128 + 1 + 128 = 257 bytes + NUL
     */
    char creds[IMAGE_UPLOADER_CRED_MAX_LEN * 2 + 2];
    int  creds_len = snprintf(creds, sizeof(creds), "%s:%s",
                              username,
                              (password && password[0]) ? password : "");
    if (creds_len <= 0 || (size_t)creds_len >= sizeof(creds)) {
        ESP_LOGE(TAG, "Credential string too long");
        return;
    }

    /*
     * Base64-encode the credentials.
     * mbedtls_base64_encode writes output_len bytes and null-terminates
     * if there is room.
     */
    uint8_t b64_buf[300];
    size_t  b64_len = 0;

    int rc = mbedtls_base64_encode(b64_buf, sizeof(b64_buf), &b64_len,
                                   (const uint8_t *)creds, (size_t)creds_len);
    if (rc != 0) {
        ESP_LOGE(TAG, "Base64 encode failed: %d", rc);
        return;
    }

    int written = snprintf(s_auth_header, sizeof(s_auth_header),
                           "Basic %.*s", (int)b64_len, (char *)b64_buf);
    if (written <= 0 || (size_t)written >= sizeof(s_auth_header)) {
        ESP_LOGE(TAG, "Auth header buffer overflow");
        memset(s_auth_header, 0, sizeof(s_auth_header));
        return;
    }

    ESP_LOGI(TAG, "Basic Auth configured for user: %s", username);
}

static esp_http_client_method_t resolve_method(upload_method_t m)
{
    return (m == UPLOAD_METHOD_POST) ? HTTP_METHOD_POST : HTTP_METHOD_PUT;
}

/*
 * Perform one upload attempt.
 * Returns the HTTP status code on completion, or a negative esp_err_t value
 * on transport / TLS failure.
 */
static int attempt_upload(const capture_frame_t *frame, uint32_t *bytes_sent)
{
    *bytes_sent = 0;

    esp_http_client_config_t http_cfg = {
        .url                         = s_cfg.endpoint_url,
        .method                      = resolve_method(s_cfg.method),
        .timeout_ms                  = s_cfg.timeout_ms,
        .keep_alive_enable           = false,
        .crt_bundle_attach           = esp_crt_bundle_attach,
        .skip_cert_common_name_check = s_cfg.skip_tls_verify,
    };

    esp_http_client_handle_t client = esp_http_client_init(&http_cfg);
    if (!client) {
        ESP_LOGE(TAG, "esp_http_client_init failed");
        return -ESP_ERR_NO_MEM;
    }

    esp_http_client_set_header(client, "Content-Type", "image/jpeg");
    esp_http_client_set_header(client, "X-Source",     "esp32-camera");

    /*
     * Attach Basic Auth header only when credentials were configured.
     * The header was pre-computed in init so this is just a string copy.
     */
    if (s_auth_header[0] != '\0') {
        esp_http_client_set_header(client, "Authorization", s_auth_header);
    }

    /*
     * Declare exact Content-Length upfront.
     * Mandatory for S3 presigned PUT; avoids chunked transfer encoding.
     */
    esp_err_t err = esp_http_client_open(client, (int)frame->fb->len);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Connection open failed: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return -(int)err;
    }

    /* Stream the JPEG from PSRAM one chunk at a time into SRAM → TLS. */
    const uint8_t *src    = frame->fb->buf;
    size_t         remain = frame->fb->len;

    while (remain > 0) {
        size_t to_copy = (remain < s_chunk_size) ? remain : s_chunk_size;

        memcpy(s_chunk_buf, src, to_copy);

        int written = esp_http_client_write(client,
                                            (const char *)s_chunk_buf,
                                            (int)to_copy);
        if (written < 0) {
            ESP_LOGE(TAG, "Write error after %"PRIu32" bytes", *bytes_sent);
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
            return -ESP_FAIL;
        }

        src         += (size_t)written;
        remain      -= (size_t)written;
        *bytes_sent += (uint32_t)written;
    }

    int content_length = esp_http_client_fetch_headers(client);
    int status         = esp_http_client_get_status_code(client);

    ESP_LOGI(TAG, "Upload complete: status=%d content_length=%d bytes_sent=%"PRIu32,
             status, content_length, *bytes_sent);

    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return status;
}

/* ── Public API ───────────────────────────────────────────────────────────── */

esp_err_t image_uploader_init(const image_uploader_config_t *cfg)
{
    if (!cfg)                        return ESP_ERR_INVALID_ARG;
    if (cfg->endpoint_url[0] == '\0') return ESP_ERR_INVALID_ARG;

    if (s_initialised) {
        /* Re-init: free previous buffer and reconfigure. */
        free(s_chunk_buf);
        s_chunk_buf = NULL;
    }

    memcpy(&s_cfg, cfg, sizeof(s_cfg));

    s_chunk_size = (cfg->chunk_size > 0 && cfg->chunk_size <= IMAGE_UPLOADER_MAX_CHUNK)
                       ? cfg->chunk_size
                       : IMAGE_UPLOADER_MAX_CHUNK;

    /*
     * Allocate the SRAM scratch buffer once.
     * heap_caps_malloc with MALLOC_CAP_INTERNAL ensures this never lands
     * in PSRAM even when CONFIG_SPIRAM_USE_MALLOC=y.
     */
    s_chunk_buf = (uint8_t *)heap_caps_malloc(s_chunk_size,
                                               MALLOC_CAP_INTERNAL |
                                               MALLOC_CAP_8BIT);
    if (!s_chunk_buf) {
        ESP_LOGE(TAG, "Failed to allocate %zu-byte SRAM chunk buffer",
                 s_chunk_size);
        return ESP_ERR_NO_MEM;
    }

    s_initialised = true;
    build_auth_header(s_cfg.username, s_cfg.password);
    ESP_LOGI(TAG, "Uploader configured: url=%.60s method=%s chunk=%zu retry=%d auth=%s",
             s_cfg.endpoint_url,
             (s_cfg.method == UPLOAD_METHOD_POST) ? "POST" : "PUT",
             s_chunk_size,
             s_cfg.retry_count,
             s_auth_header[0] ? "yes" : "no");
    return ESP_OK;
}

esp_err_t image_uploader_send(const capture_frame_t *frame,
                               upload_result_t       *result)
{
    if (!s_initialised)      return ESP_ERR_INVALID_STATE;
    if (!frame || !frame->fb) return ESP_ERR_INVALID_ARG;

    upload_result_t local_result = { .http_status = -1, .bytes_sent = 0, .attempts = 0 };

    uint8_t max_attempts = (uint8_t)(s_cfg.retry_count + 1);

    for (uint8_t attempt = 1; attempt <= max_attempts; attempt++) {
        local_result.attempts = attempt;
        local_result.bytes_sent = 0;

        ESP_LOGI(TAG, "Upload attempt %d/%d — frame #%"PRIu32" (%zu bytes)",
                 attempt, max_attempts,
                 frame->sequence, frame->fb->len);

        int status = attempt_upload(frame, &local_result.bytes_sent);
        local_result.http_status = status;

        if (status >= 200 && status < 300) {
            ESP_LOGI(TAG, "Upload succeeded on attempt %d (HTTP %d)", attempt, status);
            if (result) *result = local_result;
            return ESP_OK;
        }

        if (attempt < max_attempts) {
            /* Exponential back-off: 2s, 4s, 8s … */
            uint32_t delay_ms = (uint32_t)(2000u << (attempt - 1));
            ESP_LOGW(TAG, "Attempt %d failed (status=%d); retrying in %"PRIu32" ms",
                     attempt, status, delay_ms);
            vTaskDelay(pdMS_TO_TICKS(delay_ms));
        }
    }

    ESP_LOGE(TAG, "All %d upload attempts failed", max_attempts);
    if (result) *result = local_result;
    return ESP_FAIL;
}

void image_uploader_deinit(void)
{
    if (s_chunk_buf) {
        free(s_chunk_buf);
        s_chunk_buf = NULL;
    }
    memset(s_auth_header, 0, sizeof(s_auth_header));
    s_initialised = false;
}