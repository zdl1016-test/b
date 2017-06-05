#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
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

// todo
// release_conn in anywhere is dangerange
//  if fd error, 
//  if logic error

// todo
// connect direct return is 0, mean succ directly
// connect timeout
// connect fail release conn
// connect 
 
#define RINGBUFF_SIZE 1024
#define BUFFER_SIZE   1024

enum e_server_type {
    server_type_empty       = 0,
    server_type_echo        = 1,
    server_type_telnet      = 2,
};

enum e_proxy_type {
    proxy_type_empty       = 0,
    proxy_type_echo        = 1,
    proxy_type_telnet      = 2,
};

struct socket_conn_t;
typedef struct server_context_t {
    int         listen_fd;
    int         port;
    ev_io       sock_w;
    char        server_type;
    void        (*process_data_cb)(struct socket_conn_t *conn);
    void        (*send_data_cb)(struct socket_conn_t *conn);

} server_context_t;

typedef struct proxy_context_t {
    ev_io       read_w; // use for stdio
    char        is_connected;
    struct socket_conn_t *conn;
    char        proxy_type;
    void        (*process_data_cb)(struct socket_conn_t *conn);
    void        (*send_data_cb)(struct socket_conn_t *conn);

} proxy_context_t;

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
    char             conn_type;
    server_context_t*ctx;
    proxy_context_t*proxy_ctx;
} socket_conn_t;

server_context_t * echo_server_ctx = 0;
server_context_t * telnet_server_ctx = 0;
proxy_context_t  * echo_proxy_ctx = 0;
int conn_count = 0;

 
int socket_create_and_bind(const char *port);
int socket_setopt(int sockfd);
void socket_accept(struct ev_loop *main_loop, ev_io *sock_w, int events);
void socket_read(struct ev_loop *main_loop, struct ev_io *client_w, int events);
void socket_write(struct ev_loop *main_loop, struct ev_io *client_w, int events);
int socket_setnonblock(int fd);
int release_conn(socket_conn_t *conn);
int socket_create(const char * ip, const char *port);
void set_address(const char* ip, int port,struct sockaddr_in* addr);

int create_echo_server(struct ev_loop *main_loop, const char *ip, const char *port);
void process_echo_data(struct socket_conn_t *conn);
void send_echo_data(socket_conn_t *conn);
void send_data(socket_conn_t *conn);

int create_telnet_server(struct ev_loop *main_loop, const char *ip, const char *port);
void process_telnet_data(struct socket_conn_t *conn);
void process_telnet_cmd(struct socket_conn_t *conn, char * cmd);

int create_echo_proxy(struct ev_loop *main_loop, const char *ip, const char *port);
void connect_cb(struct ev_loop *main_loop, struct ev_io *sock_w, int events);
void stdin_cb (struct ev_loop *main_loop,  ev_io *w, int revents);
void proxy_process_echo_data(struct socket_conn_t *conn);

int main(int argc, char *argv[])
{
    int s = 0;
 
    if (argc != 4) {
        fprintf(stderr, "Usage: %s [port] [port2] [port3]\n", argv[0]);
        return -1;
    }
 
    struct ev_loop *main_loop = ev_default_loop(0);

    s = create_echo_server(main_loop, 0, argv[1]);
    if (s != 0) {
        fprintf(stderr, "create_echo_server failed\n");
        return -1;
    }

    s = create_telnet_server(main_loop, 0, argv[2]);
    if (s != 0) {
        fprintf(stderr, "create_telnet_server failed\n");
        return -1;
    }
    
    s = create_echo_proxy(main_loop, "127.0.0.1", argv[3]);
    if (s != 0) {
        fprintf(stderr, "create_echo_proxy failed\n");
        return -1;
    }
 
    ev_run(main_loop, 0);
 
    return 0;
}

void process_echo_data(struct socket_conn_t *conn)
{
    // read from recv buff, and write to send buff
    if (ringbuf_bytes_used(conn->rb_recv) <= 0) {
        return;
    }

    if (ringbuf_bytes_used(conn->rb_recv) > ringbuf_bytes_free(conn->rb_send)) {
        fprintf(stderr, "%s recv data is over send buff, force disconnect client:%s:%d, recv data:%d, send buff:%d\n", __func__,
                conn->remote_ip, conn->remote_port, 
                ringbuf_bytes_used(conn->rb_recv), ringbuf_bytes_free(conn->rb_send));
        release_conn(conn);
        return;
    }

    while (1) {
        ringbuf_copy(conn->rb_send, conn->rb_recv, ringbuf_bytes_used(conn->rb_recv));
        if (ringbuf_bytes_used(conn->rb_recv) <= 0) {
            break;
        }
    }

    ev_io_start(conn->loop, &conn->write_w);
}

