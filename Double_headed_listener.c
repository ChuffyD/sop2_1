/* Включаем стандарты POSIX.1-2008 для доступа к системным вызовам mkfifo, fcntl и poll */
#define _POSIX_C_SOURCE 200809L

#include <errno.h>     // переменная errno и коды системных ошибок
#include <fcntl.h>     // open(), fcntl(), флаги O_RDONLY, O_NONBLOCK, F_GETFL, F_SETFL
#include <poll.h>      // poll(), struct pollfd, событие POLLIN
#include <stdio.h>     // printf(), fprintf(), perror(), stderr
#include <stdlib.h>    // exit(), EXIT_SUCCESS, EXIT_FAILURE
#include <string.h>    // функции работы со строками и памятью (запасной заголовок из шаблона)
#include <sys/mman.h>  // функции отображения в память (запасной заголовок из шаблона)
#include <sys/stat.h>  // mkfifo(), константы прав доступа (0666)
#include <unistd.h>    // read(), STDIN_FILENO, ssize_t

/* Макрос обработки критических ошибок системных вызовов:
 * 1. perror(source) выводит текстовое описание ошибки из errno;
 * 2. fprintf печатает имя файла (__FILE__) и номер строки (__LINE__), где произошёл сбой;
 * 3. exit(EXIT_FAILURE) немедленно завершает программу с кодом ошибки. */
#define ERR(source) (perror(source), fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), exit(EXIT_FAILURE))

/* Перевод открытого файлового дескриптора fd в неблокирующий режим (O_NONBLOCK).
 * Сначала читаем текущие флаги (F_GETFL), добавляем бит O_NONBLOCK через побитовое ИЛИ (|=),
 * и записываем обратно (F_SETFL), чтобы не затереть остальные флаги дескриптора. */
void set_nonblocking(int fd)
{
    int flags = fcntl(fd, F_GETFL);
    if (flags == -1)
        ERR("fcntl");
    flags |= O_NONBLOCK;
    if (fcntl(fd, F_SETFL, flags) == -1)
        ERR("fcntl");
}

/* Вспомогательная функция из шаблона для вывода подсказки по запуску программы */
void usage(const char *const pname)
{
    fprintf(stderr, "USAGE:%s path\n", pname);
    exit(EXIT_FAILURE);
}

int main(void)
{
    /* === ШАГ 1: Создание и открытие именованного канала (FIFO) === */
    /* Создаём в файловой системе специальный файл именованного канала "my_pipe" с правами rw-rw-rw- (0666,
     * итоговые права будут скорректированы маской umask процесса). Если файл уже есть, mkfifo вернёт EEXIST. */
    int mypipe;
    if (mkfifo("my_pipe", 0666) == -1) {
        ERR("mkfifo");
    }

    /* Открываем канал только на чтение (O_RDONLY) с обязательным флагом O_NONBLOCK.
     * ВАЖНО: без O_NONBLOCK вызов open() на FIFO с флагом O_RDONLY заблокировал бы программу
     * и ждал, пока какой-нибудь другой процесс не откроет этот же канал на запись (O_WRONLY).
     * Флаг O_NONBLOCK позволяет open() вернуться мгновенно, даже если писателей ещё нет. */
    if ((mypipe = open("my_pipe", O_RDONLY | O_NONBLOCK)) == -1) {
        ERR("open");
    }

    /* Переводим стандартный ввод (клавиатуру) в неблокирующий режим */
    set_nonblocking(STDIN_FILENO);

    /* === ШАГ 2: Настройка мультиплексирования ввода-вывода через poll() === */
    /* Создаём массив из 2 структур pollfd, чтобы одновременно слушать два источника данных */
    struct pollfd fds[2];

    /* Первый источник (индекс 0) — клавиатура */
    fds[0].fd = STDIN_FILENO;
    fds[0].events = POLLIN; // Ждём появления данных для чтения
    fds[0].revents = 0;     // Сюда ядро запишет произошедшие события

    /* Второй источник (индекс 1) — именованный канал my_pipe */
    fds[1].fd = mypipe;
    fds[1].events = POLLIN; // Ждём появления данных от другого процесса
    fds[1].revents = 0;

    while (1) {
        /* Ждём события сразу на 2 дескрипторах максимум 5000 мс (5 секунд).
         * Процесс спит в ядре и просыпается, как только данные появятся хотя бы в одном канале
         * либо истечёт таймер. */
        int result = poll(fds, 2, 5000);

        if (result == -1) {
            /* Системная ошибка вызова poll() */
            ERR("poll");
        } else if (result == 0) {
            /* Таймаут: за 5 секунд не пришло данных ни с клавиатуры, ни из FIFO */
            printf("Время вышло!\n");
        } else {
            /* result > 0: хотя бы один дескриптор готов.
             * ВАЖНО: проверяем оба дескриптора независимыми блоками if (а не else if),
             * чтобы обработать оба источника, если данные пришли в них одновременно. */

            /* Проверяем готовность клавиатуры (fds[0]) через побитовое И (&) */
            if (fds[0].revents & POLLIN) {
                char buf[64];
                /* Читаем до 63 байт, оставляя 1 байт под завершающий '\0' */
                ssize_t calk = read(STDIN_FILENO, buf, 63);
                if (calk > 0) {
                    buf[calk] = '\0';
                    printf("[Клавиатура]: %s", buf);
                }
            }

            /* Проверяем готовность именованного канала (fds[1]) */
            if (fds[1].revents & POLLIN) {
                char buf[64];
                ssize_t calk = read(mypipe, buf, 63);
                if (calk > 0) {
                    buf[calk] = '\0';
                    printf("[Из FIFO]: %s", buf);
                }
                /* Примечание: когда процесс-писатель закроет канал, ядро выставит флаг POLLHUP
                 * в fds[1].revents, а read() вернёт 0 (EOF) */
            }
        }
    }

    return EXIT_SUCCESS;
}