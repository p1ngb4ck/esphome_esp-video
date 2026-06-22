"""
ESPHome component for Espressif ESP-Video (v1.4.0)
JPEG support with ESP-IDF dependencies

Initializes ESP-Video using the ESPHome I2C bus.
"""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import i2c, esp32
from esphome.components.esp32 import add_idf_component
from esphome.const import CONF_ID, CONF_I2C_ID
from esphome.core import CORE
import os
import logging

CODEOWNERS = ["@youkorr"]
DEPENDENCIES = ["esp32", "i2c"]
AUTO_LOAD = []

esp_video_ns = cg.esphome_ns.namespace("esp_video")
ESPVideoComponent = esp_video_ns.class_("ESPVideoComponent", cg.Component)

# Configuration keys
CONF_ENABLE_JPEG = "enable_jpeg"
CONF_ENABLE_ISP = "enable_isp"
CONF_ENABLE_UVC = "enable_uvc"
CONF_USE_HEAP_ALLOCATOR = "use_heap_allocator"
CONF_XCLK_PIN = "xclk_pin"
CONF_XCLK_FREQ = "xclk_freq"
CONF_ENABLE_XCLK_INIT = "enable_xclk_init"

# Use xclk_pin: -1 for boards with an external oscillator on the PCB
NO_CLOCK = -1

def parse_gpio_pin(value):
    """Parse a GPIO pin in ESPHome format (GPIO36 or -1)."""
    if isinstance(value, int):
        return value
    if isinstance(value, str):
        if value == "-1" or value.upper() == "NO_CLOCK":
            return NO_CLOCK
        # "GPIO36" -> 36
        if value.upper().startswith("GPIO"):
            try:
                return int(value[4:])
            except ValueError:
                raise cv.Invalid(f"Invalid GPIO format: {value}. Use 'GPIO36' or -1")
        try:
            return int(value)
        except ValueError:
            raise cv.Invalid(f"Invalid GPIO format: {value}. Use 'GPIO36' or -1")
    raise cv.Invalid(f"Invalid pin type: {type(value)}")

def validate_esp_video_config(config):
    """Validate ESP-Video configuration."""
    return config


CONFIG_SCHEMA = cv.All(
    cv.Schema({
        cv.GenerateID(): cv.declare_id(ESPVideoComponent),
        cv.Required(CONF_I2C_ID): cv.use_id(i2c.I2CBus),
        cv.Optional(CONF_ENABLE_JPEG, default=True): cv.boolean,
        cv.Optional(CONF_ENABLE_ISP, default=True): cv.boolean,
        # USB-UVC host: enables plugging an external USB (UVC) camera into the
        # ESP32-P4 USB OTG port. Off by default (MIPI-CSI only). When true, the
        # USB host stack + UVC host driver are compiled in and started, and a
        # connected UVC camera is enumerated as a /dev/videoN V4L2 device.
        cv.Optional(CONF_ENABLE_UVC, default=False): cv.boolean,
        cv.Optional(CONF_USE_HEAP_ALLOCATOR, default=True): cv.boolean,
        # XCLK pin accepts: "GPIO36", 36, -1, or "NO_CLOCK"
        cv.Optional(CONF_XCLK_PIN, default="GPIO36"): cv.Any(cv.string, cv.int_range(min=-1, max=48)),
        cv.Optional(CONF_XCLK_FREQ, default=24000000): cv.int_range(min=1000000, max=40000000),  # 1-40 MHz
        # Enable XCLK initialization via LEDC (for non-M5Stack boards)
        cv.Optional(CONF_ENABLE_XCLK_INIT, default=False): cv.boolean,
    }).extend(cv.COMPONENT_SCHEMA),
    validate_esp_video_config
)


