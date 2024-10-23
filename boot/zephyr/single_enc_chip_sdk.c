

// #ifdef __ZEPHYR__

// #ifdef CONFIG_MCUBOOT_USE_ALC16_AND_HMI
#include <zephyr/kernel.h>
#include <zephyr/sys/ring_buffer.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/crc.h>

#include "../boot_serial/src/boot_serial_priv.h"
#include "bootutil/bootutil_log.h"

#include "single_enc_chip_sdk/single_enc_chip_sdk.h"

#define UART_ALC16_DEVICE_NODE DT_CHOSEN(zephyr_alc16_uart)
#define UART_ALC16_BUFFER_SIZE 512
#define UART_ALC16_ASYNC_BUFFER_SZIE 16
#define UART_ALC16_TIMEOUT_MS 50
#define ALC16_RESET_NODE DT_ALIAS(alc_reset)

static const struct gpio_dt_spec alc_reset = GPIO_DT_SPEC_GET(ALC16_RESET_NODE, gpios);
static const struct device *const uart_dev = DEVICE_DT_GET(UART_ALC16_DEVICE_NODE);

static uint8_t async_buffer_a[UART_ALC16_ASYNC_BUFFER_SZIE];
static uint8_t async_buffer_b[UART_ALC16_ASYNC_BUFFER_SZIE];

RING_BUF_DECLARE(uart_alc16_receive_buf, UART_ALC16_BUFFER_SIZE);

static uint8_t alc16_rx_buffer_b[UART_ALC16_BUFFER_SIZE];
// RING_BUF_DECLARE(uart_alc16_send_buf, UART_ALC16_BUFFER_SIZE);

#define TP1_NODE DT_ALIAS(tp1)
static const struct gpio_dt_spec tp1 = GPIO_DT_SPEC_GET(TP1_NODE, gpios);

BOOT_LOG_MODULE_DECLARE(mcuboot);
static struct k_sem alc16_sem;

static struct k_timer frame_timer; // 用于超时判断的定时器

static void test_gpio(uint8_t i)
{
    gpio_pin_set_dt(&tp1, 0);
    for (uint8_t m = 0; m < 2 * i; m++)
    {
        gpio_pin_toggle_dt(&tp1);
    }
    gpio_pin_set_dt(&tp1, 0);
}

static void alc_semphore_init(void)
{
    k_sem_init(&alc16_sem, 0, 0x7fffffff);
}

static void alc_semphore_put(void)
{
    k_sem_give(&alc16_sem);
}

static bool alc_semphore_get(uint32_t timeoutmsecs)
{
    return (k_sem_take(&alc16_sem, K_MSEC(timeoutmsecs)) == 0);
}

static void boot_print_hex(uint8_t *data, uint16_t lenght)
{
    char buffer[200];
    int offset = 0;
    // memset(buffer, 0, sizeof(buffer));
    for (uint8_t i = 0; i < lenght; i++)
    {
        offset += sprintf(&data[offset], "%02X ", data[i]);
    }
    BOOT_LOG_INF("read  array: %s", buffer);
}

