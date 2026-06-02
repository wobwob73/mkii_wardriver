/*
 * usbd_cdc_if.c — CDC class interface: line coding, control, and
 * receive-callback hook. Receive bytes are pushed into the global
 * g_usb_cdc_rx_ring SPSC ring declared in main.h; the MKII App/ layer
 * drains it via pal_usb_cdc_read().
 */

#include "usbd_cdc_if.h"
#include "usbd_cdc.h"
#include "main.h"

#include <string.h>

extern USBD_HandleTypeDef hUsbDeviceFS;

static uint8_t UserRxBufferFS[APP_RX_DATA_SIZE];
static uint8_t UserTxBufferFS[APP_TX_DATA_SIZE];

static USBD_CDC_LineCodingTypeDef line_coding = {
    .bitrate = 230400,
    .format = 0,
    .paritytype = 0,
    .datatype = 8,
};

static int8_t CDC_Init_FS(void);
static int8_t CDC_DeInit_FS(void);
static int8_t CDC_Control_FS(uint8_t cmd, uint8_t *pbuf, uint16_t length);
static int8_t CDC_Receive_FS(uint8_t *pbuf, uint32_t *Len);

USBD_CDC_ItfTypeDef USBD_Interface_fops_FS = {
    CDC_Init_FS,
    CDC_DeInit_FS,
    CDC_Control_FS,
    CDC_Receive_FS,
};

static int8_t CDC_Init_FS(void) {
    USBD_CDC_SetTxBuffer(&hUsbDeviceFS, UserTxBufferFS, 0);
    USBD_CDC_SetRxBuffer(&hUsbDeviceFS, UserRxBufferFS);
    return USBD_OK;
}

static int8_t CDC_DeInit_FS(void) {
    return USBD_OK;
}

static int8_t CDC_Control_FS(uint8_t cmd, uint8_t *pbuf, uint16_t length) {
    switch (cmd) {
        case CDC_SET_LINE_CODING:
            if (length >= 7) {
                line_coding.bitrate = (uint32_t)(pbuf[0] | (pbuf[1] << 8) |
                                                 (pbuf[2] << 16) | (pbuf[3] << 24));
                line_coding.format     = pbuf[4];
                line_coding.paritytype = pbuf[5];
                line_coding.datatype   = pbuf[6];
            }
            break;
        case CDC_GET_LINE_CODING:
            if (length >= 7) {
                pbuf[0] = (uint8_t)(line_coding.bitrate);
                pbuf[1] = (uint8_t)(line_coding.bitrate >> 8);
                pbuf[2] = (uint8_t)(line_coding.bitrate >> 16);
                pbuf[3] = (uint8_t)(line_coding.bitrate >> 24);
                pbuf[4] = line_coding.format;
                pbuf[5] = line_coding.paritytype;
                pbuf[6] = line_coding.datatype;
            }
            break;
        case CDC_SET_CONTROL_LINE_STATE:
            /* DTR/RTS not used. */
            break;
        default:
            break;
    }
    return USBD_OK;
}

static int8_t CDC_Receive_FS(uint8_t *pbuf, uint32_t *Len) {
    uint32_t n = *Len;
    for (uint32_t i = 0; i < n; i++) {
        uint16_t w = g_usb_cdc_rx_w;
        uint16_t next = (uint16_t)((w + 1) % USB_CDC_RX_BUF_SZ);
        if (next == g_usb_cdc_rx_r) {
            /* Ring full — drop and keep going so the host doesn't NAK. */
            break;
        }
        g_usb_cdc_rx_ring[w] = pbuf[i];
        g_usb_cdc_rx_w = next;
    }
    USBD_CDC_SetRxBuffer(&hUsbDeviceFS, &pbuf[0]);
    USBD_CDC_ReceivePacket(&hUsbDeviceFS);
    return USBD_OK;
}

uint8_t CDC_Transmit_FS(uint8_t *Buf, uint16_t Len) {
    USBD_CDC_HandleTypeDef *hcdc = (USBD_CDC_HandleTypeDef *)hUsbDeviceFS.pClassData;
    if (!hcdc) return USBD_FAIL;
    if (hcdc->TxState != 0) return USBD_BUSY;
    USBD_CDC_SetTxBuffer(&hUsbDeviceFS, Buf, Len);
    return USBD_CDC_TransmitPacket(&hUsbDeviceFS);
}
