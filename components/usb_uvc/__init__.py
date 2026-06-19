import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components.esp32 import add_idf_component, only_on_variant
from esphome.components.esp32 import VARIANT_ESP32P4, VARIANT_ESP32S2, VARIANT_ESP32S3
from esphome.components.usb_host import USBClient, register_usb_client

CODEOWNERS = ["@p1ngb4ck"]
DEPENDENCIES = ["usb_host", "esp_video"]

CONF_VID = "vid"
CONF_PID = "pid"
CONF_UVC_DEV_NUM = "uvc_dev_num"
CONF_TASK_STACK = "task_stack"
CONF_TASK_PRIORITY = "task_priority"
CONF_TASK_AFFINITY = "task_affinity"

usb_uvc_ns = cg.esphome_ns.namespace("usb_uvc")
UsbUvcComponent = usb_uvc_ns.class_("UsbUvcComponent", USBClient)

# Schema for a single UVC camera entry.
# VID/PID default to 0 (wildcard) — matches any UVC class device.
# Specify both to target a particular camera model by vendor/product ID.
_SINGLE_SCHEMA = cv.All(
    cv.COMPONENT_SCHEMA.extend(
        {
            cv.GenerateID(): cv.declare_id(UsbUvcComponent),
            cv.Optional(CONF_VID, default=0): cv.hex_uint16_t,
            cv.Optional(CONF_PID, default=0): cv.hex_uint16_t,
            cv.Optional(CONF_UVC_DEV_NUM, default=1): cv.int_range(min=1, max=10),
            cv.Optional(CONF_TASK_STACK, default=4096): cv.positive_int,
            cv.Optional(CONF_TASK_PRIORITY, default=5): cv.int_range(min=1, max=24),
            cv.Optional(CONF_TASK_AFFINITY, default=-1): cv.int_range(min=-1, max=1),
        }
    ),
    only_on_variant(supported=[VARIANT_ESP32P4, VARIANT_ESP32S2, VARIANT_ESP32S3]),
)

# Allow one or multiple UVC camera entries under the usb_uvc: key.
CONFIG_SCHEMA = cv.ensure_list(_SINGLE_SCHEMA)


async def to_code(config):
    # Pull the Espressif USB Host UVC driver from the ESP Component Registry.
    # Provides usb/uvc_host.h and the UVC device enumeration stack.
    # Only this component pulls it — esp_video does not.
    add_idf_component(name="espressif/usb_host_uvc", ref="2.4.1")

    for device in config:
        var = await register_usb_client(device)
        cg.add(var.set_uvc_dev_num(device[CONF_UVC_DEV_NUM]))
        cg.add(var.set_task_stack(device[CONF_TASK_STACK]))
        cg.add(var.set_task_priority(device[CONF_TASK_PRIORITY]))
        cg.add(var.set_task_affinity(device[CONF_TASK_AFFINITY]))
