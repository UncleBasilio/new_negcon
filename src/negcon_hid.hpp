#pragma once

#include <stdint.h>
#include <errno.h>
#include <zephyr/sys/atomic.h>

struct device; 


class NegconHid {
public:
	struct Input {
		uint8_t buttons;      /* 8 кнопок битами */
		int8_t  twist;        /* поворот, центр = 0 */
		int8_t  btn_i;        /* 0 (отпущена) .. 127 */
		int8_t  btn_ii;
		int8_t  btn_l;
	};
	static_assert(sizeof(Input) == 5, "Input must match the HID report layout");

	int  init();
	/* 0 = принято в отправку; -EBUSY = канал занят, решай сам (retry/drop).
	   Гарантия доставки последнего состояния — на вызывающем. */
	int  send(const Input &in);
	void counters(uint32_t &posted, uint32_t &done) const;
	static void iface_ready(const struct device *dev, bool ready);
	static void input_report_done(const struct device *dev, const uint8_t *report);
	static int get_report(const struct device *dev, uint8_t type, uint8_t id,
						uint16_t len, uint8_t *buf);
private:

	static const struct device *m_hid_dev;
	static bool   m_ready;
	static atomic_t m_posted;   /* поставлено на отправку (пишем только мы) */
	static atomic_t m_done;     /* завершено хостом (пишет только USB-поток) */
	/* занято <=> m_posted != m_done */
};
