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

#define FILE_BUF_LEN 256

void usage(const char *const pname)
{
    fprintf(stderr, "USAGE:%s path_1 path_2\n", pname);
    exit(EXIT_FAILURE);
}
void set_nonblocking(int fd)
{
    int flags = fcntl(fd, F_GETFL);
    if (flags == -1)
        ERR("fcntl");
    flags |= O_NONBLOCK;
    if (fcntl(fd, F_SETFL, flags) == -1)
        ERR("fcntl");
}
int main(int argc, char *argv[])
{
    int desc;
    if(( desc = open(argv[1],O_RDWR | O_CREAT, 0644))==-1){
        ERR("open");
    }
    if(ftruncate(desc,32)==-1){
        ERR("ftruncate");
    }
    char* adress;
    if((adress = mmap(NULL,32,PROT_WRITE | PROT_READ,MAP_SHARED,desc,0))==MAP_FAILED){
        ERR("mmap");
    }
    set_nonblocking(STDIN_FILENO);
    struct pollfd fds[1];
    fds[0].fd = STDIN_FILENO;
    fds[0].events = POLLIN;
    fds[0].revents = 0;
    while(1){
        int result = poll(fds,1,5000);
        if(result == -1){
            ERR("poll");
        } else if (result == 0){
            snprintf(adress,32,"[SLEEPING...]");
            printf("[SLEEPING...]\n");
            msync(adress,32, MS_SYNC);
        } else {
            if(fds[0].revents & POLLIN){
                char buf[32];
                ssize_t calk = read(STDIN_FILENO,buf,32);
                if(calk > 0){
                    memcpy(adress,buf,calk);
                    msync(adress,32,MS_SYNC);
                }
            }
        }
    }
    
    return 0;
}