#pragma once

#include <stdint.h>
#include "esp_err.h"
#include "usb/uvc_host.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Configuration passed to esp_video_install_usb_uvc_driver().
 * On return, event_cb and event_cb_ctx are filled in with the callback
 * and context that must be passed to uvc_host_install() so the UVC host
 * driver can signal the esp_video pipeline when a camera connects.
 */
typedef struct {
    // Inputs
    uint32_t uvc_dev_num;    /*!< Number of UVC video pipeline slots (usually 1) */
    uint32_t task_stack;     /*!< UVC host driver task stack size in bytes */
    uint8_t task_priority;   /*!< UVC host driver task priority */
    int8_t task_affinity;    /*!< UVC host driver task core affinity, -1 = no affinity */

    // Outputs — filled by esp_video_install_usb_uvc_driver()
    uvc_host_driver_event_callback_t event_cb;  /*!< Callback to pass to uvc_host_install() */
    void *event_cb_ctx;                   /*!< Context pointer for event_cb */
} esp_video_uvc_driver_config_t;

/**
 * Install the esp_video UVC pipeline driver.
 *
 * Registers /dev/video4x V4L2 slots for UVC streams and fills in
 * event_cb / event_cb_ctx in cfg so the caller can pass them to
 * uvc_host_install(). The USB host and uvc_host driver must be
 * started by the caller (usb_uvc component) AFTER this call.
 */
esp_err_t esp_video_install_usb_uvc_driver(esp_video_uvc_driver_config_t *cfg);

/**
 * Uninstall the esp_video UVC pipeline driver and release resources.
 */
esp_err_t esp_video_uninstall_usb_uvc_driver(void);

#ifdef __cplusplus
}
#endif
