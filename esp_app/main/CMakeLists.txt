# =============================================================================
# main — single flat component containing every .c file of the project.
#
# Flat layout rules:
#   • Every .c file listed in SRCS lives directly in main/.
#   • Every .h file lives directly in main/ (no sub-include directories).
#   • INCLUDE_DIRS "." makes all headers in main/ visible to every SRCS file.
#   • The former components/ subdirectories are no longer used; delete them.
#
# PRIV_REQUIRES vs REQUIRES:
#   Nothing in the IDF build graph depends on `main`, so both keywords produce
#   identical results here.  PRIV_REQUIRES is used throughout to document that
#   these IDF components are consumed only by our implementation files, not
#   re-exported as a public API.
# =============================================================================

idf_component_register(
    SRCS
        "app_main.c"
        "camera_hal.c"
        "image_capture.c"
        "image_uploader.c"
        "upload_scheduler.c"
        "wifi_manager.c"
        "http_server.c"
        "ntp_sync.c"

    INCLUDE_DIRS "."


    PRIV_REQUIRES

        # ── Managed component (downloaded by idf_component.yml) ──────────────
        espressif__esp32-camera   # esp_camera.h             camera_hal.c
                                  #                           image_capture.c

        # ── Peripheral drivers ───────────────────────────────────────────────
        driver                    # driver/gpio.h, LEDC       camera_hal.c
                                  # NOTE: 'gpio' is NOT a standalone component;
                                  # GPIO lives inside 'driver'.

        esp_driver_gpio          # esp_driver_gpio.h         camera_hal.c

        # ── Timers ───────────────────────────────────────────────────────────
        esp_timer                 # esp_timer.h               image_capture.c

        # ── HTTP client + TLS ────────────────────────────────────────────────
        esp_http_client           # esp_http_client.h         image_uploader.c
        esp-tls                   # esp_crt_bundle.h          image_uploader.c
        mbedtls                   # mbedtls/base64.h          image_uploader.c

        # ── Network stack ────────────────────────────────────────────────────
        lwip                      # esp_sntp.h                sntp_sync.c
        esp_wifi                  # esp_wifi.h                wifi_manager.c
                                  #                           app_main.c
        esp_event                 # esp_event.h               wifi_manager.c
        esp_netif                 # esp_netif.h               wifi_manager.c

        # ── HTTP server ──────────────────────────────────────────────────────
        esp_http_server           # esp_http_server.h         http_server.c

        # ── Deep sleep + RTC memory ──────────────────────────────────────────
        #esp_sleep                 # esp_sleep.h               upload_scheduler.c
                                  # esp_attr.h (RTC_DATA_ATTR) is NOT a standalone
                                  # component; it comes from esp_common which
                                  # esp_sleep already pulls in transitively.

        # ── OS + memory ──────────────────────────────────────────────────────
        freertos                  # FreeRTOS.h, task.h        image_capture.c
                                  # event_groups.h            image_uploader.c
                                  #                           sntp_sync.c
                                  #                           wifi_manager.c
        heap                      # heap_caps_malloc           image_uploader.c
        vfs                       # virtual filesystem layer  (http_server.c)

        # ── System ───────────────────────────────────────────────────────────
        esp_pm                    # esp_pm.h                  app_main.c
        nvs_flash                 # nvs_flash.h               app_main.c 

)