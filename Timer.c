#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <poll.h>
#define ERR(source) (perror(source), fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), exit(EXIT_FAILURE))
void set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL);
    if (flags == -1) ERR("fcntl F_GETFL");
    flags |= O_NONBLOCK;
    if (fcntl(fd, F_SETFL, flags) == -1) ERR("fcntl F_SETFL");
}
int main(void){
    set_nonblocking(STDIN_FILENO);
    struct pollfd fds[1];
    fds[0].fd = STDIN_FILENO;
    fds[0].events = POLLIN;
    fds[0].revents = 0;
    char buf[256];
    while(1){
        int result = poll(fds,1,5000);
        if (result == -1){
            ERR("poll");
        }
        else if (result == 0){
            printf("Время вышло!\n");
            break;
        } else {
            if (fds[0].revents & POLLIN){
                ssize_t n = read(STDIN_FILENO,buf,255);
                if (n > 0){
                buf[n] = '\0';
                printf("Введено: %s", buf);
                } else if (n == 0) {
                    printf("Конец ввода\n");
                    break;
                } else {
                    ERR("read");
                }
            }
        }
    }
}