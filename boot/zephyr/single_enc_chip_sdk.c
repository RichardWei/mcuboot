

// #ifdef __ZEPHYR__
#include <zephyr/kernel.h>



#ifdef CONFIG_MCUBOOT_USE_ALC16
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/ring_buffer.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/crc.h>

#include <zephyr/random/random.h>

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
// 用于超时判断的定时器
static struct k_timer frame_timer;
/*用于同步的随机数*/
static uint8_t random[16];

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

// static void boot_print_hex(char *header, uint8_t *data, uint16_t lenght)
// {
//     char buffer[200];
//     int offset = 0;
//     // memset(buffer, 0, sizeof(buffer));
//     for (uint8_t i = 0; i < lenght; i++)
//     {
//         offset += sprintf(&buffer[offset], "%02X ", data[i]);
//     }
//     BOOT_LOG_INF("%s  array: %s", header, buffer);
// }

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
            // test_gpio(2);
        }
        else
        {
            uart_rx_buf_rsp(dev, async_buffer_a, sizeof(async_buffer_a));
            buffer_toggle = false;
            // test_gpio(2);
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
    Header_t *header_p;
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
    header_p = (Header_t *)read_data;
    if (header_p->lenght != bytes_read)
    {
        BOOT_LOG_ERR("enc bytes_read need %d, but read  %d ", header_p->lenght, bytes_read - 2);
        return ALC16_FUNC_RX_LENGHT_ERROR;
    }

    return ALC16_FUNC_SUCCESS;
}

static void xor_array(uint8_t *arr, uint32_t len)
{
    uint32_t i = 0;
    for (i = 0; i < len; i++)
    {
        arr[i] ^= 0xFF;
    }
    return;
}

static Header_t get_header_data(uint8_t header, uint8_t functioncode)
{
    Header_t header_i;
    // uint32_t rand_nmber;
    header_i.header = header;
    header_i.funtioncode = functioncode;
    /*填充8个随机数*/
    sys_rand_get(header_i.random, sizeof(header_i.random));
    /*继续填充3个随机数*/
    sys_rand_get(&header_i._rsv, 1);
    sys_rand_get((uint8_t *)&header_i._rsv1, 2);
    return header_i;
}

bool ask_alc_random(void)
{

    Header_t header_i;
    Ask_random_tx_t ask_random_tx_i;
    Ask_random_rx_t ask_random_rx_i;
    header_i = get_header_data(0xAA, ASK_RANDOM);
    memcpy(&ask_random_tx_i.header, &header_i, sizeof(header_i));
    ask_random_tx_i.header.lenght = sizeof(ask_random_tx_i);
    sys_rand_get((uint8_t *)&ask_random_tx_i._rsv, 2);

    ask_random_tx_i.crc = crc16_reflect(0xA001, 0xFFFF, (uint8_t *)&ask_random_tx_i, sizeof(Ask_random_tx_t) - 2);
    if (alc16_Write_and_Read((uint8_t *)&ask_random_tx_i, alc16_rx_buffer_b, sizeof(ask_random_tx_i), sizeof(ask_random_rx_i), 200) != ALC16_FUNC_SUCCESS)
        return false;
    /*复制数据到结构体*/
    memcpy((uint8_t *)&ask_random_rx_i, alc16_rx_buffer_b, sizeof(ask_random_rx_i));
    /*清空缓存*/
    memset(alc16_rx_buffer_b, 0, sizeof(alc16_rx_buffer_b));
    /*校验设置状态*/
    if (ask_random_rx_i.header.status != 1)
        return false;
    /*复制数据到结构体*/
    memcpy(random, ask_random_rx_i.true_random, sizeof(random));
    // boot_print_hex("random", random, 16);
    return true;
}

/*获取秘钥表索引秘钥*/
/*从alc16得到key和iv*/
bool ask_key_iv_random(uint8_t *key, uint8_t *iv, uint16_t key_index, uint16_t iv_index)
{

    Header_t header_i;
    Ask_aes_table_rx_t ask_aes_table_rx_i;
    Ask_aes_table_tx_t ask_aes_table_tx_i;
    header_i = get_header_data(0xAA, ASK_AES_KEY_IV);
    memcpy(&ask_aes_table_tx_i.header, &header_i, sizeof(header_i));
    /*索引*/
    ask_aes_table_tx_i.key_index = key_index;
    ask_aes_table_tx_i.iv_index = iv_index;

    ask_aes_table_tx_i.header.lenght = sizeof(ask_aes_table_tx_i);
    /*填充随机数*/
    sys_rand_get(ask_aes_table_tx_i.number_a, sizeof(ask_aes_table_tx_i.number_a));
    /*填充随机数*/
    sys_rand_get(ask_aes_table_tx_i.number_b, sizeof(ask_aes_table_tx_i.number_b));
    /*填充随机数*/
    sys_rand_get((uint8_t *)&ask_aes_table_tx_i._rsv, 2);
    ask_aes_table_tx_i.crc = crc16_reflect(0xA001, 0xFFFF, (uint8_t *)&ask_aes_table_tx_i, sizeof(ask_aes_table_tx_i) - 2);

    if (alc16_Write_and_Read((uint8_t *)&ask_aes_table_tx_i, alc16_rx_buffer_b, sizeof(ask_aes_table_tx_i), sizeof(ask_aes_table_rx_i), 200) != ALC16_FUNC_SUCCESS)
        return false;

    /*复制数据到结构体*/
    memcpy((uint8_t *)&ask_aes_table_rx_i, alc16_rx_buffer_b, sizeof(ask_aes_table_rx_i));
    /*清空缓存*/
    memset(alc16_rx_buffer_b, 0, sizeof(alc16_rx_buffer_b));

    /*校验设置状态*/
    if (ask_aes_table_rx_i.header.status != 1)
        return false;
    // /*复制数据到结构体*/
    // rt_memcpy(random, ask_aes_table_rx_i.true_random, sizeof(random));

    xor_array(ask_aes_table_rx_i.key, sizeof(ask_aes_table_rx_i.key));
    xor_array(ask_aes_table_rx_i.iv, sizeof(ask_aes_table_rx_i.iv));

    /*得到秘钥表的key和iv*/
    memcpy(key, ask_aes_table_rx_i.key, sizeof(ask_aes_table_rx_i.key));
    memcpy(iv, ask_aes_table_rx_i.iv, sizeof(ask_aes_table_rx_i.iv));

    // boot_print_hex("key ", key, 32);
    // boot_print_hex("iv ", iv, 16);
    return true;
}


#endif