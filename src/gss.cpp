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

void *gss_network_rx_thread(void *global_vp)
{
    global_data_t *global = (global_data_t *)global_vp;

    LISTEN_FOR listen_for = LF_ERROR;
    int t_index = -1;
    pthread_t thread_id = pthread_self();

    for (int i = 0; i < NUM_PORTS; i++)
    {
        if (thread_id == global->pid[i])
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
        dbprintlf("%sThread (id:%lu) listening for GUI Client.", t_tag, (unsigned long)thread_id);

        break;
    }
    case LF_ROOF_UHF:
    {
        strcpy(t_tag, "[RXT_ROOFUHF] ");
        dbprintlf("%sThread (id:%lu) listening for Roof UHF.", t_tag, (unsigned long)thread_id);

        break;
    }
    case LF_ROOF_XBAND:
    {
        strcpy(t_tag, "[RXT_ROOFXBAND] ");
        dbprintlf("%sThread (id:%lu) listening for Roof X-Band.", t_tag, (unsigned long)thread_id);

        break;
    }
    case LF_HAYSTACK:
    {
        strcpy(t_tag, "[RXT_HAYSTACK] ");
        dbprintlf("%sThread (id:%lu) listening for Haystack.", t_tag, (unsigned long)thread_id);

        break;
    }
    case LF_TRACK:
    {
        strcpy(t_tag, "[RXT_TRACK] ");
        dbprintlf("%sThread (id:%lu) listening for Track.", t_tag, (unsigned long)thread_id);

        break;
    }
    case LF_ERROR:
    default:
    {
        dbprintlf(FATAL "[RXT_ERROR] Thread (id:%lu) not listening for any valid sender (%d).", (unsigned long)thread_id, t_index);
        return NULL;
    }
    }

    // Makes my life easier.
    NetDataServer *network_data = global->network_data[t_index];

    // Socket prep.
    int listening_socket, socket_size;
    struct sockaddr_in listening_address, accepted_address;
    int buffer_size = sizeof(NetFrame) + 16;
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
    network_data->listening_port = (int)NetPort::CLIENT + (10 * t_index);
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

    while (network_data->recv_active)
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
                dbprintlf("%sTimed out (NETSTAT %d %d %d %d %d).", t_tag,
                          global->network_data[LF_CLIENT]->connection_ready ? 1 : 0,
                          global->network_data[LF_ROOF_UHF]->connection_ready ? 1 : 0,
                          global->network_data[LF_ROOF_XBAND]->connection_ready ? 1 : 0,
                          global->network_data[LF_HAYSTACK]->connection_ready ? 1 : 0,
                          global->network_data[LF_TRACK]->connection_ready ? 1 : 0);
                network_data->connection_ready = false;
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

        while (read_size >= 0 && network_data->recv_active)
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
                NetFrame *network_frame = (NetFrame *)buffer;

                // Check if we've received data in the form of a NetworkFrame.
                if (network_frame->validate() < 0)
                {
                    dbprintlf("%sIntegrity check failed (%d).", t_tag, network_frame->validate());
                    continue;
                }
                dbprintlf("%sIntegrity check successful.", t_tag);

                switch (network_frame->getDestination())
                {
                case NetVertex::SERVER:
                {
                    // Ride ends here, at the server.
                    // NOTE: Parse and do something. maybe, we'll see.
                    dbprintlf(CYAN_FG "Received a packet for the server from ID:%d!", t_index);
                    if (network_frame->getType() == NetType::POLL)
                    {
                        dbprintlf("Received a status polling packet, responding.");

                        // Send the null frame to whomever asked for it.
                        NetFrame *netstat_frame = new NetFrame(NULL, 0, NetType::POLL, (NetVertex)t_index);

                        uint8_t netstat = 0x0;
                        netstat |= 0x80 * (global->network_data[LF_CLIENT]->connection_ready);
                        netstat |= 0x40 * (global->network_data[LF_ROOF_UHF]->connection_ready);
                        netstat |= 0x20 * (global->network_data[LF_ROOF_XBAND]->connection_ready);
                        netstat |= 0x10 * (global->network_data[LF_HAYSTACK]->connection_ready);
                        netstat |= 0x8 * (global->network_data[LF_TRACK]->connection_ready);

                        netstat_frame->setNetstat(netstat);

                        dbprintlf("%sNETSTAT %d %d %d %d %d (%d)", t_tag,
                                  global->network_data[LF_CLIENT]->connection_ready ? 1 : 0,
                                  global->network_data[LF_ROOF_UHF]->connection_ready ? 1 : 0,
                                  global->network_data[LF_ROOF_XBAND]->connection_ready ? 1 : 0,
                                  global->network_data[LF_HAYSTACK]->connection_ready ? 1 : 0,
                                  global->network_data[LF_TRACK]->connection_ready ? 1 : 0, netstat);

                        // Transmit the clientserver_frame, sending the network_data for the connection down which we would like it to be sent.
                        if (netstat_frame->sendFrame(global->network_data[t_index]) < 0)
                        {
                            dbprintlf(RED_FG "%sSend failed.", t_tag);
                        }
                    }
                    else
                    {
                        dbprintlf(RED_FG "%sFrame addressed to server but was not a polling status frame.", t_tag);
                    }
                    break;
                }
                case NetVertex::CLIENT:
                case NetVertex::ROOFUHF:
                case NetVertex::ROOFXBAND:
                case NetVertex::HAYSTACK:
                case NetVertex::TRACK:
                {
                    if (global->network_data[(int)network_frame->getDestination()]->connection_ready)
                    {
                        dbprintlf("%sPassing along frame.", t_tag);
                        uint8_t netstat = 0x0;
                        netstat |= 0x80 * (global->network_data[LF_CLIENT]->connection_ready);
                        netstat |= 0x40 * (global->network_data[LF_ROOF_UHF]->connection_ready);
                        netstat |= 0x20 * (global->network_data[LF_ROOF_XBAND]->connection_ready);
                        netstat |= 0x10 * (global->network_data[LF_HAYSTACK]->connection_ready);
                        netstat |= 0x8 * (global->network_data[LF_TRACK]->connection_ready);

                        network_frame->setNetstat(netstat);

                        dbprintlf("%sNETSTAT %d %d %d %d %d", t_tag,
                                  global->network_data[LF_CLIENT]->connection_ready ? 1 : 0,
                                  global->network_data[LF_ROOF_UHF]->connection_ready ? 1 : 0,
                                  global->network_data[LF_ROOF_XBAND]->connection_ready ? 1 : 0,
                                  global->network_data[LF_HAYSTACK]->connection_ready ? 1 : 0,
                                  global->network_data[LF_TRACK]->connection_ready ? 1 : 0);

                        // Transmit the NetFrame, sending the network_data for the connection down which we would like it to be sent.
                        if (network_frame->sendFrame(global->network_data[(int)network_frame->getDestination()]) < 0)
                        {
                            dbprintlf(RED_FG "%sSend failed (from %d to %d).", t_tag, (int)network_frame->getOrigin(), (int)network_frame->getDestination());
                        }
                    }
                    else
                    {
                        dbprintlf(RED_FG "%sCannot pass frame from ID:%d to ID:%d since the connection is not ready.", t_tag, (int)network_frame->getOrigin(), (int)network_frame->getDestination());
                    }

                    break;
                }
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

    if (!global->network_data[t_index]->recv_active)
    {
        dbprintlf(YELLOW_FG "%sReceive deactivated.", t_tag);
    }

    return NULL;
}