void process_telnet_data(struct socket_conn_t *conn)
{
    // telnet cmd segment by line break '\n' 
    
    if (ringbuf_bytes_used(conn->rb_recv) <= 0) {
        return;
    }

    char cmd[1024];

    // parse recv buff, try find char '\n'
    size_t offset = 0;
    while (1) {
        size_t pos = ringbuf_findchr_human(conn->rb_recv, '\n', offset);
        if (pos == -1) {
            break;
        }

        fprintf(stderr, "DEBUG: line break pos:%u\n", pos);

        // todo , when recv invalid cmd, here process is simple and crude, the better way is ingore the invalid cmd
        if (pos > (sizeof(cmd)-1)) {
            fprintf(stderr, "invalid telnet cmd, the cmd is too long, len:%u, force disconnect client:%s:%d", pos, conn->remote_ip, conn->remote_port);
            release_conn(conn);
            return;
        }


        // find one cmd, and process the cmd
        ringbuf_memcpy_from(cmd, conn->rb_recv, pos+1);
        cmd[pos] = '\0'; // replace \n to \0

        // remove \r if exist
        if (pos >= 1 && (cmd[pos-1]=='\r')) {
            cmd[pos-1] = '\0';
        }

        process_telnet_cmd(conn, cmd);
        offset = pos + 1;
    }
}

void process_telnet_cmd(struct socket_conn_t *conn, char * cmd) {
    fprintf(stderr, "recv telnet cmd:%s\n", cmd);

    if (strcasecmp(cmd, "quit_server") == 0) {
        abort();
        return;
    } else if (strcasecmp(cmd, "status") == 0) {
        char buff[128];
        snprintf(buff, sizeof(buff), "now conn count:%d\n", conn_count);
        ringbuf_memcpy_into(conn->rb_send, buff, strlen(buff));
    } else {
        char buff[128];
        snprintf(buff, sizeof(buff), "unsupoort cmd:%s\n", cmd);
        ringbuf_memcpy_into(conn->rb_send, buff, strlen(buff));
    }
    ev_io_start(conn->loop, &conn->write_w);
}

void proxy_process_echo_data(struct socket_conn_t *conn)
{
    // read from recv buff, and write to send buff
    if (ringbuf_bytes_used(conn->rb_recv) <= 0) {
        return;
    }

    fprintf(stderr, "proxy conn, recv server data:");
    ringbuf_write(STDERR_FILENO, conn->rb_recv, ringbuf_bytes_used(conn->rb_recv));
}