async def to_code(config):
    # ESP-IDF is required; Arduino framework is not supported
    if CORE.using_arduino:
        raise cv.Invalid(
            "ESP-Video requires the esp-idf framework. "
            "Add 'framework: type: esp-idf' to your configuration."
        )

    component_dir = os.path.dirname(__file__)
    parent_components_dir = os.path.dirname(component_dir)

    # Auto-download missing dependencies
    from .esp_video_download import ensure_esp_video_dependencies

    try:
        ensure_esp_video_dependencies(parent_components_dir)
    except Exception as e:
        logging.warning(
            f"[ESP-Video] Auto-download failed: {e}\n"
            f"If components are already present locally, this is OK."
        )

    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    i2c_bus = await cg.get_variable(config[CONF_I2C_ID])
    cg.add(var.set_i2c_bus(i2c_bus))

    # CRITICAL: sensors need an active XCLK to respond on I2C
    xclk_pin_raw = config[CONF_XCLK_PIN]
    xclk_pin = parse_gpio_pin(xclk_pin_raw)
    xclk_freq = config[CONF_XCLK_FREQ]
    has_ext_clock = xclk_pin != NO_CLOCK

    cg.add(var.set_xclk_pin(cg.RawExpression(f"static_cast<gpio_num_t>({xclk_pin})")))
    cg.add(var.set_xclk_freq(xclk_freq))
    cg.add(var.set_enable_xclk_init(config[CONF_ENABLE_XCLK_INIT]))
    cg.add(var.set_enable_uvc(config[CONF_ENABLE_UVC]))

    # USB-UVC: the usb_uvc component owns USB host init and pulls the IDF UVC
    # driver. esp_video only validates that usb_uvc is present in the config.
    if config[CONF_ENABLE_UVC]:
        if "usb_uvc" not in CORE.config:
            raise cv.Invalid(
                "enable_uvc: true requires a 'usb_uvc:' entry in your configuration. "
                "The usb_uvc component manages USB host and UVC driver initialization."
            )

    logging.debug(f"[ESP-Video] I2C bus: '{config[CONF_I2C_ID]}'")
    if has_ext_clock:
        logging.debug(f"[ESP-Video] XCLK: GPIO{xclk_pin} @ {xclk_freq/1000000:.1f} MHz")
    else:
        logging.debug(f"[ESP-Video] XCLK: PCB oscillator @ {xclk_freq/1000000:.1f} MHz")

    # Register local components as IDF components via override_path.
    # This replaces the PlatformIO SCons build script — IDF/cmake picks up
    # each component's CMakeLists.txt, include dirs, and sources automatically.
    for comp_name in ("esp_video", "esp_cam_sensor", "esp_ipa", "esp_sccb_intf"):
        comp_path = os.path.join(parent_components_dir, comp_name)
        if os.path.exists(comp_path):
            add_idf_component(name=comp_name, override_path=comp_path)


    # Build flags
    flags = []

    # Base flags (always enabled)
    flags.extend([
        "-DCONFIG_ESP_VIDEO_ENABLE_MIPI_CSI_VIDEO_DEVICE=1",
        "-DCONFIG_IDF_TARGET_ESP32P4=1",
        "-DCONFIG_SOC_I2C_SUPPORTED=1",
    ])

    # Camera sensors — auto-detection will probe all and use whichever responds

    # SC202CS — matches M5Stack Tab5 configuration
    # Digital gain priority recommended to reduce noise in low light
    flags.extend([
        "-DCONFIG_CAMERA_SC202CS=1",
        "-DCONFIG_CAMERA_SC202CS_AUTO_DETECT=1",
        "-DCONFIG_CAMERA_SC202CS_AUTO_DETECT_MIPI_INTERFACE_SENSOR=1",
        "-DCONFIG_CAMERA_SC202CS_ABSOLUTE_GAIN_LIMIT=63008",  # M5Stack value
        "-DCONFIG_CAMERA_SC202CS_ANA_GAIN_PRIORITY=0",        # Disabled (M5Stack)
        "-DCONFIG_CAMERA_SC202CS_DIG_GAIN_PRIORITY=1",        # Enabled (M5Stack)
        "-DCONFIG_CAMERA_SC202CS_MAX_SUPPORT=1",
    ])

    # OV5647
    flags.extend([
        "-DCONFIG_CAMERA_OV5647=1",
        "-DCONFIG_CAMERA_OV5647_AUTO_DETECT=1",
        "-DCONFIG_CAMERA_OV5647_AUTO_DETECT_MIPI_INTERFACE_SENSOR=1",
        "-DCONFIG_CAMERA_OV5647_CSI_LINESYNC_ENABLE=0",
        "-DCONFIG_CAMERA_OV5647_MIPI_IF_FORMAT_INDEX_DEFAULT=0",
        "-DCONFIG_CAMERA_OV5647_DEFAULT_IPA_JSON_CONFIGURATION_FILE=0",  # Disabled: CCM in JSON causes red tint (matrix amplifies red 2.0x)
    ])

    # OV02C10
    flags.extend([
        "-DCONFIG_CAMERA_OV02C10=1",
        "-DCONFIG_CAMERA_OV02C10_AUTO_DETECT=1",
        "-DCONFIG_CAMERA_OV02C10_AUTO_DETECT_MIPI_INTERFACE_SENSOR=1",
        "-DCONFIG_CAMERA_OV02C10_ABSOLUTE_GAIN_LIMIT=16000",  # 16x max
        "-DCONFIG_CAMERA_OV02C10_ANA_GAIN_PRIORITY=1",        # Analog gain priority
        "-DCONFIG_CAMERA_OV02C10_DIG_GAIN_PRIORITY=0",
        "-DCONFIG_CAMERA_OV02C10_CSI_LINESYNC_ENABLE=0",
        "-DCONFIG_CAMERA_OV02C10_MIPI_IF_FORMAT_INDEX_DEFAULT=0",
        "-DCONFIG_CAMERA_OV02C10_MAX_SUPPORT=1",
        "-DCONFIG_CAMERA_OV02C10_DEFAULT_IPA_JSON_CONFIGURATION_FILE=1",  # Use cfg/ov02c10_default.json
    ])

    # ISP (Image Signal Processor)
    if config[CONF_ENABLE_ISP]:
        flags.extend([
            "-DCONFIG_ESP_VIDEO_ENABLE_ISP=1",
            "-DCONFIG_ESP_VIDEO_ENABLE_ISP_VIDEO_DEVICE=1",
            "-DCONFIG_ESP_VIDEO_ENABLE_ISP_PIPELINE_CONTROLLER=1",
            "-DESP_VIDEO_ISP_ENABLED=1",  # Used in esp_video_component.cpp
        ])

    # USB-UVC host video device (external USB camera on the P4 USB OTG port)
    if config[CONF_ENABLE_UVC]:
        flags.append("-DCONFIG_ESP_VIDEO_ENABLE_USB_UVC_VIDEO_DEVICE=1")

    # Memory allocator
    if config[CONF_USE_HEAP_ALLOCATOR]:
        flags.append("-DCONFIG_ESP_VIDEO_USE_HEAP_ALLOCATOR=1")

    # JPEG encoder
    if config[CONF_ENABLE_JPEG]:
        flags.extend([
            "-DCONFIG_ESP_VIDEO_ENABLE_JPEG_VIDEO_DEVICE=1",
            "-DCONFIG_ESP_VIDEO_ENABLE_HW_JPEG_VIDEO_DEVICE=1",
            "-DESP_VIDEO_JPEG_ENABLED=1",  # Used in esp_video_component.cpp
        ])

    for flag in flags:
        cg.add_build_flag(flag)

    for flag in [
        "-Wno-unused-function",
        "-Wno-unused-variable",
        "-Wno-missing-field-initializers",
    ]:
        cg.add_build_flag(flag)

