#include "negcon_hid.hpp"

#include <array>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/usb/usbd.h>
#include <zephyr/usb/class/hid.h>
#include <zephyr/usb/class/usbd_hid.h>

/* #include "sample_usbd.h" -- больше не включаем: беззащитный сишный
   заголовок. Вместо него своё объявление с сишной линковкой: */
extern "C" struct usbd_context *
sample_usbd_init_device(void (*msg_cb)(struct usbd_context *, const struct usbd_msg *));



#include <zephyr/logging/log.h>


LOG_MODULE_REGISTER(negcon, LOG_LEVEL_INF);

#define HID_USAGE_GEN_DESKTOP_Z   0x32
#define HID_USAGE_GEN_DESKTOP_RX  0x33

const struct device *NegconHid::m_hid_dev;
bool NegconHid::m_ready;
atomic_t NegconHid::m_posted;
atomic_t NegconHid::m_done;

namespace {
constexpr uint8_t descriptor_bytes[] = 
{
		HID_USAGE_PAGE(HID_USAGE_GEN_DESKTOP),
		HID_USAGE(HID_USAGE_GEN_DESKTOP_GAMEPAD),
		HID_COLLECTION(HID_COLLECTION_APPLICATION),
			HID_USAGE(HID_USAGE_GEN_DESKTOP_POINTER),
			HID_COLLECTION(HID_COLLECTION_PHYSICAL),

			HID_USAGE_PAGE(HID_USAGE_GEN_BUTTON),
			HID_USAGE_MIN8(1), HID_USAGE_MAX8(8),
			HID_LOGICAL_MIN8(0), HID_LOGICAL_MAX8(1),
			HID_REPORT_SIZE(1), HID_REPORT_COUNT(8),
			HID_INPUT(0x02),

			HID_USAGE_PAGE(HID_USAGE_GEN_DESKTOP),
			HID_USAGE(HID_USAGE_GEN_DESKTOP_X),
			HID_USAGE(HID_USAGE_GEN_DESKTOP_Y),
			HID_USAGE(HID_USAGE_GEN_DESKTOP_Z),
			HID_USAGE(HID_USAGE_GEN_DESKTOP_RX),
			HID_LOGICAL_MIN8((uint8_t)-128), HID_LOGICAL_MAX8(127),
			HID_REPORT_SIZE(8), HID_REPORT_COUNT(4),
			HID_INPUT(0x02),

			HID_END_COLLECTION,
		HID_END_COLLECTION,
};
} // namespace

static consteval std::array<uint8_t, sizeof(descriptor_bytes)> make_descriptor()
{
    return std::to_array(descriptor_bytes);
}

static constexpr auto k_desc = make_descriptor();
UDC_STATIC_BUF_DEFINE(report_buf, sizeof(NegconHid::Input));

void NegconHid::iface_ready(const struct device *dev, bool ready)
{
	m_ready = ready;
	LOG_INF("HID interface %s", ready ? "ready" : "not ready");
}

void NegconHid::input_report_done(const struct device *dev, const uint8_t *report)
{
	atomic_inc(&m_done);   /* единственный писатель m_done — USB-поток */
}

int NegconHid::get_report(const struct device *dev, uint8_t type, uint8_t id,
                          uint16_t len, uint8_t *buf)
{
    if (type == HID_REPORT_TYPE_INPUT && id == 0 && len >= sizeof(Input)) {
        memcpy(buf, report_buf, sizeof(Input));
        return sizeof(Input);
    }
    return -ENOTSUP;
}

static const hid_device_ops negcon_ops = {
	.iface_ready = NegconHid::iface_ready,
	.get_report = NegconHid::get_report,	
	.input_report_done = NegconHid::input_report_done,
};

int NegconHid::init()
{
	m_hid_dev = DEVICE_DT_GET_ONE(zephyr_hid_device);
	if (!device_is_ready(m_hid_dev)) {
		LOG_ERR("HID device not ready");
		return -EIO;
	}

	int ret = hid_device_register(m_hid_dev, k_desc.data(), k_desc.size(), &negcon_ops);
	if (ret != 0) {
		return ret;
	}

	struct usbd_context *usbd = sample_usbd_init_device(NULL);
	if (usbd == NULL) {
		return -ENODEV;
	}

	ret = usbd_enable(usbd);
	if (ret == 0) {
		LOG_INF("neGcon USB device enabled");
	}
	return ret;
}

int NegconHid::send(const Input &in)
{
	if (!m_ready || atomic_get(&m_posted) != atomic_get(&m_done)) {
		return -EBUSY;
	}

	memcpy(report_buf, &in, sizeof(in));
	atomic_inc(&m_posted);              /* сначала объявили передачу! */
	int ret = hid_device_submit_report(m_hid_dev, sizeof(in), report_buf);
	if (ret != 0) {
		atomic_dec(&m_posted);          /* не вышло — откат */
		return -EBUSY;
	}
	return 0;
}

void NegconHid::counters(uint32_t &posted, uint32_t &done) const
{
	posted = atomic_get(&m_posted);
	done = atomic_get(&m_done);
}