// common send buff func
void send_data(socket_conn_t *conn) {
    // if send buff is empty, direct return
    if (ringbuf_bytes_used(conn->rb_send) <= 0) {
        ev_io_stop(conn->loop, &conn->write_w);
        return;
    }

    while (1) {
        int n = ringbuf_write(conn->fd, conn->rb_send, ringbuf_bytes_used(conn->rb_send));
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

    // todo, is this right ? old code is direct io_stop without judge remain buff
    if (ringbuf_bytes_used(conn->rb_send) <= 0) {
        ev_io_stop(conn->loop, &conn->write_w);
        return;
    }
}

void send_echo_data(socket_conn_t *conn)
{
    send_data(conn);
}


int create_echo_server(struct ev_loop *main_loop, const char *ip, const char *port)
{
    int sfd = 0, s = 0;
 
    sfd = socket_create_and_bind(port);
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

    server_context_t *ctx = malloc(sizeof(struct server_context_t));
    memset(ctx, 0, sizeof(struct server_context_t));

    ctx->server_type = server_type_echo; // 
    ctx->process_data_cb = process_echo_data;
    ctx->send_data_cb = send_echo_data;
    ctx->sock_w.data = ctx;
    ev_init(&(ctx->sock_w), socket_accept);
    ev_io_set(&(ctx->sock_w), sfd, EV_READ);
    ev_io_start(main_loop, &(ctx->sock_w));

    echo_server_ctx = ctx;

    fprintf(stderr, "listen %s succ. echo server \n", port);
    return 0;
}

int create_telnet_server(struct ev_loop *main_loop, const char *ip, const char *port)
{
    int sfd = 0, s = 0;
 
    sfd = socket_create_and_bind(port);
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

    server_context_t *ctx = malloc(sizeof(struct server_context_t));
    memset(ctx, 0, sizeof(struct server_context_t));

    ctx->server_type = server_type_telnet; // 
    ctx->process_data_cb = process_telnet_data;
    ctx->send_data_cb = send_data;
    ctx->sock_w.data = ctx;
    ev_init(&(ctx->sock_w), socket_accept);
    ev_io_set(&(ctx->sock_w), sfd, EV_READ);
    ev_io_start(main_loop, &(ctx->sock_w));

    telnet_server_ctx = ctx;

    fprintf(stderr, "listen %s succ. telnet server \n", port);

    return 0;
}

void stdin_cb (struct ev_loop *main_loop,  ev_io *w, int revents)
{
    char buf[1024];
    if (fgets(buf, sizeof(buf), stdin))
    {
        fprintf(stderr, "you input : %s\n", buf);
        if (strncmp("quit\n", buf, 4) == 0)
        {
            ev_io_stop(main_loop, w);
            ev_break(main_loop, EVBREAK_ALL);
        }

        int buf_len = strlen(buf);
        proxy_context_t *ctx = w->data;
        if (ctx) {
            socket_conn_t * conn = ctx->conn;
            if (ctx->is_connected) {
                int free_size = ringbuf_bytes_free(conn->rb_send);
                if (free_size < buf_len) {
                    fprintf(stderr, "%s force ignore stdio cmd, because rb_send ringbuff is full\n", __func__);
                    return;
                }
                ringbuf_memcpy_into(conn->rb_send, buf, buf_len);
                ev_io_start(conn->loop, &conn->write_w);
            } else  {
                fprintf(stderr, "%s ignore stdio cmd, because proxy connect is not connected\n", __func__);
            }
        } else {
            fprintf(stderr, "%s ignore stdio cmd, because proxy_ctx is miss\n", __func__);
        }
    }
}

int create_echo_proxy(struct ev_loop *main_loop, const char *ip, const char *port)
{
    int sfd = 0, s = 0;
    struct hostent *he; 

    if (ip == NULL || port == NULL) {
        fprintf(stderr, "empty ip or port \n");
        abort();
        return -1;
    }
 
    //sfd = socket_create(ip, port);
    //if (sfd == -1) {
    //    abort();
    //    return -1;
    //}
    //if ((he = gethostbyname(ip)) == NULL) {
    //    fprintf(stderr, "gethostbyname failed \n");
    //    abort();
    //    return -1;
    //}


    //AF_INET : IPv4
    //SOCK_STREAM : TCP
    sfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sfd == -1) {
        fprintf(stderr, "socket failed\n");
        abort();
        return -1;
    }
 
    s = socket_setnonblock(sfd);
    if (s == -1) {
        abort();
        return -1;
    }


    struct sockaddr_in addr; 
    //addr.sin_family = AF_INET;
    //addr.sin_port = htons(atoi(port)); 
    //addr.sin_addr = *((struct in_addr *)he->h_addr);
    set_address(ip, atoi(port), &addr);

    s = connect(sfd, (struct sockaddr *)&addr, sizeof(addr));
    if (s == -1) {
        if (errno != EINPROGRESS) {
            fprintf(stderr, "connect failed %d, errno:%d %s\n", s, errno, strerror(errno));
            abort();
            return -1;  
        }
    }  

    proxy_context_t *ctx = malloc(sizeof(struct proxy_context_t));
    memset(ctx, 0, sizeof(struct proxy_context_t));

    ctx->proxy_type = proxy_type_echo; // 
    ctx->process_data_cb = proxy_process_echo_data;
    ctx->send_data_cb = send_data;

    ctx->read_w.data = ctx;
    ev_io_init (&ctx->read_w, stdin_cb, STDIN_FILENO, EV_READ);
    ev_io_start (main_loop, &ctx->read_w);

    // connect
    struct socket_conn_t *conn = (struct socket_conn_t *)malloc(sizeof(struct socket_conn_t));
    memset(conn, 0, sizeof(struct socket_conn_t));
    strncpy(conn->remote_ip, inet_ntoa(addr.sin_addr), sizeof(conn->remote_ip));
    conn->remote_ip[sizeof(conn->remote_ip)-1] = '\0';
    conn->remote_port = (int)ntohs(addr.sin_port);

    conn->loop = main_loop;
    conn->read_w.data = conn;
    conn->write_w.data = conn;
    conn->rb_recv = ringbuf_new(RINGBUFF_SIZE);
    conn->rb_send = ringbuf_new(RINGBUFF_SIZE);
    conn->fd = sfd;
    ev_io_init(&conn->read_w, socket_read, sfd, EV_READ);
    ev_io_init(&conn->write_w, connect_cb, sfd, EV_WRITE);
    ev_io_start(main_loop, &conn->write_w);
    ev_io_start(main_loop, &conn->read_w);
    conn->proxy_ctx = ctx;

    ctx->conn = conn;

    echo_proxy_ctx = ctx;

    fprintf(stderr, "try connect %s . proxy\n", port);

    return 0;
}

