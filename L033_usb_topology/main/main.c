#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "usb/usb_host.h"

#define TAG "USB_TOPOLOGY"

#define USB_HOST_EN_GPIO -1
#define DEV_VBUS_EN_GPIO -1
#define BOOST_EN_GPIO -1
#define LIMIT_EN_GPIO -1

#define USB_BOOT_RESET_OFF_MS 800
#define USB_BOOT_RESET_SETTLE_MS 2500
#define DESCRIPTOR_FETCH_MAX_BYTES CONFIG_USB_HOST_CONTROL_TRANSFER_MAX_SIZE
#define MANUAL_DESCRIPTOR_FETCH_ENABLE 0

#define USB_DESC_TYPE_IAD 0x0B
#define USB_AUDIO_SUBCLASS_CONTROL 0x01
#define USB_AUDIO_SUBCLASS_STREAMING 0x02
#define USB_AUDIO_PROTOCOL_UNDEFINED 0x00
#define USB_AUDIO_PROTOCOL_UAC2 0x20
#define USB_AUDIO_CS_INTERFACE 0x24
#define USB_AUDIO_CS_ENDPOINT 0x25
#define USB_AUDIO_AC_HEADER 0x01
#define USB_AUDIO_AC_INPUT_TERMINAL 0x02
#define USB_AUDIO_AC_OUTPUT_TERMINAL 0x03
#define USB_AUDIO_AC_MIXER_UNIT 0x04
#define USB_AUDIO_AC_SELECTOR_UNIT 0x05
#define USB_AUDIO_AC_FEATURE_UNIT 0x06
#define USB_AUDIO_AC_PROCESSING_UNIT 0x07
#define USB_AUDIO_AC_EXTENSION_UNIT 0x08
#define USB_AUDIO_AC_CLOCK_SOURCE 0x0A
#define USB_AUDIO_AC_CLOCK_SELECTOR 0x0B
#define USB_AUDIO_AC_CLOCK_MULTIPLIER 0x0C
#define USB_AUDIO_AC_SAMPLE_RATE_CONVERTER 0x0D
#define USB_AUDIO_AS_GENERAL 0x01
#define USB_AUDIO_AS_FORMAT_TYPE 0x02
#define USB_AUDIO_AS_FORMAT_SPECIFIC 0x03
#define USB_AUDIO_EP_GENERAL 0x01

typedef struct {
    uint8_t klass;
    uint8_t subclass;
    uint8_t protocol;
    uint8_t number;
    uint8_t alternate;
} interface_context_t;

static usb_host_client_handle_t s_client_hdl;
static usb_device_handle_t s_device_hdl;
static uint8_t s_device_addr;

static uint16_t read_le16(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t read_le24(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16);
}

static const char *class_name(uint8_t klass)
{
    switch (klass) {
    case USB_CLASS_PER_INTERFACE: return "Per-interface";
    case USB_CLASS_AUDIO: return "Audio";
    case USB_CLASS_COMM: return "CDC/Communication";
    case USB_CLASS_HID: return "HID";
    case USB_CLASS_PHYSICAL: return "Physical";
    case USB_CLASS_STILL_IMAGE: return "Still image";
    case USB_CLASS_PRINTER: return "Printer";
    case USB_CLASS_MASS_STORAGE: return "Mass storage";
    case USB_CLASS_HUB: return "Hub";
    case USB_CLASS_CDC_DATA: return "CDC data";
    case USB_CLASS_VIDEO: return "Video";
    case USB_CLASS_VENDOR_SPEC: return "Vendor specific";
    default: return "Unknown";
    }
}

static const char *speed_name(usb_speed_t speed)
{
    switch (speed) {
    case USB_SPEED_LOW: return "Low speed 1.5 Mbps";
    case USB_SPEED_FULL: return "Full speed 12 Mbps";
    case USB_SPEED_HIGH: return "High speed 480 Mbps";
    default: return "Unknown";
    }
}

static const char *transfer_type_name(uint8_t attributes)
{
    switch (attributes & 0x03) {
    case USB_TRANSFER_TYPE_CTRL: return "Control";
    case USB_TRANSFER_TYPE_ISOCHRONOUS: return "Isochronous";
    case USB_TRANSFER_TYPE_BULK: return "Bulk";
    case USB_TRANSFER_TYPE_INTR: return "Interrupt";
    default: return "Unknown";
    }
}

