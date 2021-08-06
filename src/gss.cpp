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
#include "network.hpp"
#include "gss.hpp"
#include "meb_debug.hpp"

int gss_find_ipv4(char *buffer, ssize_t buffer_size)
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
    int listening_socket, socket_size;
    struct sockaddr_in listening_address, accepted_address;
    int buffer_size = sizeof(NetworkFrame) + 16;
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

    listening_address.sin_family = AF_INET;
    // Its fine to accept just any address.
    listening_address.sin_addr.s_addr = INADDR_ANY;

    // Calculate and set port.
    network_data->listening_port = LISTENING_PORT_BASE + (10 * t_index);
    listening_address.sin_port = htons(network_data->listening_port);

    // Set the timeout for recv, which will allow us to reconnect to poorly disconnected clients.
    struct timeval timeout;
    timeout.tv_sec = LISTENING_SOCKET_TIMEOUT;
    timeout.tv_usec = 0;
    setsockopt(listening_socket, SOL_SOCKET, SO_RCVTIMEO, (const char *)&timeout, sizeof(timeout));

    // This allows us to crash the server, reboot, and still get all of our socket connections ready even thought theyre in a TIME_WAIT state.
    int enable = 1;
    setsockopt(listening_socket, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int));

    // Bind.
    while (bind(listening_socket, (struct sockaddr *)&listening_address, sizeof(listening_address)) < 0)
    {
        dbprintlf(RED_FG "%sError: Port binding failed.", t_tag);
        dbprintf(YELLOW_FG "%s>>> ", t_tag);
        perror("bind");
        sleep(5);
    }
    dbprintlf(GREEN_FG "%sBound to port %d.", t_tag, network_data->listening_port);

    // Listen.
    listen(listening_socket, 3);

    while (network_data->rx_active)
    {
        int read_size = 0;

        // Accept an incoming connection.
        socket_size = sizeof(struct sockaddr_in);

        // Accept connection from an incoming client.
        network_data->socket = accept(listening_socket, (struct sockaddr *)&accepted_address, (socklen_t *)&socket_size);
        if (network_data->socket < 0)
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
        network_data->connection_ready = true;

        // Read from the socket.

        while (read_size >= 0 && network_data->rx_active)
        {
            dbprintlf("%sBeginning recv... (last read: %d bytes)", t_tag, read_size);
            memset(buffer, 0x0, buffer_size);
            read_size = recv(network_data->socket, buffer, buffer_size, 0);
            if (read_size > 0)
            {
                dbprintf("%sRECEIVED (hex): ", t_tag);
                for (int i = 0; i < read_size; i++)
                {
                    printf("%02x", buffer[i]);
                }
                printf("(END)\n");

                // Parse the data.
                NetworkFrame *network_frame = (NetworkFrame *)buffer;

                // Check if we've received data in the form of a NetworkFrame.
                if (network_frame->checkIntegrity() < 0)
                {
                    dbprintlf("%sIntegrity check failed (%d).", t_tag, network_frame->checkIntegrity());
                    continue;
                }
                dbprintlf("%sIntegrity check successful.", t_tag);

                switch (network_frame->getEndpoint())
                {
                case CS_ENDPOINT_SERVER:
                {
                    // Ride ends here, at the server.
                    // NOTE: Parse and do something. maybe, we'll see.
                    dbprintlf(CYAN_FG "Received a packet for the server!");
                    if (network_frame->getType() == CS_TYPE_NULL)
                    {
                        dbprintlf("Received a null (status) packet, responding.");
                        // Send the null frame to whomever asked for it.
                        network_frame->storePayload((NETWORK_FRAME_ENDPOINT)t_index, NULL, 0);

                        network_frame->setNetstat(
                            rx_thread_data->network_data[0]->connection_ready,
                            rx_thread_data->network_data[1]->connection_ready,
                            rx_thread_data->network_data[2]->connection_ready,
                            rx_thread_data->network_data[3]->connection_ready);

                        // Transmit the clientserver_frame, sending the network_data for the connection down which we would like it to be sent.
                        if (network_frame->sendFrame(rx_thread_data->network_data[(int)network_frame->getEndpoint()]) < 0)
                        {
                            dbprintlf(RED_FG "Send failed!");
                        }
                    }
                    else
                    {
                        dbprintlf(RED_FG "Frame addressed to server but was not a Null (status) frame!");
                    }
                    break;
                }
                case CS_ENDPOINT_CLIENT:
                case CS_ENDPOINT_ROOFUHF:
                case CS_ENDPOINT_ROOFXBAND:
                case CS_ENDPOINT_HAYSTACK:
                {
                    dbprintlf("Passing along frame.");
                    network_frame->setNetstat(
                        rx_thread_data->network_data[0]->connection_ready,
                        rx_thread_data->network_data[1]->connection_ready,
                        rx_thread_data->network_data[2]->connection_ready,
                        rx_thread_data->network_data[3]->connection_ready);

                    // Transmit the clientserver_frame, sending the network_data for the connection down which we would like it to be sent.
                    if (network_frame->sendFrame(rx_thread_data->network_data[(int)network_frame->getEndpoint()]) < 0)
                    {
                        dbprintlf(RED_FG "Send failed!");
                    }
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
            network_data->connection_ready = false;
            continue;
        }
        else if (errno == EAGAIN)
        {
            dbprintlf(YELLOW_BG "%sActive connection timed-out (%d).", t_tag, read_size);
            network_data->connection_ready = false;
            continue;
        }
    }

    if (!rx_thread_data->network_data[t_index]->rx_active)
    {
        dbprintlf(YELLOW_FG "%sReceive deactivated.", t_tag);
    }

    return NULL;
}