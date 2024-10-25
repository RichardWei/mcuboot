

#include <zephyr/kernel.h>
#include <flash_map_backend/flash_map_backend.h>
#include "../boot_serial/src/boot_serial_priv.h"
#include "bootutil/bootutil_log.h"

BOOT_LOG_MODULE_DECLARE(mcuboot);

#ifdef __ZEPHYR__
#ifdef CONFIG_MCUBOOT_INDICATION_LED
#include "io/io.h"
#endif
#endif

#define CONFIG_MCUBOOT_USE_ALC16_AND_HMI
#ifdef CONFIG_MCUBOOT_USE_ALC16_AND_HMI

#include <zephyr/sys/crc.h>

#include "mbedtls/md.h"
#include "mbedtls/aes.h"
// #include "mbedtls/md5.h"
#include "single_enc_chip_sdk/single_enc_chip_sdk.h"
#include "single_enc_upload/single_enc_upload.h"

static uint8_t aes_key[32];
static uint8_t aes_iv[16];
static uint8_t str_buffer[112 * 2 + 1];

static void xor_array(uint8_t *arr, uint32_t len)
{
    uint32_t i = 0;
    for (i = 0; i < len; i++)
    {
        arr[i] ^= 0xFF;
    }
    return;
}

/*数组先转为对应的16进制字符串*/
static void bytes_array_to_str(uint8_t *input_data, char *output, uint16_t lenght)
{
    uint16_t i = 0, j = 0;
    uint8_t temp;
    for (i = 0; i < lenght; i++)
    {
        temp = ((*(input_data + i)) & 0xF0) >> 4;
        if (temp < 0x0A)
            *(output + j) = temp + 0x30;
        else
            *(output + j) = temp + 0x57;
        temp = (*(input_data + i)) & 0x0F;
        if (temp < 0X0A)
            *(output + j + 1) = temp + 0x30;
        else
            *(output + j + 1) = temp + 0x57;
        j += 2;
    }
    *(output + j + 1) = '\0';
}

// static int calculate_hash_sha256(const uint8_t *input, size_t length, uint8_t *output)
// {
//     const mbedtls_md_info_t *md_info;
//     mbedtls_md_context_t ctx;
//     int ret;

//     // 根据传入的 hash_type 获取对应的消息摘要信息
//     md_info = mbedtls_md_info_from_string("SHA256");
//     if (md_info == NULL)
//     {
//         BOOT_LOG_ERR("Unsupported hash type: sha256");
//         return -1;
//     }
//     // 初始化上下文
//     mbedtls_md_init(&ctx);

//     // 设置消息摘要上下文，指定是否用于 HMAC（这里设置为0，不使用HMAC）
//     if ((ret = mbedtls_md_setup(&ctx, md_info, 0)) != 0)
//     {
//         BOOT_LOG_ERR("Failed to setup context: -0x%04x", -ret);
//         mbedtls_md_free(&ctx);
//         return ret;
//     }

//     // 开始计算
//     if ((ret = mbedtls_md_starts(&ctx)) != 0)
//     {
//         BOOT_LOG_ERR("Failed to start hash calculation: -0x%04x", -ret);
//         mbedtls_md_free(&ctx);
//         return ret;
//     }

//     // 更新数据
//     if ((ret = mbedtls_md_update(&ctx, input, length)) != 0)
//     {
//         BOOT_LOG_ERR("Failed to update hash calculation: -0x%04x", -ret);
//         mbedtls_md_free(&ctx);
//         return ret;
//     }

//     // 完成哈希计算，并将结果写入 output
//     if ((ret = mbedtls_md_finish(&ctx, output)) != 0)
//     {
//         BOOT_LOG_ERR("Failed to finish hash calculation: -0x%04x", -ret);
//         mbedtls_md_free(&ctx);
//         return ret;
//     }

//     // 释放上下文
//     mbedtls_md_free(&ctx);

//     return 0; // 成功返回0
// }

// static int calculate_hash_md5(const uint8_t *input, size_t length, uint8_t *output)
// {
//     mbedtls_md5_context ctx;

