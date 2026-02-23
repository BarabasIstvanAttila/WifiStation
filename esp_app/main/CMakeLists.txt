idf_component_register(SRCS "app_main.c"
                    "camera_hal.c"
                    "image_capture.c"
                    "image_uploader.c"
                    "upload_scheduler.c"
                    "http_server.c"
                    "wifi_manager.c"
                    PRIV_REQUIRES esp_driver_uart vfs esp_wifi esp_pm nvs_flash
                    INCLUDE_DIRS ".")
