

// #ifdef __ZEPHYR__
#include <zephyr/kernel.h>
#include "bootutil/bootutil_log.h"

BOOT_LOG_MODULE_DECLARE(mcuboot);

#ifdef CONFIG_MCUBOOT_USE_HMI_WITH_ALC16

// #if 1

/*hmi更新*/

#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/sys/ring_buffer.h>
#include <zephyr/drivers/gpio.h>
#include "mbedtls/aes.h"

#include "stdlib.h"

#define UART_DEVICE_NODE DT_CHOSEN(zephyr_hmi_uart)
static const struct device *const hmi_uart_dev = DEVICE_DT_GET(UART_DEVICE_NODE);

#define TP1_NODE DT_ALIAS(tp1)
static const struct gpio_dt_spec tp1 = GPIO_DT_SPEC_GET(TP1_NODE, gpios);
#define UART_HMI_BUFFER_SIZE 512
#define UART_HMI_ASYNC_BUFFER_SZIE 16

static uint8_t hmi_async_buffer_a[UART_HMI_ASYNC_BUFFER_SZIE];
static uint8_t hmi_async_buffer_b[UART_HMI_ASYNC_BUFFER_SZIE];

#define UART_HMI_TIMEOUT_MS 50

RING_BUF_DECLARE(uart_hmi_receive_buf, UART_HMI_BUFFER_SIZE);
static uint8_t hmi_rx_buffer[UART_HMI_BUFFER_SIZE];

/*HMI固件更新相关的函数*/
static struct k_sem hmi_sem;
// 用于超时判断的定时器
static struct k_timer hmi_frame_timer;

static void tp_test_gpio(uint8_t i)
{
    gpio_pin_set_dt(&tp1, 0);
    for (uint8_t m = 0; m < 2 * i; m++)
    {
        gpio_pin_toggle_dt(&tp1);
    }
    gpio_pin_set_dt(&tp1, 0);
}
static void hmi_semphore_init(void)
{
    k_sem_init(&hmi_sem, 0, 0x7fffffff);
}

static void hmi_semphore_put(void)
{
    k_sem_give(&hmi_sem);
}

static bool hmi_semphore_get(uint32_t timeoutmsecs)
{
    return (k_sem_take(&hmi_sem, K_MSEC(timeoutmsecs)) == 0);
}

