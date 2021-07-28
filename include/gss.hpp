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

#define CLIENTSERVER_FRAME_GUID 0x1A1C
#define CLIENTSERVER_MAX_PAYLOAD_SIZE 0x64
#define LISTENING_IP_ADDRESS "127.0.0.1" // hostname -I
#define LISTENING_PORT_BASE 54200 // "Base" port, others are factors of this.
#define LISTENING_SOCKET_TIMEOUT 20

enum CLIENTSERVER_FRAME_TYPE
{
    // Something is wrong.
    CS_TYPE_ERROR = -1,
    // Blank, used for holding open the socket and retrieving status data.
    CS_TYPE_NULL = 0,
    // Good or back acknowledgements.
    CS_TYPE_ACK = 1,
    CS_TYPE_NACK = 2,
    // Configure ground radios.
    CS_TYPE_CONFIG_UHF = 3,
    CS_TYPE_CONFIG_XBAND = 4,
    // Most communications will be _DATA.
    CS_TYPE_DATA = 5,
    // Poll the ground radios of their status. ~Now happens every frame.
    // CS_TYPE_STATUS = 6
};

enum CLIENTSERVER_FRAME_ENDPOINT
{
    CS_ENDPOINT_ERROR = -1,
    CS_ENDPOINT_CLIENT = 0,
    CS_ENDPOINT_ROOFUHF,
    CS_ENDPOINT_ROOFXBAND,
    CS_ENDPOINT_HAYSTACK,
    CS_ENDPOINT_SERVER
    // CS_ENDPOINT_SPACEHAUC // probably dont need this one, SH can be inferred (all nonconfig cs_frames sent to TX radios...)
};

enum CLIENTSERVER_FRAME_MODE
{
    CS_MODE_ERROR = -1,
    CS_MODE_RX = 0,
    CS_MODE_TX = 1
};

class NetworkData
{
public:
    NetworkData();

    // Network
    int socket;
    struct sockaddr_in destination_addr[1];
    bool connection_ready;
    char listening_ipv4[32];
    int listening_port;

    // Booleans
    bool rx_active; // Only able to receive when this is true.
};

class ClientServerFrame
{
public:
    /**
     * @brief Sets the payload_size, type, GUID, and termination values.
     * 
     * @param payload_size The desired payload size.
     * @param type The type of data this frame will carry (see: CLIENTSERVER_FRAME_TYPE).
     */
    ClientServerFrame(CLIENTSERVER_FRAME_TYPE type, int payload_size);

    // ~ClientServerFrame();

    /**
     * @brief Copies data to the payload.
     * 
     * Returns and error if the passed data size does not equal the internal payload_size variable set during class construction.
     * 
     * Sets the CRC16s.
     * 
     * @param endpoint The final destination for the payload (see: CLIENTSERVER_FRAME_ENDPOINT).
     * @param data Data to be copied into the payload.
     * @param size Size of the data to be copied.
     * @return int Positive on success, negative on failure.
     */
    int storePayload(CLIENTSERVER_FRAME_ENDPOINT endpoint, void *data, int size);

    /**
     * @brief Copies payload to the passed space in memory.
     * 
     * @param data_space Pointer to memory into which the payload is copied.
     * @param size The size of the memory space being passed.
     * @return int Positive on success, negative on failure.
     */
    int retrievePayload(unsigned char *data_space, int size);

    int getPayloadSize() { return payload_size; };

    CLIENTSERVER_FRAME_TYPE getType() { return type; };

    CLIENTSERVER_FRAME_ENDPOINT getEndpoint() { return endpoint; };

    uint8_t getNetstat() { return netstat; };

    /**
     * @brief Checks the validity of itself.
     * 
     * @return int Positive if valid, negative if invalid.
     */
    int checkIntegrity();

    /**
     * @brief Prints the class.
     * 
     * @return int 
     */
    void print();

    /**
     * @brief Sends itself using the network data passed to it.
     * 
     * @return ssize_t Number of bytes sent if successful, negative on failure. 
     */
    ssize_t sendFrame(NetworkData *network_data);

private:
    // 0x????
    uint16_t guid;
    // Where is this going?
    CLIENTSERVER_FRAME_ENDPOINT endpoint;
    // RX or TX?
    CLIENTSERVER_FRAME_MODE mode;
    // Variably sized payload, this value tracks the size.
    int payload_size;
    // NULL, ACK, NACK, CONFIG, DATA, STATUS
    CLIENTSERVER_FRAME_TYPE type;
    // CRC16 of payload.
    uint16_t crc1;
    // Constant sized payload.
    unsigned char payload[CLIENTSERVER_MAX_PAYLOAD_SIZE];
    uint16_t crc2;
    // Network Status Information - Only read by the client, only set by the server.
    uint8_t netstat; // Bitmask - 0:Client, 1:RoofUHF, 2: RoofXB, 3: Haystack
    // 0xAAAA
    uint16_t termination;
};

enum LISTEN_FOR
{
    LF_ERROR = -1,
    LF_CLIENT = 0,
    LF_ROOF_UHF,
    LF_ROOF_XBAND,
    LF_HAYSTACK
};

/**
 * @brief Data structure used to store arguments for rx_threads as a void pointer.
 * 
 */
typedef struct
{  
    NetworkData *network_data[4];
    pthread_t pid[4];
} rx_thread_data_t;

/**
 * @brief 
 * 
 * @param buffer 
 * @param buffer_size 
 * @return int 
 */
int find_ipv4(char *buffer, ssize_t buffer_size);

/**
 * @brief 
 * 
 * @param socket 
 * @param address 
 * @param socket_size 
 * @param tout_s 
 * @return int 
 */
int connect_w_tout(int socket, const struct sockaddr *address, socklen_t socket_size, int tout_s);

/**
 * @brief 
 * 
 * @param network_data 
 * @param clientserver_frame 
 * @return int 
 */
int gss_transmit(NetworkData **network_data, ClientServerFrame *clientserver_frame);

/**
 * @brief 
 * 
 * @return void* 
 */
void *gss_rx_thread(void *);

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