static const char *isoc_sync_type_name(uint8_t attributes)
{
    switch ((attributes >> 2) & 0x03) {
    case 0: return "No sync";
    case 1: return "Asynchronous";
    case 2: return "Adaptive";
    case 3: return "Synchronous";
    default: return "Unknown";
    }
}

static const char *isoc_usage_type_name(uint8_t attributes)
{
    switch ((attributes >> 4) & 0x03) {
    case 0: return "Data";
    case 1: return "Feedback";
    case 2: return "Implicit feedback";
    case 3: return "Reserved";
    default: return "Unknown";
    }
}

static const char *audio_ac_subtype_name(uint8_t subtype)
{
    switch (subtype) {
    case USB_AUDIO_AC_HEADER: return "AC Header";
    case USB_AUDIO_AC_INPUT_TERMINAL: return "Input Terminal";
    case USB_AUDIO_AC_OUTPUT_TERMINAL: return "Output Terminal";
    case USB_AUDIO_AC_MIXER_UNIT: return "Mixer Unit";
    case USB_AUDIO_AC_SELECTOR_UNIT: return "Selector Unit";
    case USB_AUDIO_AC_FEATURE_UNIT: return "Feature Unit";
    case USB_AUDIO_AC_PROCESSING_UNIT: return "Processing Unit";
    case USB_AUDIO_AC_EXTENSION_UNIT: return "Extension Unit";
    case USB_AUDIO_AC_CLOCK_SOURCE: return "Clock Source";
    case USB_AUDIO_AC_CLOCK_SELECTOR: return "Clock Selector";
    case USB_AUDIO_AC_CLOCK_MULTIPLIER: return "Clock Multiplier";
    case USB_AUDIO_AC_SAMPLE_RATE_CONVERTER: return "Sample Rate Converter";
    default: return "Unknown AC";
    }
}

static const char *audio_as_subtype_name(uint8_t subtype)
{
    switch (subtype) {
    case USB_AUDIO_AS_GENERAL: return "AS General";
    case USB_AUDIO_AS_FORMAT_TYPE: return "Format Type";
    case USB_AUDIO_AS_FORMAT_SPECIFIC: return "Format Specific";
    default: return "Unknown AS";
    }
}

static const char *audio_protocol_name(uint8_t protocol)
{
    switch (protocol) {
    case USB_AUDIO_PROTOCOL_UNDEFINED: return "UAC1/undefined";
    case USB_AUDIO_PROTOCOL_UAC2: return "UAC2";
    default: return "Unknown audio protocol";
    }
}

static const char *audio_terminal_type_name(uint16_t terminal_type)
{
    switch (terminal_type) {
    case 0x0101: return "USB streaming";
    case 0x0201: return "Microphone";
    case 0x0301: return "Speaker";
    case 0x0302: return "Headphones";
    case 0x0603: return "Line connector";
    default: return "Unknown terminal";
    }
}

static const char *uac2_format_type_name(uint8_t format_type)
{
    switch (format_type) {
    case 0x01: return "FORMAT_TYPE_I";
    case 0x02: return "FORMAT_TYPE_II";
    case 0x03: return "FORMAT_TYPE_III";
    case 0x04: return "FORMAT_TYPE_IV";
    default: return "Unknown format type";
    }
}

static void print_uac2_type_i_formats(uint32_t formats)
{
    ESP_LOGI(TAG, "      Type I formats: PCM=%u PCM8=%u IEEE_FLOAT=%u ALAW=%u MULAW=%u RAW=%u bmFormats=0x%08" PRIX32,
             (formats & (1UL << 0)) ? 1 : 0,
             (formats & (1UL << 1)) ? 1 : 0,
             (formats & (1UL << 2)) ? 1 : 0,
             (formats & (1UL << 3)) ? 1 : 0,
             (formats & (1UL << 4)) ? 1 : 0,
             (formats & (1UL << 31)) ? 1 : 0,
             formats);
}

static void print_raw_descriptor(const uint8_t *d, uint8_t len)
{
    char line[128];
    size_t used = 0;
    for (uint8_t i = 0; i < len && used + 4 < sizeof(line); i++) {
        used += snprintf(&line[used], sizeof(line) - used, "%02X ", d[i]);
    }
    ESP_LOGI(TAG, "      raw: %s", line);
}

