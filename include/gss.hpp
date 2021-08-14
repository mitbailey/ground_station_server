/**
 * @file gss.hpp
 * @author Mit Bailey (mitbailey99@gmail.com)
 * @brief 
 * @version 0.1
 * @date 2021.07.26
 * 
 * @copyright Copyright (c) 2021
 * 
 */

#ifndef GSS_HPP
#define GSS_HPP

#include <arpa/inet.h>
#include "network.hpp"

#define LISTENING_IP_ADDRESS "127.0.0.1" // hostname -I
#define LISTENING_SOCKET_TIMEOUT 20
#define NUM_PORTS 5 // How many ports to open

/**
 * @brief Enumeration representing what client each RX thread is listening for.
 * 
 */
enum LISTEN_FOR
{
    LF_ERROR = -1,
    LF_CLIENT = 0,
    LF_ROOF_UHF,
    LF_ROOF_XBAND,
    LF_HAYSTACK,
    LF_TRACK
};

/**
 * @brief Data structure used to store arguments for rx_threads as a void pointer.
 * 
 */
typedef struct
{
    NetDataServer *network_data[4];
    pthread_t pid[4];
} global_data_t;

/**
 * @brief Thread which waits to receive network data.
 * 
 * Four of these are started, one for each expected possible client.
 * 
 * @return void* NULL
 */
void *gss_network_rx_thread(void *);

/**
 * @brief Generates a 16-bit CRC for the given data.
 * 
 * This is the CCITT CRC 16 polynomial X^16  + X^12  + X^5  + 1.
 * This works out to be 0x1021, but the way the algorithm works
 * lets us use 0x8408 (the reverse of the bit pattern).  The high
 * bit is always assumed to be set, thus we only use 16 bits to
 * represent the 17 bit value.
 * 
 * This is the same crc16 function used on-board SPACE-HAUC (line 116):
 * https://github.com/SPACE-HAUC/uhf_modem/blob/aa361d13cf1cef9b295a6cd5e2d51c7ae6d59637/uhf_modem.h
 * 
 * @param data_p 
 * @param length 
 * @return uint16_t 
 */
static inline uint16_t crc16(unsigned char *data_p, uint16_t length)
{
#define CRC16_POLY 0x8408
    unsigned char i;
    unsigned int data;
    unsigned int crc = 0xffff;

    if (length == 0)
        return (~crc);

    do
    {
        for (i = 0, data = (unsigned int)0xff & *data_p++;
             i < 8;
             i++, data >>= 1)
        {
            if ((crc & 0x0001) ^ (data & 0x0001))
                crc = (crc >> 1) ^ CRC16_POLY;
            else
                crc >>= 1;
        }
    } while (--length);

    crc = ~crc;
    data = crc;
    crc = (crc << 8) | (data >> 8 & 0xff);

    return (crc);
}

#endif // GSS_HPP