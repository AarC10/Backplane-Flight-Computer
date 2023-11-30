/*
 * Copyright (c) 2023 Aaron Chan
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "net_utils.h"
#include "lora_utils.h"
#include <zephyr/kernel.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/ethernet.h>
#include <zephyr/net/net_event.h>

#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(net_utils, CONFIG_APP_LOG_LEVEL);

static struct net_if *net_interface;
extern const struct device *const lora_dev;
#define MAX_UDP_PACKET_SIZE 255
int init_eth_iface(void) {
    const struct device *const wiznet = DEVICE_DT_GET_ONE(wiznet_w5500);
    if (!device_is_ready(wiznet)) {
        LOG_INF("Device %s is not ready.\n", wiznet->name);
        return -ENODEV;
    } 
    
    LOG_INF("Device %s is ready.\n", wiznet->name);
    return 0;
}

int init_net_stack(void) {
    static const char ip_addr[] = "10.10.10.12";
    int ret;

    net_interface = net_if_get_default();
    if (!net_interface) {
        LOG_INF("No network interface found\n");
        return -ENODEV;
    }

    struct in_addr addr;
    ret = net_addr_pton(AF_INET, ip_addr, &addr);
    if (ret < 0) {
        LOG_INF("Invalid IP address\n");
        return ret;
    }

    struct net_if_addr *ifaddr = net_if_ipv4_addr_add(net_interface, &addr, NET_ADDR_MANUAL, 0);
    if (!ifaddr) {
        LOG_INF("Failed to add IP address\n");
        return -ENODEV;
    }

    LOG_INF("IPv4 address configured: %s\n", ip_addr);

    return 0;
}

int send_udp_broadcast(const uint8_t *data, size_t data_len, uint16_t port) {
    int sock;
    int ret;

    sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        LOG_INF("Failed to create socket (%d)\n", sock);
        return sock;
    }

    struct sockaddr_in dst_addr;
    dst_addr.sin_family = AF_INET;
    dst_addr.sin_port = htons(port);
    ret = net_addr_pton(AF_INET, "255.255.255.255", &dst_addr.sin_addr);
    if (ret < 0) {
        LOG_INF("Invalid IP address format\n");
        close(sock);
        return ret;
    }

    ret = sendto(sock, data, data_len, 0, (struct sockaddr *) &dst_addr, sizeof(dst_addr));
    if (ret < 0) {
        LOG_INF("Failed to send UDP broadcast (%d)\n", ret);
        close(sock);
        return ret;
    }

    LOG_INF("Sent UDP broadcast: %s\n", data);

    close(sock);
    return 0;
}

void receive_udp_task(void *port_arg, void *, void *) {
    int sock;
    int ret;
    int port = POINTER_TO_INT(port_arg);
    printk("Listening on port %d\n", port);
    sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        LOG_INF("Failed to create socket (%d)\n", sock);
        return;
    }

    struct sockaddr_in src_addr;
    memset(&src_addr, 0, sizeof(src_addr));
    src_addr.sin_family = AF_INET;
    src_addr.sin_port = htons(port);
    src_addr.sin_addr.s_addr = INADDR_ANY;

    ret = bind(sock, (struct sockaddr *) &src_addr, sizeof(src_addr));
    if (ret < 0) {
        printk("Failed to bind socket (%d)\n", ret);
        close(sock);
        return;
    }

    uint8_t buffer[MAX_UDP_PACKET_SIZE];
    struct sockaddr_in from_addr;
    socklen_t from_addr_len = sizeof(from_addr);
    printk("Entering loop");
    while (1) {
        memset(buffer, 0, MAX_UDP_PACKET_SIZE);
        ret = recvfrom(sock, buffer, MAX_UDP_PACKET_SIZE, 0, (struct sockaddr *) &from_addr, &from_addr_len);
        if (ret < 0) {
            printk("%d: Failed to receive UDP packet (%d)\n", port, ret);
            continue;
        }

        char from_ip[NET_IPV4_ADDR_LEN];
        net_addr_ntop(AF_INET, &from_addr.sin_addr, from_ip, sizeof(from_ip));
        printk("Received UDP packet from %s:%d\n", from_ip, ntohs(from_addr.sin_port));

        LORA_PACKET_T lora_packet = {0};
        lora_packet.port = port;
        memcpy(lora_packet.packet, buffer, ret);
        
        lora_tx(lora_dev, (uint8_t *) &lora_packet, 2 + ret);
        
    }

    close(sock);
}
