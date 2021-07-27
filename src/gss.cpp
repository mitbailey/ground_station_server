/**
 * @file gss.cpp
 * @author Mit Bailey (mitbailey99@gmail.com)
 * @brief 
 * @version 0.1
 * @date 2021.07.26
 * 
 * @copyright Copyright (c) 2021
 * 
 */

#include <stdio.h>
#include <string.h>
#include <ifaddrs.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <pthread.h>
#include "gss.hpp"
#include "gss_debug.hpp"

/// NetworkData Class
NetworkData::NetworkData()
{
    connection_ready = false;
    socket = -1;
    serv_addr->sin_family = AF_INET;
    port = LISTENING_PORT;
}
/// ///

/// ClientServerFrame Class
ClientServerFrame::ClientServerFrame(CLIENTSERVER_FRAME_TYPE type, int payload_size)
{
    if (type < 0)
    {
        printf("ClientServerFrame initialized with error type (%d).\n", (int)type);
        return;
    }

    if (payload_size > CLIENTSERVER_MAX_PAYLOAD_SIZE)
    {
        printf("Cannot allocate payload larger than %d bytes.\n", CLIENTSERVER_MAX_PAYLOAD_SIZE);
        return;
    }

    this->payload_size = payload_size;
    this->type = type;
    // TODO: Set the mode properly.
    mode = CS_MODE_ERROR;
    crc1 = -1;
    crc2 = -1;
    guid = CLIENTSERVER_FRAME_GUID;
    netstat = 0; // Will be set by the server.
    termination = 0xAAAA;

    memset(payload, 0x0, this->payload_size);
}

int ClientServerFrame::storePayload(CLIENTSERVER_FRAME_ENDPOINT endpoint, void *data, int size)
{
    if (size > payload_size)
    {
        printf("Cannot store data of size larger than allocated payload size (%d > %d).\n", size, payload_size);
        return -1;
    }

    memcpy(payload, data, size);

    crc1 = crc16(payload, payload_size);
    crc2 = crc16(payload, payload_size);

    this->endpoint = endpoint;

    // TODO: Placeholder until I figure out when / why to set mode to TX or RX.
    mode = CS_MODE_RX;

    return 1;
}

int ClientServerFrame::retrievePayload(unsigned char *data_space, int size)
{
    if (size != payload_size)
    {
        printf("Data space size not equal to payload size (%d != %d).\n", size, payload_size);
        return -1;
    }

    memcpy(data_space, payload, payload_size);

    return 1;
}

int ClientServerFrame::checkIntegrity()
{
    if (guid != CLIENTSERVER_FRAME_GUID)
    {
        return -1;
    }
    else if (endpoint < 0)
    {
        return -2;
    }
    else if (mode < 0)
    {
        return -3;
    }
    else if (payload_size < 0 || payload_size > CLIENTSERVER_MAX_PAYLOAD_SIZE)
    {
        return -4;
    }
    else if (type < 0)
    {
        return -5;
    }
    else if (crc1 != crc2)
    {
        return -6;
    }
    else if (crc1 != crc16(payload, payload_size))
    {
        return -7;
    }
    else if (termination != 0xAAAA)
    {
        return -8;
    }

    return 1;
}

void ClientServerFrame::print()
{
    printf("GUID ------------ 0x%04x\n", guid);
    printf("Endpoint -------- %d\n", endpoint);
    printf("Mode ------------ %d\n", mode);
    printf("Payload Size ---- %d\n", payload_size);
    printf("Type ------------ %d\n", type);
    printf("CRC1 ------------ 0x%04x\n", crc1);
    printf("Payload ---- (HEX)");
    for (int i = 0; i < payload_size; i++)
    {
        printf(" 0x%04x", payload[i]);
    }
    printf("\n");
    printf("CRC2 ------------ 0x%04x\n", crc2);
    printf("Termination ----- 0x%04x\n", termination);
}