static void print_string_from_cached(const char *label, const usb_str_desc_t *desc)
{
    if (desc == NULL || desc->bLength < 2) {
        ESP_LOGI(TAG, "%s: <none>", label);
        return;
    }

    char text[96];
    size_t out = 0;
    const uint8_t chars = (desc->bLength - 2) / 2;
    for (uint8_t i = 0; i < chars && out + 1 < sizeof(text); i++) {
        uint16_t code = read_le16(&desc->val[2 + i * 2]);
        text[out++] = (code >= 0x20 && code < 0x7F) ? (char)code : '?';
    }
    text[out] = '\0';
    ESP_LOGI(TAG, "%s: %s", label, text);
}

static void print_uac2_ac_descriptor(const uint8_t *d, uint8_t len)
{
    const uint8_t subtype = d[2];
    switch (subtype) {
    case USB_AUDIO_AC_HEADER:
        if (len >= 9) {
            ESP_LOGI(TAG, "    CS Audio %s UAC2: ADC=%x.%02x category=%u totalLength=%u controls=0x%02X",
                     audio_ac_subtype_name(subtype),
                     read_le16(&d[3]) >> 8,
                     read_le16(&d[3]) & 0xFF,
                     d[5],
                     read_le16(&d[6]),
                     d[8]);
        }
        break;

    case USB_AUDIO_AC_CLOCK_SOURCE:
        if (len >= 8) {
            ESP_LOGI(TAG, "    CS Audio %s UAC2: clockID=%u attr=0x%02X controls=0x%02X assocTerminal=%u iClock=%u",
                     audio_ac_subtype_name(subtype), d[3], d[4], d[5], d[6], d[7]);
        }
        break;

    case USB_AUDIO_AC_CLOCK_SELECTOR:
        if (len >= 7) {
            const uint8_t inputs = d[4];
            ESP_LOGI(TAG, "    CS Audio %s UAC2: clockID=%u inputs=%u controls=0x%02X",
                     audio_ac_subtype_name(subtype), d[3], inputs,
                     (5 + inputs < len) ? d[5 + inputs] : 0);
            for (uint8_t i = 0; i < inputs && 5 + i < len; i++) {
                ESP_LOGI(TAG, "      clockSource[%u]=%u", i, d[5 + i]);
            }
        }
        break;

    case USB_AUDIO_AC_CLOCK_MULTIPLIER:
        if (len >= 7) {
            ESP_LOGI(TAG, "    CS Audio %s UAC2: clockID=%u sourceClock=%u controls=0x%02X iClock=%u",
                     audio_ac_subtype_name(subtype), d[3], d[4], d[5], d[6]);
        }
        break;

    case USB_AUDIO_AC_INPUT_TERMINAL:
        if (len >= 17) {
            ESP_LOGI(TAG, "    CS Audio %s UAC2: id=%u type=0x%04X (%s) assoc=%u clockSource=%u channels=%u channelConfig=0x%08" PRIX32 " controls=0x%04X",
                     audio_ac_subtype_name(subtype), d[3], read_le16(&d[4]),
                     audio_terminal_type_name(read_le16(&d[4])), d[6], d[7], d[8],
                     (uint32_t)d[9] | ((uint32_t)d[10] << 8) | ((uint32_t)d[11] << 16) | ((uint32_t)d[12] << 24),
                     read_le16(&d[14]));
        }
        break;

    case USB_AUDIO_AC_OUTPUT_TERMINAL:
        if (len >= 12) {
            ESP_LOGI(TAG, "    CS Audio %s UAC2: id=%u type=0x%04X (%s) assoc=%u source=%u clockSource=%u controls=0x%04X",
                     audio_ac_subtype_name(subtype), d[3], read_le16(&d[4]),
                     audio_terminal_type_name(read_le16(&d[4])), d[6], d[7], d[8], read_le16(&d[9]));
        }
        break;

    case USB_AUDIO_AC_FEATURE_UNIT:
        if (len >= 10) {
            const uint8_t control_sets = (len - 6) / 4;
            ESP_LOGI(TAG, "    CS Audio %s UAC2: unit=%u source=%u controlSets=%u iFeature=%u",
                     audio_ac_subtype_name(subtype), d[3], d[4], control_sets, d[len - 1]);
            for (uint8_t i = 0; i < control_sets; i++) {
                const uint8_t pos = 5 + i * 4;
                uint32_t controls = (uint32_t)d[pos] |
                                    ((uint32_t)d[pos + 1] << 8) |
                                    ((uint32_t)d[pos + 2] << 16) |
                                    ((uint32_t)d[pos + 3] << 24);
                ESP_LOGI(TAG, "      %s controls=0x%08" PRIX32,
                         i == 0 ? "master" : "channel", controls);
            }
        }
        break;

    case USB_AUDIO_AC_MIXER_UNIT:
    default:
        ESP_LOGI(TAG, "    CS Audio %s UAC2: subtype=0x%02X length=%u", audio_ac_subtype_name(subtype), subtype, len);
        print_raw_descriptor(d, len);
        break;
    }
}

