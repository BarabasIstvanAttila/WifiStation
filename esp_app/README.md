# Kconfig.projbuild
This is ESP-IDF's project-level extension to the Kconfig configuration language. When you run idf.py menuconfig, ESP-IDF scans every component for Kconfig files and it specifically scans the main/ folder for Kconfig.projbuild. Everything inside it appears as a new menu in the interactive configuration UI. Each config entry generates a CONFIG_* macro that lands in the auto-generated sdkconfig.h — so C code can read user choices with #ifdef CONFIG_FOO.
The key property: it defines the interface between the developer and the build system. You never hardcode credentials or hardware choices in source; the user sets them in menuconfig and the build system injects them as macros.
# sdkconfig.defaults
This is a seed file, not a lock file. It contains the values you want pre-filled the first time someone clones the repo and builds — before they've ever run menuconfig. After the first build, the real sdkconfig (git-ignored) takes over and sdkconfig.defaults no longer overrides anything the developer has changed. It is safe to commit to version control. Its role is: "here are the right hardware and OS settings for this specific project; a generic ESP-IDF default won't work."

---

# Project setup
### Kconfig.projbuild 
Exposes WiFi credentials, camera board, frame size, JPEG quality, FB count, HTTP port, and all power-save options as proper menuconfig entries
### sdkconfig.defaults
Sets PSRAM, 160 MHz CPU, WiFi IRAM opts, lwIP buffer tuning, HTTP server limits, watchdog timeout, and all power management flags — with a rationale comment on every line.
### app_config.h 
All #defines now map CONFIG_* macros to module types. The #if defined(CONFIG_APP_CAMERA_BOARD_*) chains translate Kconfig choices to camera_board_t enum values. Build fails with #error if required choices are missing.
### app_main.c 
Added #ifdef CONFIG_PM_ENABLE block that calls esp_pm_configure() with APP_MAX/MIN_CPU_FREQ_MHZ, and esp_wifi_set_ps(APP_WIFI_PS_MODE) after successful WiFi init. Both compile away cleanly when PM is disabled.