static void uart_async_callback(const struct device *dev, struct uart_event *evt, void *user_data)
{
    uint32_t size;
    uint8_t *data;
    static bool buffer_toggle = false;
    switch (evt->type)
    {
    case UART_RX_RDY:

        k_timer_start(&frame_timer, K_MSEC(3), K_NO_WAIT);
        // size = ring_buf_space_get(&uart_alc16_receive_buf);
        size = ring_buf_put_claim(&uart_alc16_receive_buf, &data, evt->data.rx.len);
        if (size != evt->data.rx.len)
        {
            ring_buf_put_finish(&uart_alc16_receive_buf, 0); // 放弃这次写入
            break;
        }
        // 将接收到的数据从 evt->data.rx.buf 复制到由 data 指向的缓冲区
        memcpy(data, evt->data.rx.buf, evt->data.rx.len);
        // 完成环形缓冲区的写入操作
        ring_buf_put_finish(&uart_alc16_receive_buf, evt->data.rx.len);

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
            uart_rx_buf_rsp(dev, async_buffer_b, sizeof(async_buffer_b));
            buffer_toggle = true;
            test_gpio(2);
        }
        else
        {
            uart_rx_buf_rsp(dev, async_buffer_a, sizeof(async_buffer_a));
            buffer_toggle = false;
            test_gpio(2);
        }
        break;
    case UART_TX_DONE:
        alc_semphore_put();
        buffer_toggle = false;
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

static void frame_timeout_handler(struct k_timer *dummy)
{
    if (UART_ALC16_BUFFER_SIZE - ring_buf_space_get(&uart_alc16_receive_buf) > 0)
    {
        alc_semphore_put();
        test_gpio(3);
    }
}

void alc_func_init(void)
{
    alc_uart_init();
    alc_semphore_init();
    gpio_pin_configure_dt(&tp1, GPIO_OUTPUT_ACTIVE);
    gpio_pin_configure_dt(&alc_reset, GPIO_OUTPUT_ACTIVE);
    gpio_pin_set(alc_reset.port, alc_reset.pin, 0);
    k_msleep(5);
    gpio_pin_set(alc_reset.port, alc_reset.pin, 1);
    gpio_pin_set_dt(&tp1, 0);
}

ENUM_ALC16_FUNC_CODE alc16_Write_and_Read(const uint8_t *send_data, uint8_t *read_data, uint16_t send_lenght, uint16_t read_lenght, uint16_t TIME_OUT)
{
    uint16_t bytes_read, crc, crc16read;
    uart_rx_enable(uart_dev, async_buffer_a, sizeof(async_buffer_a), TIME_OUT);
    ring_buf_reset(&uart_alc16_receive_buf);
    int ret = uart_tx(uart_dev, send_data, send_lenght, SYS_FOREVER_MS);
    if (ret < 0)
    {
        BOOT_LOG_ERR("enc chip %s Error", ENUM_TO_STRING(ALC16_FUNC_TX_FINISH_TIMEOUT));
        return ALC16_FUNC_TX_ERROR;
    }
    alc_semphore_get(TIME_OUT);
    k_timer_init(&frame_timer, frame_timeout_handler, NULL);
    if (!alc_semphore_get(TIME_OUT))
    {
        BOOT_LOG_ERR("enc chip %s Error", ENUM_TO_STRING(ALC16_FUNC_RX_FINISH_TIMEOUT));
        return ALC16_FUNC_RX_FINISH_TIMEOUT;
    }
    k_timer_stop(&frame_timer);
    uart_rx_disable(uart_dev);

    bytes_read = ring_buf_get(&uart_alc16_receive_buf, read_data, UART_ALC16_BUFFER_SIZE - ring_buf_space_get(&uart_alc16_receive_buf));
    if (bytes_read < 16)
    {
        BOOT_LOG_ERR("enc chip bytes_read %d bytes", bytes_read);
        return ALC16_FUNC_RX_ERROR;
    }
    if (bytes_read > read_lenght)
    {
        BOOT_LOG_ERR("bytes_read > read_lenght");
        return ALC16_FUNC_RX_FINISH_TIMEOUT;
    }
    crc = crc16_reflect(0xA001, 0xFFFF, read_data, bytes_read - 2);
    crc16read = *(uint16_t *)(read_data + bytes_read - 2);
    if (crc != crc16read)
    {
        BOOT_LOG_ERR("enc bytes_read %d, chip %s Error", bytes_read, ENUM_TO_STRING(ALC16_FUNC_RX_CRC_ERROR));
        return ALC16_FUNC_RX_FINISH_TIMEOUT;
    }
    return ALC16_FUNC_SUCCESS;
}

bool send_cmd(void)
{
    uint8_t end_command[] = {0xAA, 0x00, 0x00, 0x00, 0x14, 0x00, 0x00, 0x00, 0xD4,
                             0x2D, 0xF6, 0x49, 0x1E, 0x2B, 0xCB, 0x19, 0x00, 0x00, 0x7F, 0xD4};

    if (alc16_Write_and_Read(end_command, alc16_rx_buffer_b, sizeof(end_command), sizeof(alc16_rx_buffer_b), UART_ALC16_TIMEOUT_MS) != ALC16_FUNC_SUCCESS)
    {
        BOOT_LOG_ERR("enc chip send command error");
        /*must be delay,flash write maybe error*/
        k_msleep(50);
        return false;
    }
    return true;
}

// #endif

// #endif