static void print_uac2_as_descriptor(const uint8_t *d, uint8_t len)
{
    const uint8_t subtype = d[2];
    switch (subtype) {
    case USB_AUDIO_AS_GENERAL:
        if (len >= 16) {
            const uint32_t formats = (uint32_t)d[6] |
                                     ((uint32_t)d[7] << 8) |
                                     ((uint32_t)d[8] << 16) |
                                     ((uint32_t)d[9] << 24);
            ESP_LOGI(TAG, "    CS Audio %s UAC2: terminalLink=%u controls=0x%02X formatType=%u (%s) channels=%u channelConfig=0x%08" PRIX32,
                     audio_as_subtype_name(subtype), d[3], d[4], d[5],
                     uac2_format_type_name(d[5]), d[10],
                     (uint32_t)d[11] | ((uint32_t)d[12] << 8) | ((uint32_t)d[13] << 16) | ((uint32_t)d[14] << 24));
            print_uac2_type_i_formats(formats);
        }
        break;

    case USB_AUDIO_AS_FORMAT_TYPE:
        if (len >= 6) {
            ESP_LOGI(TAG, "    CS Audio %s UAC2: formatType=%u (%s) subslotBytes=%u bitResolution=%u",
                     audio_as_subtype_name(subtype), d[3], uac2_format_type_name(d[3]), d[4], d[5]);
        }
        break;

    default:
        ESP_LOGI(TAG, "    CS Audio %s UAC2: subtype=0x%02X length=%u", audio_as_subtype_name(subtype), subtype, len);
        print_raw_descriptor(d, len);
        break;
    }
}

static void print_uac2_cs_endpoint(const uint8_t *d, uint8_t len)
{
    if (len >= 8 && d[2] == USB_AUDIO_EP_GENERAL) {
        ESP_LOGI(TAG, "    CS Audio Endpoint UAC2: attributes=0x%02X controls=0x%02X lockDelayUnits=%u lockDelay=%u",
                 d[3], d[4], d[5], read_le16(&d[6]));
    } else {
        ESP_LOGI(TAG, "    CS Audio Endpoint UAC2: subtype=0x%02X length=%u", len >= 3 ? d[2] : 0, len);
        print_raw_descriptor(d, len);
    }
}

static void print_audio_class_specific_descriptor(const usb_standard_desc_t *desc, const interface_context_t *ctx)
{
    const uint8_t *d = desc->val;
    if (desc->bLength < 3 || ctx->klass != USB_CLASS_AUDIO) {
        print_raw_descriptor(d, desc->bLength);
        return;
    }

    if (desc->bDescriptorType == USB_AUDIO_CS_INTERFACE) {
        if (ctx->protocol == USB_AUDIO_PROTOCOL_UAC2 && ctx->subclass == USB_AUDIO_SUBCLASS_CONTROL) {
            print_uac2_ac_descriptor(d, desc->bLength);
        } else if (ctx->protocol == USB_AUDIO_PROTOCOL_UAC2 && ctx->subclass == USB_AUDIO_SUBCLASS_STREAMING) {
            print_uac2_as_descriptor(d, desc->bLength);
        } else if (ctx->subclass == USB_AUDIO_SUBCLASS_CONTROL) {
            ESP_LOGI(TAG, "    CS Audio UAC1/unknown AC parser not implemented for subtype=0x%02X length=%u", d[2], desc->bLength);
            print_raw_descriptor(d, desc->bLength);
        } else if (ctx->subclass == USB_AUDIO_SUBCLASS_STREAMING) {
            ESP_LOGI(TAG, "    CS Audio UAC1/unknown AS parser not implemented for subtype=0x%02X length=%u", d[2], desc->bLength);
            print_raw_descriptor(d, desc->bLength);
        } else {
            ESP_LOGI(TAG, "    CS Audio Interface: subclass=0x%02X subtype=0x%02X length=%u",
                     ctx->subclass, d[2], desc->bLength);
            print_raw_descriptor(d, desc->bLength);
        }
    } else {
        if (ctx->protocol == USB_AUDIO_PROTOCOL_UAC2) {
            print_uac2_cs_endpoint(d, desc->bLength);
        } else {
            ESP_LOGI(TAG, "    CS Audio Endpoint UAC1/unknown parser not implemented subtype=0x%02X length=%u",
                     d[2], desc->bLength);
            print_raw_descriptor(d, desc->bLength);
        }
    }
}

