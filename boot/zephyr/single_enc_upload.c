

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

// #ifdef CONFIG_MCUBOOT_USE_ALC16_AND_HMI
#include <zephyr/sys/ring_buffer.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/drivers/gpio.h>
#define UART_ALC16_DEVICE_NODE DT_CHOSEN(zephyr_alc16_uart)
#define UART_ALC16_BUFFER_SIZE 512
#define UART_ALC16_ASYNC_BUFFER_SZIE 16
#define ALC16_RESET_NODE DT_ALIAS(alc_reset)

static const struct gpio_dt_spec alc_reset = GPIO_DT_SPEC_GET(ALC16_RESET_NODE, gpios);

static const uint32_t kWaitForever = 0xffffffffu;

static const struct device *const uart_dev = DEVICE_DT_GET(UART_ALC16_DEVICE_NODE);

static volatile bool s_isTransferReceiveCompleted = false;
static volatile bool s_isTransferSendCompleted = false;

static uint8_t async_buffer_a[UART_ALC16_ASYNC_BUFFER_SZIE];
static uint8_t async_buffer_b[UART_ALC16_ASYNC_BUFFER_SZIE];

RING_BUF_DECLARE(uart_alc16_receive_buf, UART_ALC16_BUFFER_SIZE);
RING_BUF_DECLARE(uart_alc16_send_buf, UART_ALC16_BUFFER_SIZE);

static struct k_sem alc16_sem;
static void alc_semphore_init(void)
{
    k_sem_init(&alc16_sem, 0, 0x7fffffff);
}

static void alc_semphore_put(void)
{
    k_sem_give(&alc16_sem);
}

static bool alc_semphore_get(uint32_t timeoutUsecs)
{
    if (timeoutUsecs != kWaitForever)
    {
        if (timeoutUsecs > 0U)
        {
            timeoutUsecs /= 1000U;
            if (timeoutUsecs == 0U)
            {
                timeoutUsecs = 1U;
            }
        }
    }

    return (k_sem_take(&alc16_sem, K_USEC(timeoutUsecs)) == 0);
}

static void uart_async_callback(const struct device *dev, struct uart_event *evt, void *user_data)
{
    uint32_t size;
    uint8_t *data;
    static bool buffer_toggle = false;
    switch (evt->type)
    {
    case UART_RX_RDY:
        // 检查环形缓冲区是否有足够的空间
        if (ring_buf_space_get(&uart_alc16_receive_buf) == 0)
        {
            /* Error - receive buffer is full */
            // 记录错误，丢弃数据或扩展缓冲区
            break;
        }

        // 预留环形缓冲区空间以放置接收的数据
        size = ring_buf_put_claim(&uart_alc16_receive_buf, &data, evt->data.rx.len);
        if (size < evt->data.rx.len)
        {
            /* 缓冲区空间不足，处理错误或采取适当行动 */
            // 释放已预留但未使用的空间
            ring_buf_put_finish(&uart_alc16_receive_buf, 0);
            // 记录错误或丢弃数据
            break;
        }
        // 将接收到的数据从 evt->data.rx.buf 复制到由 data 指向的缓冲区
        memcpy(data, evt->data.rx.buf, evt->data.rx.len);
        // 完成环形缓冲区的写入操作
        ring_buf_put_finish(&uart_alc16_receive_buf, evt->data.rx.len);
        // 检查是否接收到足够的数据
        // if (ring_buf_size_get(&uart_alc16_receive_buf) >= s_transferReceiveRequireBytes)
        // {
        alc_semphore_put();
        // }
        break;

    case UART_RX_BUF_RELEASED:
        // 处理释放的缓冲区（如果需要）
        break;
    case UART_RX_DISABLED:
        // 重新启用接收以继续处理数据
        break;
    case UART_RX_BUF_REQUEST:
        if (buffer_toggle == false)
        {

            uart_rx_buf_rsp(dev, async_buffer_b, sizeof(async_buffer_a));
            buffer_toggle = true;
        }
        else
        {
            uart_rx_buf_rsp(dev, async_buffer_a, sizeof(async_buffer_a));
            buffer_toggle = false;
        }
        break;
    case UART_TX_DONE:
        alc_semphore_put();
        // ring_buf_reset(&uart_receive_buf);
        // uart_rx_disable(dev);
        // uart_rx_enable(dev, async_buffer_a, 6, SYS_FOREVER_MS);
        break;
    case UART_TX_ABORTED:
        // 处理发送中止事件
        break;
    case UART_RX_STOPPED:
        // 处理接收停止事件
        break;
    default:
        break;
    }
}

static void alc_uart_init(void)
{
    int ret;
    ret = device_is_ready(uart_dev);
    if (ret < 0)
    {
        BOOT_LOG_ERR("Failed to get uart4 device\n");
        return;
    }
    ret = uart_callback_set(uart_dev, uart_async_callback, NULL);
    if (ret < 0)
    {
        BOOT_LOG_ERR("Uart CallBack Error\n");
        return;
    }
}

static bool send_cmd(void)
{

    uint8_t end_command[] = {0xAA, 0x00, 0x00, 0x00, 0x14, 0x00, 0x00, 0x00, 0xD4,
                             0x2D, 0xF6, 0x49, 0x1E, 0x2B, 0xCB, 0x19, 0x00, 0x00, 0x7F, 0xD4};
    int ret = uart_tx(uart_dev, end_command, sizeof(end_command), SYS_FOREVER_MS);
    if (ret < 0)
    {
        BOOT_LOG_ERR("Uart uart_tx Error\n");
        return false;
    }
    alc_semphore_get(kWaitForever);
    k_msleep(50);
    return true;
}

static void alc_func_init(void)
{
    alc_uart_init();
    alc_semphore_init();

    gpio_pin_configure_dt(&alc_reset, GPIO_OUTPUT_ACTIVE);
    gpio_pin_set(alc_reset.port, alc_reset.pin, 0);
    k_msleep(5);
    gpio_pin_set(alc_reset.port, alc_reset.pin, 1);
}

// #endif

int release_image_to_slot(uint8_t app_slot, uint8_t storage_slot, uint32_t enc_image_lenght)
{
    int rc;
    const struct flash_area *storage_partition = NULL;
    const struct flash_area *app_partition = NULL;
    uint8_t buffer[512];
    size_t offset = 0;
    size_t write_times = 0;
    alc_func_init();
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
    for (size_t i = 0; i < write_times; i++)
    {

        send_cmd();
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
