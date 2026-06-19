#pragma once

#include <sys/queue.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"
#include "usb/usb_host.h"
#include "esphome/components/usb_uvc/include/usb/uvc_host.h"

/* Forward declaration — full definition is in uvc_types_priv.h */
struct uvc_host_stream_s;

typedef struct {
    usb_host_client_handle_t usb_client_hdl;
    SemaphoreHandle_t open_close_mutex;
    EventGroupHandle_t driver_status;
    usb_transfer_t *ctrl_transfer;
    SemaphoreHandle_t ctrl_mutex;
    uvc_host_driver_event_callback_t user_cb;
    void *user_ctx;
    SLIST_HEAD(list_dev, uvc_host_stream_s) uvc_stream_list;
} uvc_host_driver_t;

/* Defined in uvc_host.c (non-static), referenced by uvc_host_driver.c */
extern uvc_host_driver_t *p_uvc_host_driver;