static void parse_config_descriptor(const char *source, const uint8_t *raw, uint16_t total_len)
{
    if (raw == NULL || total_len < USB_CONFIG_DESC_SIZE) {
        ESP_LOGW(TAG, "%s config descriptor too short: %u", source, total_len);
        return;
    }

    const usb_config_desc_t *config_desc = (const usb_config_desc_t *)raw;
    int offset = 0;
    interface_context_t ctx = {0};
    ESP_LOGI(TAG, "===== %s configuration descriptor =====", source);
    ESP_LOGI(TAG, "Config: value=%u interfaces=%u totalLength=%u attr=0x%02X maxPower=%umA",
             config_desc->bConfigurationValue,
             config_desc->bNumInterfaces,
             config_desc->wTotalLength,
             config_desc->bmAttributes,
             config_desc->bMaxPower * 2);

    while (offset + USB_STANDARD_DESC_SIZE <= total_len) {
        const usb_standard_desc_t *desc = (const usb_standard_desc_t *)(raw + offset);
        if (desc->bLength < USB_STANDARD_DESC_SIZE || offset + desc->bLength > total_len) {
            ESP_LOGW(TAG, "Descriptor parse stopped at offset=%d len=%u type=0x%02X total=%u",
                     offset, desc->bLength, desc->bDescriptorType, total_len);
            break;
        }

        ESP_LOGI(TAG, "  @%03d len=%u type=0x%02X", offset, desc->bLength, desc->bDescriptorType);
        if (desc->bDescriptorType == USB_B_DESCRIPTOR_TYPE_INTERFACE && desc->bLength >= USB_INTF_DESC_SIZE) {
            const usb_intf_desc_t *intf = (const usb_intf_desc_t *)desc;
            ESP_LOGI(TAG, "  Interface %u alt=%u class=0x%02X (%s) subclass=0x%02X protocol=0x%02X (%s) endpoints=%u iInterface=%u",
                     intf->bInterfaceNumber, intf->bAlternateSetting, intf->bInterfaceClass,
                     class_name(intf->bInterfaceClass), intf->bInterfaceSubClass,
                     intf->bInterfaceProtocol,
                     intf->bInterfaceClass == USB_CLASS_AUDIO ? audio_protocol_name(intf->bInterfaceProtocol) : "-",
                     intf->bNumEndpoints, intf->iInterface);
            ctx.klass = intf->bInterfaceClass;
            ctx.subclass = intf->bInterfaceSubClass;
            ctx.protocol = intf->bInterfaceProtocol;
            ctx.number = intf->bInterfaceNumber;
            ctx.alternate = intf->bAlternateSetting;
        } else if (desc->bDescriptorType == USB_B_DESCRIPTOR_TYPE_ENDPOINT && desc->bLength >= USB_EP_DESC_SIZE) {
            const usb_ep_desc_t *ep = (const usb_ep_desc_t *)desc;
            ESP_LOGI(TAG, "    Endpoint 0x%02X %s attr=0x%02X sync=%s usage=%s mps=%u interval=%u",
                     ep->bEndpointAddress,
                     transfer_type_name(ep->bmAttributes),
                     ep->bmAttributes,
                     ((ep->bmAttributes & 0x03) == USB_TRANSFER_TYPE_ISOCHRONOUS) ? isoc_sync_type_name(ep->bmAttributes) : "-",
                     ((ep->bmAttributes & 0x03) == USB_TRANSFER_TYPE_ISOCHRONOUS) ? isoc_usage_type_name(ep->bmAttributes) : "-",
                     ep->wMaxPacketSize & USB_W_MAX_PACKET_SIZE_MPS_MASK,
                     ep->bInterval);
        } else if (desc->bDescriptorType == USB_DESC_TYPE_IAD && desc->bLength >= 8) {
            const uint8_t *d = desc->val;
            ESP_LOGI(TAG, "  IAD: firstInterface=%u count=%u class=0x%02X (%s) subclass=0x%02X protocol=0x%02X iFunction=%u",
                     d[2], d[3], d[4], class_name(d[4]), d[5], d[6], d[7]);
        } else if (desc->bDescriptorType == USB_AUDIO_CS_INTERFACE ||
                   desc->bDescriptorType == USB_AUDIO_CS_ENDPOINT) {
            print_audio_class_specific_descriptor(desc, &ctx);
        } else {
            print_raw_descriptor(desc->val, desc->bLength);
        }

        offset += desc->bLength;
    }
}

