#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/unistd.h>
#include <sys/epoll.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <netdb.h>
#include "ev.h"
 
#define BUFFER_SIZE   1024

typedef struct socket_conn_t {
    struct ev_loop  *loop;
    ev_io            read_w;
    ev_io            write_w;
    ev_io            time_w;
    char            *buffer;
}socket_conn_t;

 
int socket_create_and_bind(const char *port);
int socket_setopt(int sockfd);
void socket_accept(struct ev_loop *main_loop, ev_io *sock_w, int events);
void socket_read(struct ev_loop *main_loop, struct ev_io *client_w, int events);
void socket_write(struct ev_loop *main_loop, struct ev_io *client_w, int events);
int socket_setnonblock(int fd);

int main(int argc, char *argv[])
{
    int sfd = 0, s = 0;
 
    if (argc != 2) {
        fprintf(stderr, "Usage: %s [port]\n", argv[0]);
        return -1;
    }
 
    sfd = socket_create_and_bind(argv[1]);
    if (sfd == -1) {
        abort();
        return -1;
    }
 
    s = socket_setnonblock(sfd);
    if (s == -1) {
        abort();
        return -1;
    }
 
    s = listen(sfd, SOMAXCONN);
    if (s == -1) {
        fprintf(stderr, "listen error\n");
        return -1;
    }
 
    struct ev_loop *main_loop = ev_default_loop(0);
 
    ev_io sock_w;
    ev_init(&sock_w, socket_accept);
    ev_io_set(&sock_w, sfd, EV_READ);
    ev_io_start(main_loop, &sock_w);
 
    ev_run(main_loop, 0);
 
    return 0;
}

int socket_setnonblock(int fd)
{
    long flags = fcntl(fd, F_GETFL);
    if (flags < 0) {
        fprintf(stderr, "fcntl F_GETFL");
        return -1;
    }

    flags |= O_NONBLOCK;
    if (fcntl(fd, F_SETFL, flags) < 0) {
        fprintf(stderr, "fcntl F_SETFL");
        return -1;
    }

    return 0;
}

int socket_create_and_bind(const char *port)
{
    int s, sfd;
    struct addrinfo hints;
    struct addrinfo *result, *rp;
 
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags    = AI_PASSIVE;
 
    s = getaddrinfo(NULL, port, &hints, &result);
    if (s != 0) {
        fprintf(stderr,"%s getaddrinfo: %s\n", __func__, gai_strerror(s));
        return -1;
    }
 
    for (rp = result; rp!= NULL; rp = rp->ai_next) {
        sfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (sfd == -1) {
            continue;
        }
 
        s = socket_setopt(sfd);
        if (s == -1) {
            return -1;
        }
 
        s = bind(sfd, rp->ai_addr, rp->ai_addrlen);
        if (s == 0) {
            break;
        }
        close(sfd);
    }
 
    if (rp == NULL) {
        fprintf(stderr, "%s Could not bind\n", __func__);
        return -1;
    }
 
    freeaddrinfo(result);
    return sfd;
}

int socket_setopt(int sockfd)
{
    int ret = 0;
 
    int reuse = 1;
    ret = setsockopt(sockfd, SOL_SOCKET, SO_REUSEPORT, (void *)&reuse, sizeof(reuse));
    if (ret < 0) {
        fprintf(stderr, "%s setsockopt SO_REUSEPORT error\n", __func__);
        return -1;
    }
 
    struct linger so_linger;
    so_linger.l_onoff = 1;
    so_linger.l_linger = 1;
    ret = setsockopt(sockfd, SOL_SOCKET, SO_LINGER, &so_linger, sizeof(so_linger));
    if (ret < 0) {
        fprintf(stderr, "%s setsockopt SO_LINGER error\n", __func__);
        return -1;
    }
 
    return 0;
}

void socket_accept(struct ev_loop *main_loop, ev_io *sock_w, int events)
{
    struct sockaddr_in sin;
    socklen_t len = sizeof(struct sockaddr_in);
 
    int nfd = 0;
    if ((nfd = accept(sock_w->fd, (struct sockaddr *)&sin, &len)) == -1) {
        if (errno != EAGAIN && errno != EINTR) {
            fprintf(stderr, "%s bad accept\n", __func__);
        }
        return;
    }
 
    do {
        fprintf(stderr, "%s new conn %d [%s:%d]\n", __func__, nfd, inet_ntoa(sin.sin_addr), ntohs(sin.sin_port));
 
        socket_setnonblock(nfd);
 
        // new client
        struct socket_conn_t *conn = (struct socket_conn_t *)malloc(sizeof(struct socket_conn_t));
        memset(conn, 0, sizeof(struct socket_conn_t));
 
        conn->loop = main_loop;
        conn->read_w.data = conn;
        conn->write_w.data = conn;
        ev_io_init(&conn->read_w, socket_read, nfd, EV_READ);
        ev_io_init(&conn->write_w, socket_write, nfd, EV_WRITE);
        ev_io_start(main_loop, &conn->read_w);
    } while (0);
}

void socket_read(struct ev_loop *main_loop, struct ev_io *client_w, int events)
{
    struct socket_conn_t *conn = NULL;
 
    if (EV_ERROR & events) {
        fprintf(stderr, "%s error event\n", __func__);
        return;
    }
 
    conn = (struct socket_conn_t *)client_w->data;
 
    char buffer[BUFFER_SIZE] = {0};
    ssize_t read = recv(client_w->fd, buffer, BUFFER_SIZE, 0);
    if (read < 0) {
         fprintf(stderr, "%s read error\n", __func__);
         return;
    }
 
    if (read == 0) {
        fprintf(stderr, "%s client disconnected\n", __func__);
        close(client_w->fd);
        ev_io_stop(conn->loop, &conn->read_w);
        ev_io_stop(conn->loop, &conn->write_w);
        free(conn);
        return;
    }
 
    fprintf(stderr, "%s receive message: [%s]\n", __func__, buffer);
 
    if (strncasecmp(buffer, "quit", 4) == 0) {
        fprintf(stderr, "%s client quit\n", __func__);
        close(client_w->fd);
        ev_io_stop(conn->loop, &conn->read_w);
        ev_io_stop(conn->loop, &conn->write_w);
        free(conn);
        return;
    }
 
    int len = strlen(buffer);
    conn->buffer = (char *)malloc((len + 1) * sizeof(char));
    strncpy(conn->buffer, buffer, len);
    conn->buffer[len] = '\0';
 
    memset(buffer, 0, sizeof(buffer));
    ev_io_start(conn->loop, &conn->write_w);
}

void socket_write(struct ev_loop *main_loop, struct ev_io *client_w, int events)
{
    struct socket_conn_t *conn = NULL;
 
    if (EV_ERROR & events) {
        fprintf(stderr, "%s error event\n", __func__);
        return;
    }
 
    conn = (struct socket_conn_t *)client_w->data;
 
    // write/clean data buffer
    if (conn->buffer == NULL) {
        ev_io_stop(conn->loop, &conn->write_w);
        return;
    }
    send(client_w->fd, conn->buffer, strlen(conn->buffer), 0);
    free(conn->buffer);
    conn->buffer = NULL;
 
    ev_io_stop(conn->loop, &conn->write_w);
    return;
}