ssize_t ClientServerFrame::sendFrame(NetworkData *network_data)
{
    if (!(network_data->connection_ready))
    {
        dbprintlf(YELLOW_FG "Connection is not ready.");
        return -1;
    }

    if (!checkIntegrity())
    {
        dbprintlf(YELLOW_FG "Integrity check failed, send aborted.");
        return -1;
    }

    printf("Sending the following:\n");
    print();

    return send(network_data->socket, this, sizeof(ClientServerFrame), 0);
}
/// ///

int find_ipv4(char *buffer, ssize_t buffer_size)
{
    struct ifaddrs *addr, *temp_addr;
    getifaddrs(&addr);
    for (temp_addr = addr; temp_addr != NULL; temp_addr = temp_addr->ifa_next)
    {
        struct sockaddr_in *addr_in = (struct sockaddr_in *)temp_addr->ifa_addr;
        inet_ntop(AF_INET, &addr_in->sin_addr, buffer, buffer_size);

        // If the IP address is the IPv4...
        if (buffer[0] == '1' && buffer[1] == '7' && buffer[2] == '2' && buffer[3] == '.')
        {
            dbprintlf(CYAN_FG "%s", buffer);
            return 1;
        }
        else
        {
            dbprintlf(MAGENTA_FG "%s", buffer);
        }
    }

    return 0;
}

/**
 * @brief 
 * 
 * From:
 * https://github.com/sunipkmukherjee/comic-mon/blob/master/guimain.cpp
 * with minor modifications.
 * 
 * @param socket 
 * @param address 
 * @param socket_size 
 * @param tout_s 
 * @return int 
 */
int connect_w_tout(int socket, const struct sockaddr *address, socklen_t socket_size, int tout_s)
{
    int res;
    long arg;
    fd_set myset;
    struct timeval tv;
    int valopt;
    socklen_t lon;

    // Set non-blocking.
    if ((arg = fcntl(socket, F_GETFL, NULL)) < 0)
    {
        dbprintlf(RED_FG "Error fcntl(..., F_GETFL)");
        erprintlf(errno);
        return -1;
    }
    arg |= O_NONBLOCK;
    if (fcntl(socket, F_SETFL, arg) < 0)
    {
        dbprintlf(RED_FG "Error fcntl(..., F_SETFL)");
        erprintlf(errno);
        return -1;
    }

    // Trying to connect with timeout.
    res = connect(socket, address, socket_size);
    if (res < 0)
    {
        if (errno == EINPROGRESS)
        {
            dbprintlf(YELLOW_FG "EINPROGRESS in connect() - selecting");
            do
            {
                if (tout_s > 0)
                {
                    tv.tv_sec = tout_s;
                }
                else
                {
                    tv.tv_sec = 1; // Minimum 1 second.
                }
                tv.tv_usec = 0;
                FD_ZERO(&myset);
                FD_SET(socket, &myset);
                res = select(socket + 1, NULL, &myset, NULL, &tv);
                if (res < 0 && errno != EINTR)
                {
                    dbprintlf(RED_FG "Error connecting.");
                    erprintlf(errno);
                    return -1;
                }
                else if (res > 0)
                {
                    // Socket selected for write.
                    lon = sizeof(int);
                    if (getsockopt(socket, SOL_SOCKET, SO_ERROR, (void *)(&valopt), &lon) < 0)
                    {
                        dbprintlf(RED_FG "Error in getsockopt()");
                        erprintlf(errno);
                        return -1;
                    }

                    // Check the value returned...
                    if (valopt)
                    {
                        dbprintlf(RED_FG "Error in delayed connection()");
                        erprintlf(valopt);
                        return -1;
                    }
                    break;
                }
                else
                {
                    dbprintlf(RED_FG "Timeout in select(), cancelling!");
                    return -1;
                }
            } while (1);
        }
        else
        {
            fprintf(stderr, "Error connecting %d - %s\n", errno, strerror(errno));
            dbprintlf(RED_FG "Error connecting.");
            erprintlf(errno);
            return -1;
        }
    }
    // Set to blocking mode again...
    if ((arg = fcntl(socket, F_GETFL, NULL)) < 0)
    {
        dbprintlf("Error fcntl(..., F_GETFL)");
        erprintlf(errno);
        return -1;
    }
    arg &= (~O_NONBLOCK);
    if (fcntl(socket, F_SETFL, arg) < 0)
    {
        dbprintlf("Error fcntl(..., F_GETFL)");
        erprintlf(errno);
        return -1;
    }
    return socket;
}

