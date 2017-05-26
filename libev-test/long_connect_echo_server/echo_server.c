#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/unistd.h>
#include <sys/epoll.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h> 
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <netdb.h>
#include "ev.h"
#include "ringbuf.h"
 
#define RINGBUFF_SIZE 1024
#define BUFFER_SIZE   1024

typedef struct socket_conn_t {
    struct ev_loop  *loop;
    ev_io            read_w;
    ev_io            write_w;
    ev_io            time_w;
    ringbuf_t        rb_send;
    ringbuf_t        rb_recv;
    int              fd;
    char             remote_ip[32];
    int              remote_port;
}socket_conn_t;

 
int socket_create_and_bind(const char *port);
int socket_setopt(int sockfd);
void socket_accept(struct ev_loop *main_loop, ev_io *sock_w, int events);
void socket_read(struct ev_loop *main_loop, struct ev_io *client_w, int events);
void socket_write(struct ev_loop *main_loop, struct ev_io *client_w, int events);
int socket_setnonblock(int fd);
int release_conn(socket_conn_t *conn);

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

    int no_delay = 1;
    setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, (const char*)&no_delay, sizeof(no_delay));
 
    return 0;
}

void socket_accept(struct ev_loop *main_loop, ev_io *sock_w, int events)
{
    struct sockaddr_in sin;
    socklen_t len = sizeof(struct sockaddr_in);
 
    int nfd = 0;
    if ((nfd = accept(sock_w->fd, (struct sockaddr *)&sin, &len)) == -1) {
        if (errno != EAGAIN && errno != EINTR) {
            fprintf(stderr, "%s bad accept, %s\n", __func__, strerror(errno));
        }
        return;
    }
 
    do {
        fprintf(stderr, "%s new conn %d [%s:%d]\n", __func__, nfd, inet_ntoa(sin.sin_addr), ntohs(sin.sin_port));
 
        socket_setnonblock(nfd);
 
        // new client
        struct socket_conn_t *conn = (struct socket_conn_t *)malloc(sizeof(struct socket_conn_t));
        memset(conn, 0, sizeof(struct socket_conn_t));
 
        strncpy(conn->remote_ip, inet_ntoa(sin.sin_addr), sizeof(conn->remote_ip));
        conn->remote_ip[sizeof(conn->remote_ip)-1] = '\0';
        conn->remote_port = (int)ntohs(sin.sin_port);

        conn->loop = main_loop;
        conn->read_w.data = conn;
        conn->write_w.data = conn;
        conn->rb_recv = ringbuf_new(RINGBUFF_SIZE);
        conn->rb_send = ringbuf_new(RINGBUFF_SIZE);
        conn->fd = nfd;
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
 
    while (1) {
        char buffer[BUFFER_SIZE] = {0};
        ssize_t read = recv(client_w->fd, buffer, BUFFER_SIZE, 0);
        if (read < 0) {
            if(errno != EINTR && errno != EWOULDBLOCK && errno != EAGAIN) {
                fprintf(stderr, "%s read error\n", __func__);
                release_conn(conn);
                return;
            }
            break;
        }

        if (read == 0) {
            fprintf(stderr, "%s client disconnected\n", __func__);
            release_conn(conn);
            return;
        }

        //fprintf(stderr, "%s receive message: [%s]\n", __func__, buffer);

        // todo, need support AUTO-INCR ringbuf
        int free_size = ringbuf_bytes_free(conn->rb_send);
        if (free_size < read)
        {
            fprintf(stderr, "%s force disconnect client, because ringbuff is full\n", __func__);
            release_conn(conn);
            return;
        }
        ringbuf_memcpy_into(conn->rb_send, buffer, read);
    }
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
 
    if (ringbuf_bytes_used(conn->rb_send) == 0) {
        ev_io_stop(conn->loop, &conn->write_w);
        return;
    }

    while (1) {
        int n = ringbuf_write(client_w->fd, conn->rb_send, ringbuf_bytes_used(conn->rb_send));
        if (n <= 0) {
            if(errno==EINTR||errno== EWOULDBLOCK||errno == EAGAIN) {
                break;
            } else {
                fprintf(stderr, "epoll send date faild(%d)(%s) ip = (%s) \n", errno, strerror(errno), conn->remote_ip);
                release_conn(conn);
                return;
            }
        }

        if (ringbuf_bytes_used(conn->rb_send) <= 0) {
            break;
        }
    }
 
    ev_io_stop(conn->loop, &conn->write_w);
    return;
}

int release_conn(socket_conn_t *conn)
{
    close(conn->fd);
    conn->fd=-1;
    ev_io_stop(conn->loop, &conn->read_w);
    ev_io_stop(conn->loop, &conn->write_w);
    ringbuf_free(&conn->rb_send);
    ringbuf_free(&conn->rb_recv);
    free(conn);
    return;
}
