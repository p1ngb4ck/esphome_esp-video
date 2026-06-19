/*
 * Replaces uvc_host_install() for ESPHome integration.
 *
 * uvc_host_install() calls usb_host_client_register() which creates a second
 * USB client conflicting with ESPHome's usb_host component when other device
 * classes (e.g. usb_storage) are present.
 *
 * uvc_host_driver_init() populates the same uvc_host_driver_t that all
 * uvc_host_stream_* functions use, but uses the already-registered ESPHome
 * USBClient handle instead of registering a new one.
 */

#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"
#include "usb/usb_host.h"
#include "usb/uvc_host.h"
#include "uvc_driver_priv.h"
#include "esphome/components/usb_uvc/usb_uvc.h"

static const char *TAG = "uvc_host_driver";

/* ctrl_xfer_cb: mirrors ctrl_xfer_cb in uvc_host.c */
static void ctrl_xfer_cb(usb_transfer_t *transfer)
{
    SemaphoreHandle_t sem = (SemaphoreHandle_t) transfer->context;
    xSemaphoreGiveFromISR(sem, NULL);
}

esp_err_t uvc_host_driver_init(usb_host_client_handle_t client_hdl,
                               uvc_host_driver_event_callback_t event_cb,
                               void *user_ctx)
{
    if (p_uvc_host_driver != NULL) {
        ESP_LOGW(TAG, "Driver already initialised");
        return ESP_ERR_INVALID_STATE;
    }

    uvc_host_driver_t *drv = heap_caps_calloc(1, sizeof(uvc_host_driver_t),
                                               MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
    if (!drv) {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t ret = ESP_OK;

    drv->open_close_mutex = xSemaphoreCreateMutex();
    if (!drv->open_close_mutex) { ret = ESP_ERR_NO_MEM; goto fail_mutex1; }

    drv->driver_status = xEventGroupCreate();
    if (!drv->driver_status) { ret = ESP_ERR_NO_MEM; goto fail_eg; }

    drv->ctrl_mutex = xSemaphoreCreateMutex();
    if (!drv->ctrl_mutex) { ret = ESP_ERR_NO_MEM; goto fail_mutex2; }

    if (usb_host_transfer_alloc(64, 0, &drv->ctrl_transfer) != ESP_OK) {
        ret = ESP_ERR_NO_MEM;
        goto fail_transfer;
    }

    SemaphoreHandle_t ctrl_sem = xSemaphoreCreateBinary();
    if (!ctrl_sem) { ret = ESP_ERR_NO_MEM; goto fail_sem; }

    drv->ctrl_transfer->context         = ctrl_sem;
    drv->ctrl_transfer->bEndpointAddress = 0;
    drv->ctrl_transfer->timeout_ms      = 5000;
    drv->ctrl_transfer->callback        = ctrl_xfer_cb;

    drv->usb_client_hdl = client_hdl;
    drv->user_cb        = event_cb;
    drv->user_ctx       = user_ctx;
    SLIST_INIT(&drv->uvc_stream_list);

    __atomic_store_n(&p_uvc_host_driver, drv, __ATOMIC_SEQ_CST);

    ESP_LOGI(TAG, "UVC host driver initialised with ESPHome client handle");
    return ESP_OK;

fail_sem:
    usb_host_transfer_free(drv->ctrl_transfer);
fail_transfer:
    vSemaphoreDelete(drv->ctrl_mutex);
fail_mutex2:
    vEventGroupDelete(drv->driver_status);
fail_eg:
    vSemaphoreDelete(drv->open_close_mutex);
fail_mutex1:
    free(drv);
    return ret;
}

esp_err_t uvc_host_driver_deinit(void)
{
    uvc_host_driver_t *drv = __atomic_exchange_n(&p_uvc_host_driver, NULL, __ATOMIC_SEQ_CST);
    if (!drv) {
        return ESP_ERR_INVALID_STATE;
    }

    if (drv->ctrl_transfer) {
        SemaphoreHandle_t sem = (SemaphoreHandle_t) drv->ctrl_transfer->context;
        if (sem) {
            vSemaphoreDelete(sem);
        }
        usb_host_transfer_free(drv->ctrl_transfer);
    }
    if (drv->ctrl_mutex)       { vSemaphoreDelete(drv->ctrl_mutex); }
    if (drv->driver_status)    { vEventGroupDelete(drv->driver_status); }
    if (drv->open_close_mutex) { vSemaphoreDelete(drv->open_close_mutex); }

    free(drv);
    ESP_LOGI(TAG, "UVC host driver deinitialised");
    return ESP_OK;
}
