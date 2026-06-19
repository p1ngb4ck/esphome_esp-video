#pragma once

#include <stdint.h>
#include "esp_err.h"
#include "usb/uvc_host.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Configuration passed to esp_video_install_usb_uvc_driver().
 * On return, event_cb and event_cb_ctx are filled in with the esp_video
 * pipeline callback that must be called when a UVC device is connected.
 */
typedef struct {
    // Inputs
    uint32_t uvc_dev_num;    /*!< Number of UVC video pipeline slots (usually 1) */
    uint32_t task_stack;     /*!< Reserved (unused — ESPHome drives the USB event loop) */
    uint8_t task_priority;   /*!< Reserved (unused) */
    int8_t task_affinity;    /*!< Reserved (unused) */

    // Outputs — filled by esp_video_install_usb_uvc_driver()
    uvc_host_driver_event_callback_t event_cb;  /*!< Call this when a UVC device connects */
    void *event_cb_ctx;                          /*!< Context for event_cb */
} esp_video_uvc_driver_config_t;

/**
 * Install the esp_video UVC pipeline driver.
 *
 * Creates /dev/videoX V4L2 slots for UVC streams and fills in
 * event_cb / event_cb_ctx. The caller is responsible for initialising
 * the uvc_host driver layer (uvc_host_driver_init()) using the existing
 * ESPHome USB host client handle.
 */
esp_err_t esp_video_install_usb_uvc_driver(esp_video_uvc_driver_config_t *cfg);

/**
 * Uninstall the esp_video UVC pipeline driver and release resources.
 */
esp_err_t esp_video_uninstall_usb_uvc_driver(void);

/**
 * Initialise the uvc_host driver internals using an existing USB host client
 * handle (already registered by ESPHome's usb_host component).
 *
 * This replaces uvc_host_install(): it allocates and populates the internal
 * uvc_host_driver_t, wires up event_cb/event_cb_ctx, but does NOT call
 * usb_host_client_register() — the provided handle is used directly.
 *
 * Must be called after esp_video_install_usb_uvc_driver() and after the
 * USB host client handle is registered.
 */
esp_err_t uvc_host_driver_init(usb_host_client_handle_t client_hdl,
                               uvc_host_driver_event_callback_t event_cb,
                               void *user_ctx);

/**
 * Tear down the uvc_host driver internals (counterpart to uvc_host_driver_init).
 * Does NOT deregister the USB client handle — that is ESPHome's responsibility.
 */
esp_err_t uvc_host_driver_deinit(void);

#ifdef __cplusplus
}
#endif
