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
    if ((fd = open(file_path, O_RDWR)) == -1){
        ERR("open");
    }
    struct stat sb;
    if(fstat(fd,&sb) == -1){
        ERR("fstat");
    }
    int size = sb.st_size;
    char* adress;
    if((adress = mmap(NULL,size,PROT_READ | PROT_WRITE,MAP_SHARED,fd, 0))== MAP_FAILED){
        ERR("adress");
    }
    for (int i=0;i<size; i++){
        if(adress[i] == ' '){
            adress[i]= '_';
        }
    }
    msync(adress,size,MS_SYNC);
    munmap(adress,size);
    close(fd);
    return EXIT_SUCCESS;
}