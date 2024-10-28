#ifndef __SINGLE_HMI_SDK_H__
#define __SINGLE_HMI_SDK_H__
#include <zephyr/kernel.h>

void hmi_uart_init(void);

int connect_lcd(void);

bool go_to_download_mode(uint32_t file_lenght, uint16_t addr, bool flag);
bool send_tft_pkg(uint8_t *buffer, uint16_t lenght);

#endif
