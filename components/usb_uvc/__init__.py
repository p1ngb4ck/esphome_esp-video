"""
ESPHome usb_uvc component.

Manages a UVC (USB Video Class) camera as a proper ESPHome USB device.
Depends on the usb_host component for USB host initialization and device
enumeration. Installs the uvc_host driver and registers the esp_video V4L2
pipeline — but never touches the USB host stack itself.
"""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import usb_host
from esphome.components.esp32 import add_idf_component

CODEOWNERS = ["@p1ngb4ck"]
DEPENDENCIES = ["usb_host", "esp_video"]
AUTO_LOAD = []

CONF_UVC_DEV_NUM = "uvc_dev_num"
CONF_TASK_STACK = "task_stack"
CONF_TASK_PRIORITY = "task_priority"
CONF_TASK_AFFINITY = "task_affinity"

usb_uvc_ns = cg.esphome_ns.namespace("usb_uvc")
UsbUvcComponent = usb_uvc_ns.class_("UsbUvcComponent", usb_host.USBClient)

CONFIG_SCHEMA = usb_host.usb_device_schema(cls=UsbUvcComponent).extend({
    cv.Optional(CONF_UVC_DEV_NUM, default=1): cv.int_range(min=1, max=10),
    cv.Optional(CONF_TASK_STACK, default=4096): cv.positive_int,
    cv.Optional(CONF_TASK_PRIORITY, default=5): cv.int_range(min=1, max=24),
    cv.Optional(CONF_TASK_AFFINITY, default=-1): cv.int_range(min=-1, max=1),
})


async def to_code(config):
    # Pull the Espressif USB Host UVC driver from the ESP Component Registry.
    # This provides usb/uvc_host.h and the UVC host stack.
    # Only this component pulls it — esp_video does not.
    add_idf_component(name="espressif/usb_host_uvc", ref="2.4.1")

    var = await usb_host.register_usb_client(config)

    cg.add(var.set_uvc_dev_num(config[CONF_UVC_DEV_NUM]))
    cg.add(var.set_task_stack(config[CONF_TASK_STACK]))
    cg.add(var.set_task_priority(config[CONF_TASK_PRIORITY]))
    cg.add(var.set_task_affinity(config[CONF_TASK_AFFINITY]))