//     // 初始化 MD5 上下文
//     mbedtls_md5_init(&ctx);

//     // 开始 MD5 计算
//     if (mbedtls_md5_starts(&ctx) != 0)
//     {
//         mbedtls_md5_free(&ctx);
//         BOOT_LOG_ERR("mbed md5 init failed");
//         return -1; // 如果失败，返回错误码
//     }

//     // 更新 MD5 计算（传入数据）
//     if (mbedtls_md5_update(&ctx, input, length) != 0)
//     {
//         mbedtls_md5_free(&ctx);
//         BOOT_LOG_ERR("mbed md5 updated failed");
//         return -1; // 如果失败，返回错误码
//     }

//     // 完成 MD5 计算（获得输出）
//     if (mbedtls_md5_finish(&ctx, output) != 0)
//     {
//         mbedtls_md5_free(&ctx);
//         BOOT_LOG_ERR("mbed md5 finish failed");
//         return -1; // 如果失败，返回错误码
//     }

//     // 释放 MD5 上下文
//     mbedtls_md5_free(&ctx);

//     return 0; // 成功返回0
// }

static int calculate_hash(const char *hash_name_string, uint8_t *input, size_t length, uint8_t *output)
{
    const mbedtls_md_info_t *md_info;
    mbedtls_md_context_t ctx;
    int ret;

    // 根据传入的 hash_type 获取对应的消息摘要信息
    md_info = mbedtls_md_info_from_string(hash_name_string);
    if (md_info == NULL)
    {
        BOOT_LOG_ERR("Unsupported hash type: sha256");
        return -1;
    }
    // 初始化上下文
    mbedtls_md_init(&ctx);

    // 设置消息摘要上下文，指定是否用于 HMAC（这里设置为0，不使用HMAC）
    if ((ret = mbedtls_md_setup(&ctx, md_info, 0)) != 0)
    {
        BOOT_LOG_ERR("Failed to setup context: -0x%04x", -ret);
        mbedtls_md_free(&ctx);
        return ret;
    }

    // 开始计算
    if ((ret = mbedtls_md_starts(&ctx)) != 0)
    {
        BOOT_LOG_ERR("Failed to start hash calculation: -0x%04x", -ret);
        mbedtls_md_free(&ctx);
        return ret;
    }

    // 更新数据
    if ((ret = mbedtls_md_update(&ctx, input, length)) != 0)
    {
        BOOT_LOG_ERR("Failed to update hash calculation: -0x%04x", -ret);
        mbedtls_md_free(&ctx);
        return ret;
    }

    // 完成哈希计算，并将结果写入 output
    if ((ret = mbedtls_md_finish(&ctx, output)) != 0)
    {
        BOOT_LOG_ERR("Failed to finish hash calculation: -0x%04x", -ret);
        mbedtls_md_free(&ctx);
        return ret;
    }

    // 释放上下文
    mbedtls_md_free(&ctx);

    return 0; // 成功返回0
}

