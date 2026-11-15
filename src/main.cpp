#include "negcon_hid.hpp"
#include <zephyr/kernel.h>

static NegconHid hid;
int main(void)
{

	if (hid.init() != 0) {
		return 0;
	}

	NegconHid::Input in{};
	uint8_t t = 0;

	while (true) {
		t++;
		in.twist = (int8_t)(t < 128 ? t - 64 : 192 - t);
		in.btn_i = (int8_t)(t / 2);
		in.btn_ii = (int8_t)(127 - in.btn_i);
		in.btn_l = (int8_t)(t < 128 ? t : 255 - t);
		in.buttons = 1U << (t / 32 % 8);

		(void)hid.send(in);   /* занято — ну и ладно, следующий круг довезёт */
		k_msleep(20);
	}
}