static void hmi_uart_async_callback(const struct device *dev, struct uart_event *evt, void *user_data)
{
    uint32_t size;
    uint8_t *data;
    static bool buffer_toggle = false;
    switch (evt->type)
    {
    case UART_RX_RDY:

        k_timer_start(&hmi_frame_timer, K_MSEC(3), K_NO_WAIT);
        // size = ring_buf_space_get(&uart_alc16_receive_buf);
        size = ring_buf_put_claim(&uart_hmi_receive_buf, &data, evt->data.rx.len);
        if (size != evt->data.rx.len)
        {
            ring_buf_put_finish(&uart_hmi_receive_buf, 0); // 放弃这次写入
            break;
        }
        // 将接收到的数据从 evt->data.rx.buf 复制到由 data 指向的缓冲区
        memcpy(data, evt->data.rx.buf, evt->data.rx.len);
        // 完成环形缓冲区的写入操作
        ring_buf_put_finish(&uart_hmi_receive_buf, evt->data.rx.len);
        // tp_test_gpio(5);
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
            uart_rx_buf_rsp(dev, hmi_async_buffer_b, sizeof(hmi_async_buffer_b));
            buffer_toggle = true;
        }
        else
        {
            uart_rx_buf_rsp(dev, hmi_async_buffer_a, sizeof(hmi_async_buffer_a));
            buffer_toggle = false;
        }
        break;
    case UART_TX_DONE:
        hmi_semphore_put();
        tp_test_gpio(1);
        buffer_toggle = false;
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

void hmi_uart_init(void)
{
    int ret;
    hmi_semphore_init();
    gpio_pin_configure_dt(&tp1, GPIO_OUTPUT_ACTIVE);
    gpio_pin_set_dt(&tp1, 0);
    ret = device_is_ready(hmi_uart_dev);

    if (ret < 0)
    {
        BOOT_LOG_ERR("Failed to get hmi uart device\n");
        return;
    }
    ret = uart_callback_set(hmi_uart_dev, hmi_uart_async_callback, NULL);
    if (ret < 0)
    {
        BOOT_LOG_ERR("hmi Uart CallBack Error\n");
        return;
    }
}
static void hmi_frame_timeout_handler(struct k_timer *dummy)
{
    if (UART_HMI_BUFFER_SIZE - ring_buf_space_get(&uart_hmi_receive_buf) > 0)
    {
        hmi_semphore_put();
        tp_test_gpio(3);
    }
}

static int hmi_write_and_read(const uint8_t *send_data, uint8_t *read_data, uint16_t send_lenght, uint16_t read_lenght, uint16_t TIME_OUT)
{

    uint16_t bytes_read;
    /*使能数据接收*/

    if (read_data != NULL)
        uart_rx_enable(hmi_uart_dev, hmi_async_buffer_a, sizeof(hmi_async_buffer_a), TIME_OUT);
    ring_buf_reset(&uart_hmi_receive_buf);

    int ret = uart_tx(hmi_uart_dev, send_data, send_lenght, SYS_FOREVER_MS);

    if (ret < 0)
    {
        return -1;
    }
    if (!hmi_semphore_get(TIME_OUT))
    {
        return -2;
    }
    if (read_data == NULL)
    {
        return 0;
    }
    k_timer_init(&hmi_frame_timer, hmi_frame_timeout_handler, NULL);
    if (!hmi_semphore_get(TIME_OUT))
    {
        return -3;
    }
    k_timer_stop(&hmi_frame_timer);
    uart_rx_disable(hmi_uart_dev);
    bytes_read = ring_buf_get(&uart_hmi_receive_buf, read_data, UART_HMI_BUFFER_SIZE - ring_buf_space_get(&uart_hmi_receive_buf));
    if (bytes_read < 1)
    {
        return -4;
    }
    return 0;
}

static int hmi_read(uint8_t *read_data, uint16_t read_lenght, uint16_t timeout)
{
    uint16_t bytes_read;
    uart_rx_enable(hmi_uart_dev, hmi_async_buffer_a, sizeof(hmi_async_buffer_a), timeout);
    ring_buf_reset(&uart_hmi_receive_buf);
    k_timer_init(&hmi_frame_timer, hmi_frame_timeout_handler, NULL);
    hmi_semphore_get(timeout + 5);
    k_timer_stop(&hmi_frame_timer);
    uart_rx_disable(hmi_uart_dev);
    bytes_read = ring_buf_get(&uart_hmi_receive_buf, read_data, UART_HMI_BUFFER_SIZE - ring_buf_space_get(&uart_hmi_receive_buf));
    if (bytes_read < 1)
    {
        BOOT_LOG_ERR("bytes_read %d", bytes_read);
        return -2;
    }
    BOOT_LOG_INF("Receive log lenght %d", bytes_read);
    return bytes_read;
}

static int com_sendstring(char *s, uint16_t addr, bool issend)
{

    int rc;
    uint8_t nop = 0;
    uint8_t endbytes[] = {0xff, 0xff, 0xff};
    uint8_t addrbytes[2];
    if (addr != 0)
    {
        addrbytes[0] = addr & 0xff;
        addrbytes[1] = addr >> 8;
        if (hmi_write_and_read(addrbytes, NULL, sizeof(addrbytes), 0, 500))
            return -1;
    }
    if (strlen(s) == 0)
        rc = hmi_write_and_read(&nop, NULL, 1, 0, 500);
    else
        rc = hmi_write_and_read((uint8_t *)s, NULL, strlen(s), 0, 500);
    if (rc)
        return -2;
    if (issend)
    {
        if (hmi_write_and_read(endbytes, NULL, sizeof(endbytes), 0, 500))
            return -3;
    }
    return 0;
}



int connect_lcd(void)
{

    char *p1, *p2;
    uint16_t addr = 0;
    int rc;
    int ret = 0;
    int bytes_read;
    // 避免屏幕已经收到一些非法数据，所以先发一个空指令
    rc = com_sendstring("\0", 0, true);
    if (rc)
    {
        BOOT_LOG_ERR("hmi nop command");
        ret = -1;
        goto error;
    }
    // 如果在主动解析模式，先退出
    rc = com_sendstring("DRAKJHSUYDGBNCJHGJKSHBDN", 65535, true);
    if (rc)
    {
        BOOT_LOG_ERR("hmi nop command");
        ret = -2;
        goto error;
    }
    // 等待退出主动解析
    if ((bytes_read = hmi_read(hmi_rx_buffer, sizeof(hmi_rx_buffer), 20)) < 1)
    {
        BOOT_LOG_ERR("bytes_read error");
        ret = -3;
        goto error;
    }
    // 无地址联机，为了兼容老产品
    rc = com_sendstring("connect", 0, true);
    if (rc)
    {
        BOOT_LOG_ERR("hmi nop command");
        ret = -3;
        goto error;
    }
    bytes_read = hmi_read(hmi_rx_buffer, sizeof(hmi_rx_buffer), 30);
    /*第一种方式联机失败*/
    if (bytes_read > 1)
    { // 广播地址联机
        rc = com_sendstring("connect", 65535, true);
        if (rc)
        {
            BOOT_LOG_ERR("hmi nop command");
            ret = -3;
            goto error;
        }
        bytes_read = hmi_read(hmi_rx_buffer, sizeof(hmi_rx_buffer), 30);
    }
    /*第二帧方式联机依旧失败*/
    if (bytes_read < 1)
    {
        BOOT_LOG_ERR("connect type 2 failed");
        ret = -4;
        goto error;
    }
    if (strstr(hmi_rx_buffer, "comok ") == NULL)
    {

        BOOT_LOG_ERR("hmi_rx_buffer not includ  comok ");
        ret = -5;
        goto error;
    }
    hmi_rx_buffer[bytes_read] = '\0';
    p1 = strstr((char *)hmi_rx_buffer, "-");
    if (p1)
    {
        p2 = strstr(p1 + 1, ",");
        if (p2)
        {
            *(p2 + 1) = '\0';
            addr = atoi(p1 + 1);
        }
    }
    return addr;
error:
    return ret;
}

bool go_to_download_mode(uint32_t file_lenght, uint16_t addr, bool flag)
{
    char Buffer[128];
    uint16_t bytes_read;
    sprintf(Buffer, "whmi-wri %d,115200,0", file_lenght);
    com_sendstring(Buffer, addr, flag);
    if ((bytes_read = hmi_read(hmi_rx_buffer, sizeof(hmi_rx_buffer), 500)) == 1)
        return true;
    else
        return false;
}

bool send_tft_pkg(const uint8_t *buffer, uint16_t lenght)
{

    if (hmi_write_and_read(buffer, hmi_rx_buffer, lenght, 8, 500))
    {
        BOOT_LOG_ERR("send_tft_pkg failed");
        return false;
    }
    return hmi_rx_buffer[0] == 0x05;
    true;
    false;
}

#endif