static void control_transfer_done_cb(usb_transfer_t *transfer)
{
    SemaphoreHandle_t done = (SemaphoreHandle_t)transfer->context;
    xSemaphoreGive(done);
}

static esp_err_t usb_control_in(uint8_t request,
                                uint16_t value,
                                uint16_t index,
                                uint8_t *data,
                                uint16_t data_len,
                                int *actual_data_len)
{
    SemaphoreHandle_t done = xSemaphoreCreateBinary();
    if (done == NULL) {
        return ESP_ERR_NO_MEM;
    }

    usb_transfer_t *transfer = NULL;
    esp_err_t err = usb_host_transfer_alloc(USB_SETUP_PACKET_SIZE + data_len, 0, &transfer);
    if (err != ESP_OK) {
        vSemaphoreDelete(done);
        return err;
    }

    usb_setup_packet_t *setup = (usb_setup_packet_t *)transfer->data_buffer;
    setup->bmRequestType = USB_BM_REQUEST_TYPE_DIR_IN |
                           USB_BM_REQUEST_TYPE_TYPE_STANDARD |
                           USB_BM_REQUEST_TYPE_RECIP_DEVICE;
    setup->bRequest = request;
    setup->wValue = value;
    setup->wIndex = index;
    setup->wLength = data_len;

    transfer->device_handle = s_device_hdl;
    transfer->bEndpointAddress = 0;
    transfer->num_bytes = USB_SETUP_PACKET_SIZE + data_len;
    transfer->callback = control_transfer_done_cb;
    transfer->context = done;

    err = usb_host_transfer_submit_control(s_client_hdl, transfer);
    if (err == ESP_OK) {
        if (xSemaphoreTake(done, pdMS_TO_TICKS(1000)) != pdTRUE) {
            err = ESP_ERR_TIMEOUT;
        } else if (transfer->status != USB_TRANSFER_STATUS_COMPLETED) {
            ESP_LOGW(TAG, "control IN request=0x%02X value=0x%04X index=0x%04X status=%d",
                     request, value, index, transfer->status);
            err = ESP_FAIL;
        } else {
            int got = transfer->actual_num_bytes - USB_SETUP_PACKET_SIZE;
            if (got < 0) {
                got = 0;
            }
            if (got > data_len) {
                got = data_len;
            }
            memcpy(data, &transfer->data_buffer[USB_SETUP_PACKET_SIZE], got);
            if (actual_data_len != NULL) {
                *actual_data_len = got;
            }
        }
    }

    usb_host_transfer_free(transfer);
    vSemaphoreDelete(done);
    return err;
}

static void fetch_and_parse_raw_config_descriptor(void)
{
    uint8_t header[USB_CONFIG_DESC_SIZE] = {0};
    int got = 0;
    esp_err_t err = usb_control_in(USB_B_REQUEST_GET_DESCRIPTOR,
                                   (USB_W_VALUE_DT_CONFIG << 8),
                                   0,
                                   header,
                                   sizeof(header),
                                   &got);
    if (err != ESP_OK || got < USB_CONFIG_DESC_SIZE) {
        ESP_LOGW(TAG, "Manual GET_DESCRIPTOR config header failed: %s got=%d", esp_err_to_name(err), got);
        return;
    }

    const uint16_t total_len = read_le16(&header[2]);
    const uint16_t fetch_len = total_len > DESCRIPTOR_FETCH_MAX_BYTES ? DESCRIPTOR_FETCH_MAX_BYTES : total_len;
    ESP_LOGI(TAG, "Manual GET_DESCRIPTOR config: totalLength=%u fetchLength=%u maxControl=%u",
             total_len, fetch_len, (unsigned)DESCRIPTOR_FETCH_MAX_BYTES);

    uint8_t *buf = calloc(1, fetch_len);
    if (buf == NULL) {
        ESP_LOGE(TAG, "No memory for %u-byte config descriptor", fetch_len);
        return;
    }

    got = 0;
    err = usb_control_in(USB_B_REQUEST_GET_DESCRIPTOR,
                         (USB_W_VALUE_DT_CONFIG << 8),
                         0,
                         buf,
                         fetch_len,
                         &got);
    if (err == ESP_OK && got >= USB_CONFIG_DESC_SIZE) {
        parse_config_descriptor("manual-control", buf, got);
    } else {
        ESP_LOGW(TAG, "Manual GET_DESCRIPTOR full config failed: %s got=%d", esp_err_to_name(err), got);
    }
    free(buf);
}