static int calculate_hash_flash_data(const struct flash_area *partition, size_t length, size_t offset, uint8_t *digest)
{
    int ret;
    int i;
    uint32_t read_times;
    uint8_t read_buffer[256];
    uint8_t calculate_digest[32];
    const mbedtls_md_info_t *md_info;
    mbedtls_md_context_t ctx;

    // 根据传入的 hash_type 获取对应的消息摘要信息
    md_info = mbedtls_md_info_from_string("SHA256");
    if (md_info == NULL)
    {
        BOOT_LOG_ERR("Unsupported hash type: sha256");
        return -1;
    }
    // 初始化上下文
    mbedtls_md_init(&ctx);

    // 设置消息摘要上下文，指定是否用于 HMAC（这里设置为0，不使用HMAC）
    if ((ret = mbedtls_md_setup(&ctx, md_info, 0)) != 0)
    {
        BOOT_LOG_ERR("Failed to setup context: -0x%04x", -ret);
        goto error;
    }

    // 开始计算
    if ((ret = mbedtls_md_starts(&ctx)) != 0)
    {
        BOOT_LOG_ERR("Failed to start hash calculation: -0x%04x", -ret);
        goto error;
    }
    /*计算需要读取flash的次数*/
    if (length < sizeof(read_buffer))
        read_times = 1;
    else if (length % sizeof(read_buffer) == 0)
        read_times = length / sizeof(read_buffer);
    else
        read_times = length % sizeof(read_buffer) + 1;

    // read_times = (length + sizeof(read_buffer) - 1) / sizeof(read_buffer);

    for (i = 0; i < read_times; i++)
    {
        ret = flash_area_read(partition, i * sizeof(read_buffer) + offset, read_buffer, sizeof(read_buffer));

        if (ret)
        {
            BOOT_LOG_ERR("Failed to flash_area_read: 0x%04x", -ret);
            break;
        }
        // 更新数据
        if ((ret = mbedtls_md_update(&ctx, read_buffer, sizeof(read_buffer))) != 0)
        {
            BOOT_LOG_ERR("Failed to update hash calculation: 0x%04x", -ret);
            break;
        }
    }
    if (ret)
        goto error;
    // 完成哈希计算，并将结果写入 output
    if ((ret = mbedtls_md_finish(&ctx, calculate_digest)) != 0)
    {
        BOOT_LOG_ERR("Failed to finish hash calculation: -0x%04x", -ret);
        mbedtls_md_free(&ctx);
        return (memcmp(digest, calculate_digest, 32) != 0) ? -2 : 0;
    }
error:
    // 释放上下文
    mbedtls_md_free(&ctx);
    return ret;
}

static bool read_file_header_infor(Rbl_Header_t *header, const struct flash_area *partition)
{
    int rc;
    uint32_t crc;
    /*读取信息*/

    rc = flash_area_read(partition, 0, (uint8_t *)header, sizeof(Rbl_Header_t));
    if (rc)
    {
        return false;
    }
    /*校验CRC数据*/
    crc = crc32_ieee((uint8_t *)header, sizeof(Rbl_Header_t) - 4);
    if (header->hdr_crc == crc)
    {
        return true;
    }
    BOOT_LOG_ERR("read crc32 error");
    return false;
}

static bool get_app_file_key_iv(uint8_t *KEY, uint8_t *IV, const Rbl_Header_t *header)
{
    uint8_t i;
    /*前后32字节随机数，32字节key，16字节iv*/
    uint8_t buffer[32 + 32 + 32 + 16];
    /*用于伪装计算的中间缓冲值*/
    uint8_t file_aes_key[32], file_aes_iv[16];
    /*首先需要得到原始索引秘钥值*/
    if (ask_key_iv_random(KEY, IV, header->key_index, header->iv_index) == false)
    {
        return false;
    }
    /*复制下载文件中的随机数*/
    memcpy(buffer, header->rand_a, 32);
    /*A区随机数异或*/
    xor_array(buffer, 32);

    /*复制alc表中的key*/
    memcpy(buffer + 32, KEY, 32);
    /*复制alc表中的iv*/
    memcpy(buffer + 32 + 32, IV, 16);

    /*复制alc表中的iv*/
    memcpy(buffer + 32 + 32 + 16, header->rand_b, 32);
    /*B区随机数异或*/
    xor_array(buffer + 32 + 32 + 16, 32);
    /*计算首次sha256*/
    bytes_array_to_str(buffer, (char *)str_buffer, sizeof(buffer));
    calculate_hash("SHA256", (uint8_t *)str_buffer, sizeof(str_buffer) - 1, file_aes_key);
    calculate_hash("MD5", (uint8_t *)str_buffer, sizeof(str_buffer) - 1, file_aes_iv);

    for (i = 0; i < 50; i++)
    {
        calculate_hash("SHA256", file_aes_key, 32, buffer);
        calculate_hash("MD5", file_aes_iv, 16, buffer + 32);
        memcpy(file_aes_key, buffer, 32);
        memcpy(file_aes_iv, buffer + 32, 16);
        if (i < 5)
            ask_alc_random();
    }
    memcpy(KEY, file_aes_key, 32);
    memcpy(IV, file_aes_iv, 16);
    return true;
}

