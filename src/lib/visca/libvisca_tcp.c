/*
 * VISCA(tm) Camera Control Library - TCP/IP Extension
 * Copyright (C) 2025 Energy Robotics
 *
 * Based on libvisca by Damien Douxchamps
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <libvisca2/libvisca.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/ioctl.h>
#include <stdio.h>

/**
 * Opens a TCP connection to a VISCA camera/interface.
 * 
 * This function creates a TCP socket and connects to the specified
 * hostname and port. The resulting socket file descriptor is stored
 * in iface->port_fd, allowing reuse of the existing VISCA read/write
 * functions that operate on file descriptors.
 *
 * @param iface     Pointer to the VISCA interface structure
 * @param hostname  Hostname or IP address of the VISCA device
 * @param port      TCP port number (typically 1000 for FV4K or 52381 for VISCA-over-IP)
 * @return          VISCA_SUCCESS on success, VISCA_FAILURE on error
 */
uint32_t
VISCA_open_tcp(VISCAInterface_t *iface, const char *hostname, uint32_t port)
{
    int sockfd;
    struct sockaddr_in server_addr;
    struct hostent *server;
    int flags;
    int optval;

    /* Create TCP socket */
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0)
    {
        fprintf(stderr, "(%s): cannot create socket\n", __FILE__);
        iface->port_fd = -1;
        return VISCA_FAILURE;
    }

    /* Resolve hostname */
    server = gethostbyname(hostname);
    if (server == NULL)
    {
        fprintf(stderr, "(%s): cannot resolve hostname %s\n", __FILE__, hostname);
        close(sockfd);
        iface->port_fd = -1;
        return VISCA_FAILURE;
    }

    /* Set up server address structure */
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    memcpy(&server_addr.sin_addr.s_addr, server->h_addr, server->h_length);
    server_addr.sin_port = htons(port);

    /* Connect to server */
    if (connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        fprintf(stderr, "(%s): cannot connect to %s:%d - %s\n", 
                __FILE__, hostname, port, strerror(errno));
        close(sockfd);
        iface->port_fd = -1;
        return VISCA_FAILURE;
    }

    /* Set TCP_NODELAY to disable Nagle's algorithm for low latency */
    optval = 1;
    if (setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, &optval, sizeof(optval)) < 0)
    {
        fprintf(stderr, "(%s): warning - cannot set TCP_NODELAY\n", __FILE__);
        /* Non-fatal, continue anyway */
    }

    /* Set socket to non-blocking mode initially, then switch to blocking for I/O */
    /* Actually, keep it blocking for simpler read/write operations */
    flags = fcntl(sockfd, F_GETFL, 0);
    if (flags >= 0)
    {
        /* Ensure blocking mode */
        fcntl(sockfd, F_SETFL, flags & ~O_NONBLOCK);
    }

    /* Set socket receive timeout (5 seconds) */
    struct timeval tv;
    tv.tv_sec = 5;
    tv.tv_usec = 0;
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    /* Store socket fd and initialize interface */
    iface->port_fd = sockfd;
    _VISCA_init_interface(iface);

    fprintf(stderr, "(%s): connected to %s:%d (fd=%d)\n", 
            __FILE__, hostname, port, sockfd);

    return VISCA_SUCCESS;
}

/**
 * Closes a TCP connection to a VISCA camera/interface.
 *
 * @param iface     Pointer to the VISCA interface structure
 * @return          VISCA_SUCCESS on success, VISCA_FAILURE on error
 */
uint32_t
VISCA_close_tcp(VISCAInterface_t *iface)
{
    if (iface->port_fd != -1)
    {
        /* Graceful shutdown */
        shutdown(iface->port_fd, SHUT_RDWR);
        close(iface->port_fd);
        iface->port_fd = -1;

        fprintf(stderr, "(%s): TCP connection closed\n", __FILE__);
        return VISCA_SUCCESS;
    }
    else
    {
        return VISCA_FAILURE;
    }
}

/**
 * Checks if there are bytes available to read on the TCP socket.
 * This is useful for non-blocking checks before attempting to read.
 *
 * @param iface     Pointer to the VISCA interface structure
 * @param available Pointer to store the number of available bytes
 * @return          VISCA_SUCCESS on success, VISCA_FAILURE on error
 */
uint32_t
VISCA_tcp_bytes_available(VISCAInterface_t *iface, uint32_t *available)
{
    int bytes = 0;
    
    if (iface->port_fd == -1)
    {
        *available = 0;
        return VISCA_FAILURE;
    }
    
    if (ioctl(iface->port_fd, FIONREAD, &bytes) < 0)
    {
        *available = 0;
        return VISCA_FAILURE;
    }
    
    *available = (uint32_t)bytes;
    return VISCA_SUCCESS;
}