static void print_device(uint8_t address, usb_device_handle_t device_hdl)
{
    usb_device_info_t info;
    const usb_device_desc_t *device_desc = NULL;
    const usb_config_desc_t *config_desc = NULL;

    ESP_ERROR_CHECK(usb_host_device_info(device_hdl, &info));
    ESP_ERROR_CHECK(usb_host_get_device_descriptor(device_hdl, &device_desc));

    ESP_LOGI(TAG, "===== USB device mounted =====");
    ESP_LOGI(TAG, "Address=%u devAddr=%u parent=%p parentPort=%u speed=%s config=%u EP0_MPS=%u",
             address, info.dev_addr, info.parent.dev_hdl, info.parent.port_num,
             speed_name(info.speed), info.bConfigurationValue, info.bMaxPacketSize0);
    ESP_LOGI(TAG, "VID=0x%04X PID=0x%04X bcdDevice=%x.%02x deviceClass=0x%02X (%s) subclass=0x%02X protocol=0x%02X USB=%x.%02x",
             device_desc->idVendor, device_desc->idProduct,
             device_desc->bcdDevice >> 8, device_desc->bcdDevice & 0xFF,
             device_desc->bDeviceClass, class_name(device_desc->bDeviceClass),
             device_desc->bDeviceSubClass, device_desc->bDeviceProtocol,
             device_desc->bcdUSB >> 8, device_desc->bcdUSB & 0xFF);
    ESP_LOGI(TAG, "String indexes: manufacturer=%u product=%u serial=%u configs=%u",
             device_desc->iManufacturer, device_desc->iProduct, device_desc->iSerialNumber,
             device_desc->bNumConfigurations);
    print_string_from_cached("Manufacturer", info.str_desc_manufacturer);
    print_string_from_cached("Product", info.str_desc_product);
    print_string_from_cached("Serial", info.str_desc_serial_num);

    esp_err_t err = usb_host_get_active_config_descriptor(device_hdl, &config_desc);
    if (err == ESP_OK && config_desc != NULL) {
        parse_config_descriptor("idf-cached", (const uint8_t *)config_desc, config_desc->wTotalLength);
    } else {
        ESP_LOGE(TAG, "usb_host_get_active_config_descriptor failed: %s", esp_err_to_name(err));
    }

#if MANUAL_DESCRIPTOR_FETCH_ENABLE
    fetch_and_parse_raw_config_descriptor();
#else
    ESP_LOGI(TAG, "Manual GET_DESCRIPTOR config fetch disabled; using IDF cached descriptor");
#endif
}

static void set_output_gpio(int gpio_num, int level)
{
    if (gpio_num < 0) {
        return;
    }
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << gpio_num,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&cfg));
    ESP_ERROR_CHECK(gpio_set_level(gpio_num, level));
}

static void disable_board_vbus(void)
{
    set_output_gpio(USB_HOST_EN_GPIO, 0);
    set_output_gpio(DEV_VBUS_EN_GPIO, 0);
    set_output_gpio(BOOST_EN_GPIO, 0);
    set_output_gpio(LIMIT_EN_GPIO, 0);
}

