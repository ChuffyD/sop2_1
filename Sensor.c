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
int main(int argc, char *argv[])
{
    if (argc != 2)
        usage(argv[0]);

    char *file_path = argv[1];
    int fd;
    if ((fd = open(file_path, O_RDWR | O_CREAT, 0644)) == -1){
        ERR("open");
    }
    if(ftruncate(fd, 64) == -1){
        ERR("ftruncate");
    }
    char* adress;
    if((adress = mmap(NULL,64,PROT_READ | PROT_WRITE,MAP_SHARED,fd, 0))== MAP_FAILED){
        ERR("adress");
    }
    set_nonblocking(STDIN_FILENO);
    struct pollfd fds[1];
    fds[0].fd = STDIN_FILENO;
    fds[0].events = POLLIN;
    fds[0].revents = 0;
    while (1){
        int result = poll(fds, 1, 7000);
        if(result == -1){
            ERR("poll");
        } else if (result == 0){
            printf("Время вышло!\n");
            snprintf(adress,64,"[AWAY]");
            msync(adress,64, MS_SYNC);
            printf("Таймаут: %.*s\n", 64, adress);
        } else {
            if(fds[0].revents & POLLIN){
                char buf[64];
                ssize_t calk = read(STDIN_FILENO,buf,63);
                if (calk > 0) {
                    buf[calk] = '\0';
                    snprintf(adress, 64, "%s", buf);
                    msync(adress, 64, MS_SYNC);     
                    printf("Записано: %s", buf);
                }
            }
        }
    }


    return EXIT_SUCCESS;
}