int gs_transmit(NetworkData *network_data, CLIENTSERVER_FRAME_TYPE type, CLIENTSERVER_FRAME_ENDPOINT endpoint, void *data, int data_size)
{
    if (data_size < 0)
    {
        printf("Error: data_size is %d.\n", data_size);
        printf("Cancelling transmit.\n");
        return -1;
    }

    // Create a ClientServerFrame to send our data in.
    ClientServerFrame *clientserver_frame = new ClientServerFrame(type, data_size);
    clientserver_frame->storePayload(endpoint, data, data_size);

    clientserver_frame->sendFrame(network_data);

    return 1;
}

void *gss_rx_thread(void *rx_thread_data_vp)
{
    rx_thread_data_t *rx_thread_data = (rx_thread_data_t *)rx_thread_data_vp;

    LISTEN_FOR listen_for = LF_ERROR;
    int t_index = -1;
    pthread_t thread_id = pthread_self();

    for (int i = 0; i < 4; i++)
    {
        if (thread_id == rx_thread_data->pid[i])
        {
            listen_for = (LISTEN_FOR)i;
            t_index = i;
        }
    }

    char t_tag[32];

    switch (listen_for)
    {
    case LF_CLIENT:
    {
        strcpy(t_tag, "[RXT_GUICLIENT] ");
        dbprintlf("%sThread (id:%d) listening for GUI Client.", t_tag, (int)thread_id);

        // TODO: Place thread-listening-type specific code in this location in each switch-case (ie, only common code should appear after this switch statement).

        break;
    }
    case LF_ROOF_UHF:
    {
        strcpy(t_tag, "[RXT_ROOFUHF] ");
        dbprintlf("%sThread (id:%d) listening for Roof UHF.", t_tag, (int)thread_id);

        break;
    }
    case LF_ROOF_XBAND:
    {
        strcpy(t_tag, "[RXT_ROOFXBAND] ");
        dbprintlf("%sThread (id:%d) listening for Roof X-Band.", t_tag, (int)thread_id);

        break;
    }
    case LF_HAYSTACK:
    {
        strcpy(t_tag, "[RXT_HAYSTACK] ");
        dbprintlf("%sThread (id:%d) listening for Haystack.", t_tag, (int)thread_id);

        break;
    }
    case LF_ERROR:
    default:
    {
        dbprintlf(FATAL "[RXT_ERROR] Thread (id:%d) not listening for any valid sender.", (int)thread_id);
        return NULL;
    }
    }

    // Socket prep.
    int listening_socket, accepted_socket, socket_size;
    struct sockaddr_in listening_address, accepted_address;
    int buffer_size = sizeof(ClientServerFrame);
    unsigned char buffer[buffer_size + 1];
    memset(buffer, 0x0, buffer_size);

    // Create socket.
    listening_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (listening_socket == -1)
    {
        dbprintlf(FATAL "%sCould not create socket.", t_tag);
        return NULL;
    }
    dbprintlf(GREEN_FG "%sSocket created.", t_tag);

    // network_data[i]->ipv4 and ->port already set by main()

    listening_address.sin_family = AF_INET;
    // TODO: Should probably not accept just any address.
    listening_address.sin_addr.s_addr = INADDR_ANY;
    listening_address.sin_port = htons(rx_thread_data->network_data[t_index]->port);

    // Set the IP address.
    if (inet_pton(AF_INET, rx_thread_data->network_data[t_index]->ipv4, &listening_address.sin_addr) <= 0)
    {
        dbprintlf(FATAL "%sInvalid address; address not supported.", t_tag);
        return NULL;
    }

    // Set the timeout for recv, which will allow us to reconnect to poorly disconnected clients.
    struct timeval timeout;
    timeout.tv_sec = LISTENING_SOCKET_TIMEOUT;
    timeout.tv_usec = 0;
    setsockopt(listening_socket, SOL_SOCKET, SO_RCVTIMEO, (const char *)&timeout, sizeof(timeout));

    // Bind.
    while (bind(listening_socket, (struct sockaddr *)&listening_address, sizeof(listening_address)) < 0)
    {
        dbprintlf(RED_FG "%sError: Port binding failed.", t_tag);
        dbprintlf(YELLOW_FG "%s>>> ", t_tag);
        perror("bind");
        sleep(5);
    }
    dbprintlf(GREEN_FG "%sBound to port.", t_tag);

    // Listen.
    listen(listening_socket, 3);

    while (rx_thread_data->network_data[t_index]->rx_active)
    {
        int read_size = 0;

        // Accept an incoming connection.
        dbprintlf("%sWaiting for incoming connections...", t_tag);
        socket_size = sizeof(struct sockaddr_in);

        // Accept connection from an incoming client.
        accepted_socket = accept(listening_socket, (struct sockaddr *)&accepted_address, (socklen_t *)&socket_size);
        if (accepted_socket < 0)
        {
            if (errno == EAGAIN)
            {
                // Waiting for connection timed-out.
                continue;
            }
            else
            {
                dbprintlf(YELLOW_FG "%s>>> ", t_tag);
                perror("accept failed");
                continue;
            }
        }
        dbprintlf(CYAN_FG "%sConnection accepted.", t_tag);

        // We are now connected.

        // Read from the socket.

        while (read_size >= 0 && rx_thread_data->network_data[t_index]->rx_active)
        {
            dbprintlf("%sBeginning recv... (last read: %d bytes)", t_tag, read_size);
            read_size = recv(accepted_socket, buffer, buffer_size, 0);
            if (read_size > 0)
            {
                dbprintf("%sRECEIVED (hex): ", t_tag);
                for (int i = 0; i < read_size; i++)
                {
                    printf("%02x", buffer[i]);
                }
                printf("(END)\n");

                // TODO: Parse the data.
                ClientServerFrame *clientserver_frame = (ClientServerFrame *)buffer;

                // Check if we've received data in the form of a ClientServerFrame.
                if (clientserver_frame->checkIntegrity() < 0)
                {
                    dbprintlf("%sIntegrity check failed (%d).", t_tag, clientserver_frame->checkIntegrity());
                    continue;
                }
                dbprintlf("%sIntegrity check successful.", t_tag);

                // Do not extract the payload unless required.
                switch (clientserver_frame->getEndpoint())
                {
                case CS_ENDPOINT_SERVER:
                {
                    break;
                }
                case CS_ENDPOINT_CLIENT:
                {
                    break;
                }
                case CS_ENDPOINT_ROOFUHF:
                {
                    break;
                }
                case CS_ENDPOINT_ROOFXBAND:
                {
                    break;
                }
                case CS_ENDPOINT_HAYSTACK:
                {
                    break;
                }
                case CS_ENDPOINT_ERROR:
                default:
                {
                    break;
                }
                }
            }
            else
            {
                break;
            }
        }
        if (read_size == 0)
        {
            dbprintlf(CYAN_BG "%sClient closed connection.", t_tag);
            continue;
        }
        else if (errno == EAGAIN)
        {
            dbprintlf(YELLOW_BG "%sActive connection timed-out (%d).", t_tag, read_size);
            continue;
        }
    }

    if (!rx_thread_data->network_data[t_index]->rx_active)
    {
        dbprintlf(YELLOW_FG "%sReceive deactivated.", t_tag);
    }

    return NULL;
}