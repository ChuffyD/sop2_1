/* Включаем стандарты POSIX.1-2008 для доступа к системным вызовам fcntl, ftruncate, poll и mmap */
#define _POSIX_C_SOURCE 200809L

#include <errno.h>     // переменная errno и коды системных ошибок
#include <fcntl.h>     // open(), fcntl(), флаги O_RDWR, O_CREAT, O_NONBLOCK, F_GETFL, F_SETFL
#include <poll.h>      // poll(), struct pollfd, событие POLLIN
#include <stdio.h>     // printf(), fprintf(), snprintf(), perror(), stderr
#include <stdlib.h>    // exit(), EXIT_FAILURE
#include <string.h>    // memcpy() — побайтовое копирование данных в памяти
#include <sys/mman.h>  // mmap(), msync(), PROT_READ, PROT_WRITE, MAP_SHARED, MAP_FAILED
#include <sys/stat.h>  // константы прав доступа (0644) и структуры метаданных файлов
#include <unistd.h>    // read(), ftruncate(), STDIN_FILENO, ssize_t

/* Макрос обработки критических ошибок системных вызовов:
 * 1. perror(source) выводит текстовое описание ошибки из errno;
 * 2. fprintf печатает имя файла (__FILE__) и номер строки (__LINE__), где произошёл сбой;
 * 3. exit(EXIT_FAILURE) немедленно завершает программу с кодом ошибки. */
#define ERR(source) (perror(source), fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), exit(EXIT_FAILURE))

/* Константа размера буфера (в данном файле объявлена про запас и ниже не используется) */
#define FILE_BUF_LEN 256

/* Вспомогательная функция для вывода подсказки по запуску программы */
void usage(const char *const pname)
{
    fprintf(stderr, "USAGE:%s path_1 path_2\n", pname);
    exit(EXIT_FAILURE);
}

/* Перевод открытого файлового дескриптора fd в неблокирующий режим (O_NONBLOCK).
 * Сначала считываем текущие флаги (F_GETFL), добавляем бит O_NONBLOCK через побитовое ИЛИ (|=)
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

int main(int argc, char *argv[])
{
    /* Открываем файл по пути из argv[1] или создаём его (O_CREAT) с правами rw-r--r-- (0644).
     * Режим O_RDWR обязателен, так как ниже мы будем отображать файл в память с правами
     * на запись (PROT_WRITE | MAP_SHARED). При O_RDONLY вызов mmap завершился бы ошибкой EACCES. */
    int desc;
    if ((desc = open(argv[1], O_RDWR | O_CREAT, 0644)) == -1) {
        ERR("open");
    }

    /* Устанавливаем физический размер файла ровно 32 байта.
     * Это обязательно перед mmap(): если файл только что создан (размер 0 байт), попытка
     * записать данные в отображённую память за пределами файла вызовет сигнал SIGBUS. */
    if (ftruncate(desc, 32) == -1) {
        ERR("ftruncate");
    }

    /* Отображаем первые 32 байта файла напрямую в виртуальную память процесса:
     * NULL — ядро само выбирает свободный адрес;
     * 32 — размер отображаемого участка;
     * PROT_WRITE | PROT_READ — разрешаем запись и чтение этой области;
     * MAP_SHARED — изменения в массиве adress напрямую попадают в файл на диске;
     * desc, 0 — дескриптор файла и смещение от начала (0 байт). */
    char *adress;
    if ((adress = mmap(NULL, 32, PROT_WRITE | PROT_READ, MAP_SHARED, desc, 0)) == MAP_FAILED) {
        ERR("mmap");
    }

    /* Переводим стандартный ввод (клавиатуру, дескриптор 0) в неблокирующий режим */
    set_nonblocking(STDIN_FILENO);

    /* Настраиваем структуру pollfd для отслеживания событий на стандартном вводе */
    struct pollfd fds[1];
    fds[0].fd = STDIN_FILENO; // За каким дескриптором следим
    fds[0].events = POLLIN;   // Какое событие ждём (появление данных для чтения)
    fds[0].revents = 0;       // Поле для результата, которое заполнит ядро после возврата из poll()

    while (1) {
        /* Ждём ввод пользователя максимум 5000 мс (5 секунд).
         * Пока клавиши не нажаты, процесс спит в ядре и не нагружает процессор. */
        int result = poll(fds, 1, 5000);

        if (result == -1) {
            /* Ошибка системного вызова poll() */
            ERR("poll");
        } else if (result == 0) {
            /* Таймаут (прошло 5 секунд без ввода):
             * записываем статус "[SLEEPING...]" (вместе с завершающим '\0') в отображённую память файла */
            snprintf(adress, 32, "[SLEEPING...]");
            printf("[SLEEPING...]\n");

            /* Принудительно сбрасываем изменённую страницу памяти на диск (MS_SYNC) */
            msync(adress, 32, MS_SYNC);
        } else {
            /* result > 0: пришли данные со стандартного ввода. Проверяем бит готовности POLLIN */
            if (fds[0].revents & POLLIN) {
                char buf[32];
                /* Читаем до 32 сырых байт из stdin (включая символ '\n' при нажатии Enter) */
                ssize_t calk = read(STDIN_FILENO, buf, 32);
                if (calk > 0) {
                    /* Копируем прочитанные calk байт в начало отображённого файла поверх старых данных
                     * и сразу синхронизируем изменения на диск */
                    memcpy(adress, buf, calk);
                    msync(adress, 32, MS_SYNC);
                }
            }
        }
    }

    return 0;
}