static void reset_board_vbus_after_boot(void)
{
    ESP_LOGI(TAG, "Reset USB VBUS after boot: off=%dms settle=%dms",
             USB_BOOT_RESET_OFF_MS, USB_BOOT_RESET_SETTLE_MS);
    disable_board_vbus();
    vTaskDelay(pdMS_TO_TICKS(USB_BOOT_RESET_OFF_MS));
    set_output_gpio(BOOST_EN_GPIO, 1);
    set_output_gpio(LIMIT_EN_GPIO, 1);
    set_output_gpio(DEV_VBUS_EN_GPIO, 1);
    set_output_gpio(USB_HOST_EN_GPIO, 1);
    vTaskDelay(pdMS_TO_TICKS(USB_BOOT_RESET_SETTLE_MS));
}

static void client_event_cb(const usb_host_client_event_msg_t *event_msg, void *arg)
{
    (void)arg;
    switch (event_msg->event) {
    case USB_HOST_CLIENT_EVENT_NEW_DEV:
        if (s_device_hdl != NULL) {
            ESP_LOGW(TAG, "Another device detected; detach current device first in this diagnostic build");
            return;
        }
        s_device_addr = event_msg->new_dev.address;
        ESP_ERROR_CHECK(usb_host_device_open(s_client_hdl, s_device_addr, &s_device_hdl));
        print_device(s_device_addr, s_device_hdl);
        break;
    case USB_HOST_CLIENT_EVENT_DEV_GONE:
        ESP_LOGI(TAG, "USB device detached, address=%u", s_device_addr);
        if (event_msg->dev_gone.dev_hdl == s_device_hdl && s_device_hdl != NULL) {
            esp_err_t err = usb_host_device_close(s_client_hdl, s_device_hdl);
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "usb_host_device_close failed: %s", esp_err_to_name(err));
            }
            s_device_hdl = NULL;
            s_device_addr = 0;
        }
        break;
    }
}

static void usb_library_task(void *arg)
{
    (void)arg;
    while (true) {
        uint32_t event_flags = 0;
        esp_err_t err = usb_host_lib_handle_events(portMAX_DELAY, &event_flags);
        if (err != ESP_OK && err != ESP_ERR_TIMEOUT) {
            ESP_LOGE(TAG, "usb_host_lib_handle_events: %s", esp_err_to_name(err));
        }
    }
}

static void usb_client_task(void *arg)
{
    (void)arg;
    while (true) {
        esp_err_t err = usb_host_client_handle_events(s_client_hdl, portMAX_DELAY);
        if (err != ESP_OK && err != ESP_ERR_TIMEOUT) {
            ESP_LOGE(TAG, "usb_host_client_handle_events: %s", esp_err_to_name(err));
        }
    }
}

static esp_err_t start_usb_topology(void)
{
    reset_board_vbus_after_boot();

    const usb_host_config_t host_config = {
        .skip_phy_setup = false,
        .root_port_unpowered = false,
        .intr_flags = ESP_INTR_FLAG_LEVEL1,
        .enum_filter_cb = NULL,
        .fifo_settings_custom = {
            .nptx_fifo_lines = 16,
            .ptx_fifo_lines = 150,
            .rx_fifo_lines = 34,
        },
    };
    ESP_RETURN_ON_ERROR(usb_host_install(&host_config), TAG, "usb_host_install failed");

    const usb_host_client_config_t client_config = {
        .is_synchronous = false,
        .max_num_event_msg = 8,
        .async = {
            .client_event_callback = client_event_cb,
            .callback_arg = NULL,
        },
    };
    ESP_RETURN_ON_ERROR(usb_host_client_register(&client_config, &s_client_hdl), TAG, "usb_host_client_register failed");

    xTaskCreatePinnedToCore(usb_library_task, "usb_library", 4096, NULL, 20, NULL, 0);
    xTaskCreatePinnedToCore(usb_client_task, "usb_client", 6144, NULL, 19, NULL, 0);
    return ESP_OK;
}

void app_main(void)
{
    ESP_LOGI(TAG, "L033 ESP-IDF USB topology parser");
    ESP_LOGI(TAG, "CONFIG_USB_HOST_CONTROL_TRANSFER_MAX_SIZE=%d", CONFIG_USB_HOST_CONTROL_TRANSFER_MAX_SIZE);
    disable_board_vbus();
    ESP_ERROR_CHECK(start_usb_topology());

    while (true) {
        usb_host_lib_info_t info;
        if (usb_host_lib_info(&info) == ESP_OK) {
            ESP_LOGI(TAG, "Host status: devices=%d clients=%d", info.num_devices, info.num_clients);
        }
        vTaskDelay(pdMS_TO_TICKS(3000));
    }
}
