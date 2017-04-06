#include <stdio.h>  
#include <netinet/in.h>  
#include <ev.h>  
#include <stdlib.h>
#include <string.h>

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

#define PORT 9999  
#define BUFFER_SIZE 1024  

/*
 * NOTE
 * 该示例代码中有大量不规范的地方没有修正，仅仅能跑起来而已
 */


void accept_cb(struct ev_loop *loop, struct ev_io *watcher, int revents);  


void read_cb(struct ev_loop *loop, struct ev_io *watcher, int revents);  
void write_cb(struct ev_loop *loop, struct ev_io *watcher, int revents);  
int socket_setnonblock(int fd);

typedef struct socket_conn_t {
    struct ev_io read_w;
    struct ev_io write_w;
} socket_conn_t;

int main()  
{  
    struct ev_loop *loop = ev_default_loop(0);  
    int sd;  
    struct sockaddr_in addr;  
    int addr_len = sizeof(addr);  
    struct ev_io socket_accept;  

    // 创建socket的写法,这里简单处理,用INADDR_ANY ,匹配任何客户端请求.这里写法都一样,没什么特别的,直接copy都可以用  
    if( (sd = socket(PF_INET, SOCK_STREAM, 0)) < 0 )  
    {  
        printf("socket error");  
        return -1;  
    }  

    socket_setnonblock(sd);

    bzero(&addr, sizeof(addr));  
    addr.sin_family = AF_INET;  
    addr.sin_port = htons(PORT);  
    addr.sin_addr.s_addr = INADDR_ANY;  
    if (bind(sd, (struct sockaddr*) &addr, sizeof(addr)) != 0)  
    {  
        printf("bind error");  
    }  
    if (listen(sd, 2) < 0)  
    {  
        printf("listen error");  
        return -1;  
    }  



    // 初始化,这里监听了io事件,写法参考官方文档的  
    ev_io_init(&socket_accept, accept_cb, sd, EV_READ);  
    ev_io_start(loop, &socket_accept);  

    while (1)  
    {  
        ev_loop(loop, 0);  
    }  

    return 0;  
}  



//accept事件 的回调块  
void accept_cb(struct ev_loop *loop, struct ev_io *watcher, int revents)  
{  
    struct sockaddr_in client_addr;  
    socklen_t client_len = sizeof(client_addr);  
    int client_sd;  

    //分派客户端的ev io结构  
    /*struct ev_io *w_client = (struct ev_io*) malloc (sizeof(struct ev_io));  */
    /*struct ev_io *w_client_w = (struct ev_io*) malloc (sizeof(struct ev_io));  */

    //libev的错误处理  
    if(EV_ERROR & revents)  
    {  
        printf("error event in accept");  
        return;  
    }  

    //accept,普通写法  
    client_sd = accept(watcher->fd, (struct sockaddr *)&client_addr, &client_len);  
    if (client_sd < 0)  
    {  
        printf("accept error");  
        return;  
    }  
    socket_setnonblock(client_sd);

    printf("someone connected.\n");  

    socket_conn_t * conn = (socket_conn_t*)malloc(sizeof(socket_conn_t));
    memset(conn, 0, sizeof(socket_conn_t));

    conn->read_w.data = conn;
    conn->write_w.data = conn;

    //开始监听读事件了,有客户端信息就会被监听到  
    ev_io_init(&conn->read_w, read_cb, client_sd, EV_READ);  
    ev_io_init(&conn->write_w, write_cb, client_sd, EV_WRITE); 
    ev_io_start(loop, &conn->read_w);  
}  

char buffer[BUFFER_SIZE];  
ssize_t buffer_data_size=0;

//read 数据事件的回调快  
void read_cb(struct ev_loop *loop, struct ev_io *watcher, int revents){  
    ssize_t read;  

    if(EV_ERROR & revents)  
    {  
        printf("error event in read");  
        return;  
    }  

    //recv普通socket写法  
    read = recv(watcher->fd, buffer, BUFFER_SIZE, 0);  

    if(read < 0)  
    {  
        printf("read error");  
        return;  
    }  

    //断开链接的处理,停掉evnet就可以,同时记得释放客户端的结构体!  
    if(read == 0)  
    {  
        printf("someone disconnected.\n");  
        ev_io_stop(loop,watcher);  
        free(watcher);  
        return;  
    }  
    else  
    {  
        printf("get the message:[%s]\n",buffer);  
    }  

    buffer_data_size+=read;
    struct socket_conn_t * conn = watcher->data;
    ev_io_start(loop, &conn->write_w);  
}  

//write 数据事件的回调快  
void write_cb(struct ev_loop *loop, struct ev_io *watcher, int revents){  

    if(EV_ERROR & revents)  
    {  
        printf("error event in write");  
        return;  
    }  

    //recv普通socket写法  
    /*read = write(watcher->fd, buffer, BUFFER_SIZE, 0);  */
    ssize_t send_len = send(watcher->fd, buffer, buffer_data_size, 0);  

    if(send_len != buffer_data_size)  
    {  
        printf("write error");  
        /*return;  */
    }  

    /*buffer_data_size -= send_len*/
    buffer_data_size = 0;

    //断开链接的处理,停掉evnet就可以,同时记得释放客户端的结构体!  
    if(send_len < 0)  
    {  
        printf("someone disconnected when write.\n");  
        ev_io_stop(loop,watcher);  
        free(watcher);  
        return;  
    }  
    ev_io_stop(loop, watcher);
    printf("write the message:[%s]\n",buffer);  
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
