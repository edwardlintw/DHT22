#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <string.h>

#define BUF_MAX         64
#define SIZEOF(array)   (sizeof(array)/sizeof(array[0]))

struct thread_param_t {
    char    path[BUF_MAX];
    char    name[BUF_MAX];
    int     time_out;
};

void* thread_work(void*);

int main(int argc, char* argv[])
{
    int                     i;
    pthread_t               threads[2];
    struct thread_param_t   params [2] = {  {
            .path = "/sys/kernel/dht22/temperature",
            .name = "temperature",
            .time_out = 20000, /* ms */
        }, {
            .path = "/sys/kernel/dht22/humidity",
            .name = "humidity",
            .time_out = 20000, /* ms */
        }
    };

    for (i = 0; i < SIZEOF(threads); ++i) 
        pthread_create(&threads[i], NULL, thread_work, (void*)&params[i]);

    for (i = 0; i < SIZEOF(threads); ++i)
        pthread_join(threads[i], NULL);

    return 0;
}

void* thread_work(void* arg)
{
    struct thread_param_t   param;
    struct pollfd           pfd = { .events = POLLPRI };
    int                     ret = 1;
    char                    buf[BUF_MAX];

    memcpy((void*)&param, arg, sizeof(param));
    while (1) {
        /*
         * open/close in loop is a must,
         * or poll() won't be block
         */
        pfd.fd = open(param.path, O_RDONLY);
        if (-1 == pfd.fd) {
            printf("Can't open %s\n", param.path);
            break;
        }
        /*
         * a dummy read is a must,
         * or poll() won't be blocked
         */
        read(pfd.fd, buf, sizeof(buf));
        ret = poll(&pfd, 1, param.time_out);
        if (0 > ret) 
            printf("poll error\n");
        else if (0 == ret)
            printf("poll timeout\n");
        else
            printf("%s: %s", param.name, buf);

        close(pfd.fd);
    }

    return NULL;
}

