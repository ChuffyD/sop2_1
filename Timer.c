/* Включаем стандарты POSIX.1-2008 для доступа к системным вызовам fcntl, poll и их флагам */
#define _POSIX_C_SOURCE 200809L

#include <errno.h>     // переменная errno и коды системных ошибок
#include <fcntl.h>     // fcntl(), F_GETFL, F_SETFL, O_NONBLOCK
#include <poll.h>      // poll(), struct pollfd, событие POLLIN
#include <stdio.h>     // printf(), fprintf(), perror(), stderr
#include <stdlib.h>    // exit(), EXIT_FAILURE
#include <string.h>    // стандартные функции работы со строками и памятью (запасной заголовок)
#include <sys/mman.h>  // функции отображения в память (запасной заголовок)
#include <sys/stat.h>  // структуры и макросы информации о файлах (запасной заголовок)
#include <unistd.h>    // read(), STDIN_FILENO, ssize_t

/* Макрос обработки критических ошибок системных вызовов:
 * 1. perror(source) выводит текстовое описание ошибки из errno;
 * 2. fprintf печатает имя файла (__FILE__) и номер строки (__LINE__), где произошёл сбой;
 * 3. exit(EXIT_FAILURE) аварийно завершает программу. */
#define ERR(source) (perror(source), fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), exit(EXIT_FAILURE))

/* Перевод любого открытого файлового дескриптора fd в неблокирующий режим (O_NONBLOCK).
 * ВАЖНО: нельзя просто вызвать fcntl(fd, F_SETFL, O_NONBLOCK) — это затрёт все старые флаги
 * дескриптора (например, O_APPEND или режим доступа). Поэтому делаем в 3 шага: */
void set_nonblocking(int fd)
{
    /* 1. Считываем текущую битовую маску флагов дескриптора */
    int flags = fcntl(fd, F_GETFL);
    if (flags == -1)
        ERR("fcntl F_GETFL");

    /* 2. Добавляем бит O_NONBLOCK через побитовое ИЛИ (|=), сохраняя остальные флаги */
    flags |= O_NONBLOCK;

    /* 3. Записываем обновлённую маску флагов обратно в ядро */
    if (fcntl(fd, F_SETFL, flags) == -1)
        ERR("fcntl F_SETFL");
}

int main(void)
{
    /* Переводим стандартный ввод (клавиатуру, дескриптор STDIN_FILENO == 0) в неблокирующий режим */
    set_nonblocking(STDIN_FILENO);

    /* Настраиваем массив из 1 структуры pollfd для отслеживания событий на клавиатуре */
    struct pollfd fds[1];
    fds[0].fd = STDIN_FILENO; // За каким дескриптором следим
    fds[0].events = POLLIN;   // Какое событие ждём (