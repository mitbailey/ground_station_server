/**
 * @file main.cpp
 * @author Mit Bailey (mitbailey99@gmail.com)
 * @brief 
 * @version 0.1
 * @date 2021.07.26
 * 
 * This program will run on the Ground Station Server, a central throughway-hub for handling communications between the 
 * three radios and the GUI Client. It will include RX threads, a transmission method, parsing, and error handling.
 * Communication is done via a ClientServerFrame.
 * 
 * @copyright Copyright (c) 2021
 * 
 */

// TODO: Set Netstat
// TODO: Send (or receive?) periodic null packets.
// NOTE: Leave packets destined for the server alone - no configs yet.

#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <signal.h>
#include "gss.hpp"
#include "gss_debug.hpp"

int main(int argc, char *argv[])
{
    signal(SIGPIPE, SIG_IGN); // so that client does not die when server does

    // Get network data and IP addresses.
    rx_thread_data_t rx_thread_data[1];

    for (int i = 0; i < 4; i++)
    {
        rx_thread_data->network_data[i] = new NetworkData();
    }

    // Get local IPv4 and set each rx_thread_data->network_data[i]->ipv4
    char ipv4[32];
    if (!find_ipv4(ipv4, sizeof(ipv4)))
    {
        dbprintlf(YELLOW_FG "Failed to auto-detect local IPv4! Using default (%s).", LISTENING_IP_ADDRESS);
        strcpy(ipv4, LISTENING_IP_ADDRESS);
    }
    else
    {
        dbprintlf(BLUE_FG "Auto-detected local IPv4: %s", ipv4);
    }

    // Set each thread's network_data to have the correct ipv4 and port.
    for (int i = 0; i < 4; i++)
    {
        memcpy(rx_thread_data->network_data[i]->listening_ipv4, ipv4, sizeof(ipv4));

        rx_thread_data->network_data[i]->listening_port = LISTENING_PORT_BASE + (10 * i);
    }

    // Activate each thread's receive ability.
    for (int i = 0; i < 4; i++)
    {
        rx_thread_data->network_data[i]->rx_active = true;
    }

    // Begin receiver threads.
    // 0:Client, 1:RoofUHF, 2: RoofXB, 3: Haystack
    for (int i = 0; i < 4; i++)
    {
        if (pthread_create(&rx_thread_data->pid[i], NULL, gss_rx_thread, rx_thread_data) != 0)
        {
            dbprintlf(FATAL "Thread %d failed to start.", i);
            return -1;
        }
        else
        {
            dbprintlf(GREEN_FG "Thread %d started.", i);
        }
    }

    for (int i = 0; i < 4; i++)
    {
        void *status;
        if (pthread_join(rx_thread_data->pid[i], &status) != 0)
        {
            dbprintlf(RED_FG "Thread %d failed to join.", i);
        }
        else
        {
            dbprintlf(GREEN_FG "Thread %d joined.", i);
        }
    }

    // On receive:
    // (If endpoint != Here)
    // - Forward to endpoint.
    // (If endpoint == Here)
    // - Accept, perform relevant actions, and respond.

    // Finished.
    for (int i = 0; i < 4; i++)
    {
        delete rx_thread_data->network_data[i];
    }

    return 1;
}