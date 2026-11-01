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
    int fd = open("game.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd==-1){
        ERR("open");
    }
    char *field = "..........";
    if((write(fd,field,10))==-1){
        ERR("write");
    }
    close(fd);
    int new_fd = open("game.txt", O_RDWR);
    if (new_fd == -1){
        ERR("open");
    }
    struct stat sb;
    if(fstat(new_fd, &sb) == -1) ERR("fstat");
    char *map_ptr = mmap(NULL, sb.st_size, PROT_READ | PROT_WRITE, MAP_SHARED, new_fd,0);
    if(map_ptr == MAP_FAILED){
        ERR("mmap");
    }
    set_nonblocking(STDIN_FILENO);
    struct pollfd fds[1];
    fds[0].fd = STDIN_FILENO;
    fds[0].events = POLLIN;
    fds[0].revents = 0;
    char buf[16];
    int step = 0;
    printf("Начали! У тебя 3 секунды на каждый символ:\n");
    while (step < 10){
        int result = poll(fds,1,3000);
        if (result == -1){
            ERR("poll");
        }
        else if (result == 0){
            printf("Время вышло!\n");
            break;
        } else {
            if (fds[0].revents & POLLIN){
                ssize_t n = read(STDIN_FILENO,buf,15);
                if (n > 0){
                map_ptr[step] = buf[0];
                msync(map_ptr, sb.st_size, MS_SYNC);
                step++;
                printf("Поле: %.*s\n", 10, map_ptr);
                }
            }
        }
    }
    munmap(map_ptr,sb.st_size);
    close(new_fd);
    return 0;
}