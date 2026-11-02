#define _POSIX_C_SOURCE 200809L
#include <poll.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#define ERR(source) (perror(source), fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), exit(EXIT_FAILURE))
void set_nonblocking(int fd)
{
    int flags = fcntl(fd, F_GETFL);
    if (flags == -1)
        ERR("fcntl");
    flags |= O_NONBLOCK;
    if (fcntl(fd, F_SETFL, flags) == -1)
        ERR("fcntl");
}

void usage(const char *const pname)
{
    fprintf(stderr, "USAGE:%s path\n", pname);
    exit(EXIT_FAILURE);
}
int main(void)
{

    int mypipe;
    if(mkfifo("my_pipe", 0666) == -1){
        ERR("mkfifo");
    }
    if ((mypipe = open("my_pipe", O_RDONLY | O_NONBLOCK)) == -1){
        ERR("open");
    }
    set_nonblocking(STDIN_FILENO);
    struct pollfd fds[2];
    fds[0].fd = STDIN_FILENO;
    fds[0].events = POLLIN;
    fds[0].revents = 0;
    fds[1].fd = mypipe;
    fds[1].events = POLLIN;
    fds[1].revents = 0;
    while (1){
        int result = poll(fds, 2, 5000);
        if(result == -1){
            ERR("poll");
        } else if (result == 0){
            printf("Время вышло!\n");
        } else {
            if(fds[0].revents & POLLIN){
                char buf[64];
                ssize_t calk = read(STDIN_FILENO,buf,63);
                if (calk > 0) {
                buf[calk] = '\0';
                printf("[Клавиатура]: %s", buf);
                }
            }
            if(fds[1].revents & POLLIN){
                char buf[64];
                ssize_t calk = read(mypipe,buf,63);
                if (calk > 0) {
                buf[calk] = '\0';
                printf("[Из FIFO]: %s", buf);
                }
            }
        }
    }


    return EXIT_SUCCESS;
}