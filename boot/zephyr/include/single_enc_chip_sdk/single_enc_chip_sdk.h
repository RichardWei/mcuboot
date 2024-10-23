#ifndef __SINGLE_ENC_CHIP_SDK_H__
#define __SINGLE_ENC_CHIP_SDK_H__

#define ALC16_UART_TIMEOUT 200

#define ENUM_TO_STRING(name) #name

typedef enum ALC16_ERROR_CODE
{
    ALC16_FUNC_SUCCESS = 0,
    ALC16_FUNC_TX_ERROR,
    ALC16_FUNC_RX_ERROR,
    ALC16_FUNC_TX_FINISH_TIMEOUT,
    ALC16_FUNC_RX_FINISH_TIMEOUT,
    ALC16_FUNC_RX_CRC_ERROR,
} ENUM_ALC16_FUNC_CODE;

typedef struct Header
{
    uint8_t header;
    uint8_t funtioncode;
    uint8_t status;
    uint8_t _rsv;

    uint16_t lenght;
    uint16_t _rsv1;
    /*随机数*/
    uint8_t random[8];
} Header_t;

typedef struct Ask_random_tx
{
    Header_t header;

    uint16_t _rsv;
    uint16_t crc;
} Ask_random_tx_t;

/*发送随机数或者设置随机数用到的结构体*/
typedef struct Ask_random_rx
{
    Header_t header;
    /*真正有效的随机数*/
    uint8_t true_random[16];
    uint16_t _rsv;
    uint16_t crc;
} Ask_random_rx_t;

/*数据鉴权，挑战数和挑战结果用到的结构体，收发都会用到*/
typedef struct Key_authentication
{
    Header_t header;

    /*发送方使用该结构体时，作为挑战数*/
    /*接收方返回数据，作为挑战结果*/
    uint8_t number[32];
    uint16_t _rsv;
    uint16_t crc;
} Key_authentication_t;
/*查表同步秘钥*/
typedef struct Ask_aes_table_tx
{
    Header_t header;

    /*发送方使用该结构体时，作为挑战数*/
    /*接收方返回数据，作为挑战结果*/
    /*随机数B区*/
    uint8_t number_a[32];
    /*key索引*/
    uint16_t key_index;
    /*iv索引*/
    uint16_t iv_index;
    /*随机数B区*/
    uint8_t number_b[32];
    uint16_t _rsv;
    uint16_t crc;
} Ask_aes_table_tx_t;

typedef struct Ask_aes_table_rx
{
    Header_t header;

    /*发送方使用该结构体时，作为挑战数*/
    /*接收方返回数据，作为挑战结果*/
    /*随机数B区*/
    uint8_t number_a[32];
    /*查表得到的key*/
    uint8_t key[32];
    /*查表得到的iv*/
    uint8_t iv[16];
    /*随机数B区*/
    uint8_t number_b[32];
    uint16_t _rsv;
    uint16_t crc;
} Ask_aes_table_rx_t;

/*擦除flash结构体*/
typedef struct Flash_erase
{
    Header_t header;

    /*需要擦除的数据*/
    uint16_t page_num;
    uint16_t crc;
} Flash_erase_t;

/*读flash用发送结构体*/
typedef struct Flash_read_tx
{
    /* data */
    Header_t header;

    uint16_t page_num;
    /*flash共512字节，page_index代表第几个128块*/
    uint16_t page_index;

    uint16_t _rsv;
    uint16_t crc;
} Flash_read_tx_t;

/*读flash用接收结构体*/
typedef struct Flash_read_rx
{
    /* data */
    Header_t header;

    uint16_t page_num;
    /*flash共512字节，page_index代表第几个128块*/
    uint16_t page_index;
    /*发送的数据*/
    uint8_t FLASHDATA[128];

    uint16_t _rsv;
    uint16_t crc;
} Flash_read_rx_t;

/*写flash用发送结构体*/
typedef struct Flash_write_tx
{
    /* data */
    Header_t header;

    uint16_t page_num;
    /*flash共512字节，page_index代表第几个128块*/
    uint16_t page_index;
    /*发送的数据*/
    uint8_t FLASHDATA[128];

    uint16_t _rsv;
    uint16_t crc;
} Flash_write_tx_t;
/*写flash用接收结构体*/
typedef struct Flash_write_rx
{
    /* data */
    Header_t header;

    uint16_t page_num;
    /*flash共512字节，page_index代表第几个128块*/
    uint16_t page_index;

    uint16_t _rsv;
    uint16_t crc;
} Flash_write_rx_t;

enum
{
    ASK_RANDOM = 0,
    SET_AES_IV,
    KEY_AUTHENTICATION,
    RANDOM_MESSAGE,
    ASK_AES_KEY_IV,
    ERASE_FLASH = 0x51,
    READ_FLASH,
    WRITE_FLASH,
};

void alc_func_init(void);
bool send_cmd(void);

#endif