void set_address(const char* ip, int port,struct sockaddr_in* addr){  
    bzero(addr,sizeof(*addr));  
    addr->sin_family=AF_INET;  
    inet_pton(AF_INET,ip,&(addr->sin_addr));  
    addr->sin_port=htons(port);  
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

int socket_create(const char * ip, const char *port)
{
    int s, sfd;
    struct addrinfo hints;
    struct addrinfo *result, *rp;
 
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
 
    s = getaddrinfo(ip, port, &hints, &result);
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
 
        // break 
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
        //fprintf(stderr, "%s new conn %d [%s:%d]\n", __func__, nfd, inet_ntoa(sin.sin_addr), ntohs(sin.sin_port));
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
        conn->ctx = sock_w->data;

        fprintf(stderr, "%s new conn %d [%s:%d], server_type:%d\n", __func__, nfd, inet_ntoa(sin.sin_addr), ntohs(sin.sin_port), (int)conn->ctx->server_type);
        fflush(stderr);
        conn_count++;
    } while (0);
}

void connect_cb(struct ev_loop *main_loop, struct ev_io *sock_w, int events)
{
    struct socket_conn_t *conn = NULL;
 
    if (EV_ERROR & events) {
        fprintf(stderr, "%s error event\n", __func__);
        return;
    }

    conn = (struct socket_conn_t *)sock_w->data;
    ev_io_stop(conn->loop, &conn->write_w);

    int err;
    socklen_t len = sizeof(err);
    int s = getsockopt(sock_w->fd, SOL_SOCKET, SO_ERROR, &err, &len);
    if (s == -1) {
        sprintf("getsockopt(SO_ERROR): %s", strerror(errno));
        release_conn(conn);
        return;
    }

    if (err) {
        // error happen
        fprintf(stderr, "connect_cb error, err:%d, %s \n", err, strerror(err));
        release_conn(conn);
        return;
    }

    proxy_context_t* ctx = conn->proxy_ctx;
    if (ctx) {
        ctx->is_connected = 1;
        fprintf(stderr, "conn succ %s [%s:%d], proxy_type:%d\n", __func__, conn->remote_ip, conn->remote_port, (int)conn->proxy_ctx->proxy_type);
        fflush(stderr);
    } else {
        fprintf(stderr, "!!! conn succ %s [%s:%d], but not find proxy_ctx\n", __func__, conn->remote_ip, conn->remote_port);
        fflush(stderr);
    }


    // modify write_handle from connect_cb to socket_write
    ev_io_init(&conn->write_w, socket_write, conn->fd, EV_WRITE);
}

void socket_read(struct ev_loop *main_loop, struct ev_io *client_w, int events)
{
        fprintf(stderr, " socket_read aaaaaaaaaa \n");
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
        int free_size = ringbuf_bytes_free(conn->rb_recv);
        if (free_size < read) {
            fprintf(stderr, "%s force disconnect client, because ringbuff is full\n", __func__);
            release_conn(conn);
            return;
        }
        ringbuf_memcpy_into(conn->rb_recv, buffer, read);
    }

    if (conn->ctx) {
        conn->ctx->process_data_cb(conn);
    } else if(conn->proxy_ctx) {
        conn->proxy_ctx->process_data_cb(conn);
    }
}

void socket_write(struct ev_loop *main_loop, struct ev_io *client_w, int events)
{
    struct socket_conn_t *conn = NULL;
 
    if (EV_ERROR & events) {
        fprintf(stderr, "%s error event\n", __func__);
        return;
    }
 
    conn = (struct socket_conn_t *)client_w->data;
    if (conn->ctx) {
        conn->ctx->send_data_cb(conn);
    } else if(conn->proxy_ctx) {
        conn->proxy_ctx->send_data_cb(conn);
    }
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
    conn_count--;
    return;
}
