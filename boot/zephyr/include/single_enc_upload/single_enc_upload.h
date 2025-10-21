#ifndef __SINGLE_ENC_UPLOAD_H__
#define __SINGLE_ENC_UPLOAD_H__
#include <zephyr/kernel.h>





typedef struct Size_And_Crc
{
    uint32_t pkg_crc;  // 压缩固件CRC
    uint32_t raw_crc;  // 原始固件CRC
    uint32_t raw_size; // 原始固件大小
    uint32_t pkg_size; // 压缩固件大小
    uint32_t reserve;  // 保留信息;
    /*加密前的sha256字节*/
    uint8_t raw_data_sha256[32];
    /*加密后的sha256字节*/
    uint8_t aes_data_sha256[32];

} Size_And_Crc_Struct;

typedef struct Rbl_Header
{
    uint8_t type[4]; // 文件头

    uint32_t time_stamp;   // 4字节时间搓
    uint8_t part_name[16]; // 16字节固件分区名
    uint8_t fw_ver[40];    // 固件版本号 //APP分区版本号
    uint8_t prod_code[24]; // 产品识别码

    Size_And_Crc_Struct Mcu_Infor;
    Size_And_Crc_Struct Tft_Infor;
#ifdef CONFIG_MCUBOOT_USE_FPGA_WITH_ALC16
    Size_And_Crc_Struct lvglsource_infor;///< LVGL source firmware size and CRC information

#endif
    uint8_t rand_a[32]; // A区随机数
    uint16_t key_index; // 秘钥索引
    uint16_t iv_index;  // 初始向量索引
    uint8_t rand_b[32]; // B区随机数

    uint32_t hdr_crc; // 头结构体CRC
} Rbl_Header_t;



int release_image_to_slot(uint8_t application_slot,uint8_t storage_slot,uint32_t enc_image_lenght);



#ifdef CONFIG_MCUBOOT_USE_FPGA_WITH_ALC16


void set_pwm_led_frq_duty(uint16_t  frq, uint16_t duty);

#endif








#endif



