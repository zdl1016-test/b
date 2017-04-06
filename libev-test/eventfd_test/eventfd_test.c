#include <stdio.h>
#include <unistd.h>
#include <sys/time.h>
#include <stdint.h>
#include <pthread.h>
#include <sys/eventfd.h>
#include <ev.h>

int efd = -1;

void event_fd_read(struct ev_loop *main_loop, ev_io *sock_w, int events)
{
    uint64_t count = 0;
    int event_fd = sock_w->fd;
    int ret_1 = read(event_fd, &count, sizeof(count));
    if (ret_1 < 0)
    {
        perror("read fail:");
        /*goto fail;*/
    }
    else
    {
        struct timeval tv;

        gettimeofday(&tv, NULL);
        printf("success read from efd, read %d bytes(%llu) at %lds %ldus\n",
                ret_1, count, tv.tv_sec, tv.tv_usec);
    }
}

void *read_thread(void *dummy)
{
    int ret = 0;
    uint64_t count = 0;

    if (efd < 0)
    {
        printf("efd not inited.\n");
        goto fail;
    }

    struct ev_loop *main_loop = ev_default_loop(0);
    ev_io sock_w;
    ev_init(&sock_w, event_fd_read);
    ev_io_set(&sock_w, efd, EV_READ);
    ev_io_start(main_loop, &sock_w);

    ev_run(main_loop, 0);

fail:
    return NULL;
}

int main(int argc, char *argv[])
{
    pthread_t pid = 0;
    uint64_t count = 0;
    int ret = 0;
    int i = 0;

    efd = eventfd(0, EFD_NONBLOCK);
    if (efd < 0)
    {
        perror("eventfd failed.");
        goto fail;
    }

    ret = pthread_create(&pid, NULL, read_thread, NULL);
    if (ret < 0)
    {
        perror("pthread create:");
        goto fail;
    }

    for (i = 0; i < 5; i++)
    {
        printf("please input a num:\n");
        char buff[128] = {0};
        fgets(buff, sizeof(buff)-1, stdin);
        count = (uint64_t)atoi(buff);
        ret = write(efd, &count, sizeof(count));
        if (ret < 0)
        {
            perror("write event fd fail:");
            goto fail;
        }
        else
        {
            struct timeval tv;

            gettimeofday(&tv, NULL);
            printf("success write to efd, write %d bytes(%llu) at %lds %ldus\n",
                   ret, count, tv.tv_sec, tv.tv_usec);
        }

        //sleep(1);
    }

fail:
    if (0 != pid)
    {
        pthread_join(pid, NULL);
        pid = 0;
    }

    if (efd >= 0)
    {
        close(efd);
        efd = -1;
    }
    return ret;
}