static int aes256_cbc_decrypt_file(const struct flash_area *storage_partition, const struct flash_area *app_partition, size_t length, size_t offset, uint8_t *key, uint8_t *iv)
{
    int i;
    int ret;
    uint32_t read_times;
    uint8_t read_buffer[256];

#define AES_BLOCK_SIZE 16
    // 初始化AES上下文
    mbedtls_aes_context aes_ctx;
    mbedtls_aes_init(&aes_ctx);
    uint8_t iv_copy[AES_BLOCK_SIZE];

    // 复制IV，因为mbedTLS在解密时会修改IV
    memcpy(iv_copy, iv, sizeof(iv_copy));
    // 设置AES-256解密上下文
    if ((ret = mbedtls_aes_setkey_dec(&aes_ctx, key, 256)) != 0)
    {
        BOOT_LOG_ERR("Failed to set AES decryption key: -0x%04x\n", -ret);
        ret = -1;
        goto error;
    }

    /*计算需要读取flash的次数*/
    if (length < sizeof(read_buffer))
        read_times = 1;
    else if (length % sizeof(read_buffer) == 0)
        read_times = length / sizeof(read_buffer);
    else
        read_times = length % sizeof(read_buffer) + 1;
    for (i = 0; i < read_times; i++)
    {
        ret = flash_area_read(storage_partition, i * sizeof(read_buffer) + offset, read_buffer, sizeof(read_buffer));
        if (ret)
        {
            BOOT_LOG_ERR("Failed to flash_area_read: 0x%04x", -ret);
            break;
        }
        if ((ret = mbedtls_aes_crypt_cbc(&aes_ctx, MBEDTLS_AES_DECRYPT, sizeof(read_buffer), iv_copy, read_buffer, read_buffer)) != 0)
        {
            BOOT_LOG_ERR("Failed to decrypt: -0x%04x\n", -ret);
            break;
        }
        ret = flash_area_write(app_partition, i * sizeof(read_buffer), read_buffer, sizeof(read_buffer));
        if (ret)
        {
            BOOT_LOG_ERR("Failed to flash_area_write: 0x%04x", -ret);
            break;
        }
    }
    if (ret)
        goto error;
    return 0;
error:
    mbedtls_aes_free(&aes_ctx);
    return ret;
}

int release_image_to_slot(uint8_t app_slot, uint8_t storage_slot, uint32_t enc_image_lenght)
{
    int rc = 0;
    /*打包的头信息*/
    Rbl_Header_t fw_info;
    const struct flash_area *storage_partition = NULL;
    const struct flash_area *app_partition = NULL;

    alc_func_init();

    BOOT_LOG_INF("release_image_to_slot app_slot %d ,enc_image_slot %d size %d", app_slot, storage_slot, enc_image_lenght);
    /*open app_slot partition*/
    rc = flash_area_open(flash_area_id_from_direct_image(app_slot), &app_partition);
    if (rc)
    {
        rc = -1;
        goto out;
    }
    io_led_set(0);
    /*open storage_slot partition*/
    rc = flash_area_open(flash_area_id_from_direct_image(storage_slot), &storage_partition);
    if (rc)
    {
        rc = -2;
        goto out;
    }
    /*读取打包的头信息*/
    if (!read_file_header_infor(&fw_info, storage_partition))
    {
        rc = -3;
        BOOT_LOG_ERR("header crc32 error ");
        goto out;
    }
    if (fw_info.Mcu_Infor.raw_size > flash_area_get_size(app_partition))
    {
        rc = -4;
        BOOT_LOG_ERR("file to big ");
        goto out;
    }
    ask_alc_random();
    /*获取真正的密钥*/
    get_app_file_key_iv(aes_key, aes_iv, &fw_info);
    /*检查下载区加密后MCU区间是否完整被篡改SHA256校验*/
    if (calculate_hash_flash_data(storage_partition, fw_info.Mcu_Infor.pkg_size, sizeof(Rbl_Header_t), fw_info.Mcu_Infor.aes_data_sha256))
    {
        rc = -5;
        BOOT_LOG_ERR("header mcu aes sha256 error ");
        goto out;
    }

    /*检查下载区加密后tft区间是否完整被篡改SHA256校验*/
    if (calculate_hash_flash_data(storage_partition, fw_info.Tft_Infor.pkg_size, sizeof(Rbl_Header_t) + fw_info.Mcu_Infor.pkg_size, fw_info.Tft_Infor.aes_data_sha256))
    {
        rc = -6;
        BOOT_LOG_ERR("header tft aes sha256 error ");
        goto out;
    }

    /*释放固件到mcu app区域*/
    if (aes256_cbc_decrypt_file(storage_partition, app_partition, fw_info.Mcu_Infor.pkg_size, sizeof(Rbl_Header_t), aes_key, aes_iv))
    {
        rc = -7;
        BOOT_LOG_ERR("aes256_cbc_decrypt_file error ");
        goto out;
    }

    /*检查MCU app区间是否完整被篡改SHA256校验*/
    if (calculate_hash_flash_data(app_partition, fw_info.Mcu_Infor.raw_size, 0, fw_info.Mcu_Infor.raw_data_sha256))
    {
        rc = -8;
        BOOT_LOG_ERR("header mcu raw sha256 error ");
        goto out;
    }

    BOOT_LOG_INF("release_image_to_slot success ");
    return rc;

out:
    BOOT_LOG_ERR("release_image_to_slot error code %d", rc);
    return rc;
}

