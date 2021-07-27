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
    destination_addr->sin_family = AF_INET;
    listening_port = LISTENING_PORT;
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

int gss_transmit(ClientServerFrame *clientserver_frame)
{
    if (!clientserver_frame->checkIntegrity())
    {
        dbprintlf(RED_FG "Transmission cancelled, bad frame integrity.");
        return -1;
    }

    int endpoint_index = (int)clientserver_frame->getEndpoint();

    if (endpoint_index > 3)
    {
        dbprintlf(RED_FG "Invalid endpoint index detected (%d).", endpoint_index);
        return -1;
    }

    // Open a connection to the endpoint.
    NetworkData* network_data = new NetworkData();
    // struct sockaddr_in dest_addr[1];
    // int sock = -1;
    network_data->destination_addr->sin_port = htons(LISTENING_PORT_BASE + (10 * endpoint_index));

    if ((network_data->socket = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        dbprintlf(RED_FG "Socket creation error.");
        return -1;
    }

    // if (inet_pton(AF_INET, destination_ips[endpoint_index], &network_data->destination_addr->sin_addr) <= 0)
    // {
    //     dbprintlf(RED_FG "Invalid address; address not supported (%d %s).", endpoint_index, destination_ips[endpoint_index]);
    //     return -1;
    // }

    if (connect_w_tout(network_data->socket, (struct sockaddr *) network_data->destination_addr, sizeof(network_data->destination_addr), 1) < 0)
    {
        dbprintlf(RED_FG "Connection failed!");
        return -1;
    }

    // Connection ready!
    network_data->connection_ready = true;
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

    // Makes my life easier.
    NetworkData *network_data = rx_thread_data->network_data[t_index];

    // Socket prep.
    int listening_socket, accepted_socket, socket_size;
    struct sockaddr_in listening_address, accepted_address;
    int buffer_size = sizeof(ClientServerFrame) + 16;
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
    // Its fine to accept just any address.
    listening_address.sin_addr.s_addr = INADDR_ANY;
    listening_address.sin_port = htons(network_data->listening_port);

    // Set the IP address.
    if (inet_pton(AF_INET, network_data->listening_ipv4, &listening_address.sin_addr) <= 0)
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
        dbprintf(YELLOW_FG "%s>>> ", t_tag);
        perror("bind");
        sleep(5);
    }
    dbprintlf(GREEN_FG "%sBound to port.", t_tag);

    // Listen.
    listen(listening_socket, 3);

    while (network_data->rx_active)
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

        while (read_size >= 0 && network_data->rx_active)
        {
            dbprintlf("%sBeginning recv... (last read: %d bytes)", t_tag, read_size);
            memset(buffer, 0x0, buffer_size);
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

                switch (clientserver_frame->getEndpoint())
                {
                case CS_ENDPOINT_SERVER:
                {
                    // Ride ends here, at the server.
                    // TODO: Parse and do something.
                    dbprintlf(CYAN_FG "Received a packet for the server!");
                    break;
                }
                case CS_ENDPOINT_CLIENT:
                case CS_ENDPOINT_ROOFUHF:
                case CS_ENDPOINT_ROOFXBAND:
                case CS_ENDPOINT_HAYSTACK:
                {
                    gss_transmit(clientserver_frame);
                    break;
                }
                case CS_ENDPOINT_ERROR:
                default:
                {
                    // Probably received nothing.
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