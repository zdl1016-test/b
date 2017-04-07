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

int repeat_times = 0;

void timer_cb(struct ev_loop *main_loop, struct ev_timer *w, int events)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    printf("timer_cb at %lds %ldus\n",tv.tv_sec, tv.tv_usec);

    repeat_times++;
    if (repeat_times > 10)
    {
        printf("over times limit, stop timer\n");
        ev_timer_stop(main_loop, w);
    }
}

int main(int argc, char *argv[])
{
    struct ev_loop *main_loop = ev_default_loop(0);
    ev_timer w;
    ev_init(&w, timer_cb);
    w.repeat = 1.; // sec
    ev_timer_again(main_loop, &w); // do not need ev_timer_start

    ev_run(main_loop, 0);
    ev_loop_destroy(main_loop);
    return 0;
}