#else

int release_image_to_slot(uint8_t app_slot, uint8_t storage_slot, uint32_t enc_image_lenght)
{
    int rc;
    const struct flash_area *storage_partition = NULL;
    const struct flash_area *app_partition = NULL;
    uint8_t buffer[512];
    size_t offset = 0;
    size_t write_times = 0;
#ifdef CONFIG_MCUBOOT_USE_ALC16_AND_HMI

    alc_func_init();

#endif
    BOOT_LOG_INF("release_image_to_slot app_slot %d ,enc_image_slot %d size %d", app_slot, storage_slot, enc_image_lenght);
    /*open app_slot partition*/
    rc = flash_area_open(flash_area_id_from_direct_image(app_slot), &app_partition);
    if (rc)
    {
        rc = MGMT_ERR_EINVAL;
        goto out;
    }
    /*Dont Need Earse ,it earseed when downdload*/
    /*earse app_slot partition*/
    // const size_t area_size = flash_area_get_size(app_partition);

    // io_led_set(0);
    // k_msleep(50);
    // io_led_set(1);

    // rc = flash_area_erase(app_partition, 0, area_size);
    // if (rc) {
    //     rc = MGMT_ERR_ENOMEM;
    //     goto out;
    // }
    io_led_set(0);
    /*open storage_slot partition*/
    rc = flash_area_open(flash_area_id_from_direct_image(storage_slot), &storage_partition);
    if (rc)
    {
        rc = MGMT_ERR_EINVAL;
        goto out;
    }

    // write_times=(enc_image_lenght%sizeof(buffer)==0)?(enc_image_lenght%sizeof(buffer)):((enc_image_lenght%sizeof(buffer))+1)
    write_times = (enc_image_lenght + sizeof(buffer) - 1) / sizeof(buffer);

    if (write_times * sizeof(buffer) > flash_area_get_size(app_partition))
    {
        BOOT_LOG_ERR("file to big ");
        goto out;
    }

    for (size_t i = 0; i < write_times; i++)
    {

        // send_cmd();

        rc = flash_area_read(storage_partition, offset, buffer, sizeof(buffer));
        if (rc)
        {
            rc = MGMT_ERR_ENOTSUP;
            goto out;
        }
        rc = flash_area_write(app_partition, offset, buffer, sizeof(buffer));
        if (rc)
        {
            rc = MGMT_ERR_ENOTSUP;
            goto out;
        }
        offset += sizeof(buffer);
    }
    BOOT_LOG_INF("release_image_to_slot success ");
    return rc;

out:
    BOOT_LOG_ERR("release_image_to_slot error code %d", rc);
    return rc;
}

#endif