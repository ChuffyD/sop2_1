/* Включаем стандарты POSIX.1-2008, чтобы были доступны современные системные вызовы (ftruncate, poll, mmap и др.) */
#define _POSIX_C_SOURCE 200809L

#include <errno.h>     // переменная errno и коды системных ошибок
#include <fcntl.h>     // open(), fcntl(), флаги O_RDWR, O_CREAT, O_NONBLOCK, F_GETFL, F_SETFL
#include <poll.h>      // poll(), struct pollfd, событие POLLIN
#include <stdio.h>     // printf(), fprintf(), snprintf(), perror(), stderr
#include <stdlib.h>    // exit(), EXIT_SUCCESS, EXIT_FAILURE
#include <string.h>    // функции работы со строками и памятью
#include <sys/mman.h>  // mmap(), msync(), PROT_READ, PROT_WRITE, MAP_SHARED, MAP_FAILED
#include <sys/stat.h>  // константы прав доступа (0644) и структуры метаданных файлов
#include <unistd.h>    // read(), ftruncate(), STDIN_FILENO, ssize_t

/* Макрос обработки критических системных ошибок:
 * 1. perror(source) выводит текстовое описание ошибки из errno;
 * 2. fprintf печатает имя файла (__FILE__) и номер строки (__LINE__), где произошёл сбой;
 * 3. exit(EXIT_FAILURE) немедленно завершает программу с кодом ошибки. */
#define ERR(source) (perror(source), fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), exit(EXIT_FAILURE))

/* Перевод открытого дескриптора fd в неблокирующий режим (O_NONBLOCK).
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

/* Вывод подсказки по правильному запуску программы и выход при неверных аргументах */
void usage(const char *const pname)
{
    fprintf(stderr, "USAGE:%s path\n", pname);
    exit(EXIT_FAILURE);
}

int main(int argc, char *argv[])
{
    /* Проверяем, что передан ровно 1 аргумент — путь к файлу состояния (argc == 2) */
    if (argc != 2)
        usage(argv[0]);

    char *file_path = argv[1];
    int fd;

    /* Открываем файл или создаём его (O_CREAT), если он ещё не существует, с правами rw-r--r-- (0644).
     * ВАЖНО: нужен именно режим O_RDWR (чтение и запись), так как ниже мы будем отображать файл
     * в память с флагами PROT_READ | PROT_WRITE и MAP_SHARED (при O_RDONLY вызов mmap упадёт с EACCES). */
    if ((fd = open(file_path, O_RDWR | O_CREAT, 0644)) == -1) {
        ERR("open");
    }

    /* Устанавливаем размер файла ровно 64 байта.
     * Это критически важный шаг перед mmap(): новый созданный файл имеет размер 0 байт,
     * и попытка записать что-либо в отображённую память за пределами реального размера файла
     * мгновенно убьёт программу сигналом SIGBUS (Bus error). */
    if (ftruncate(fd, 64) == -1) {
        ERR("ftruncate");
    }

    /* Отображаем первые 64 байта файла напрямую в виртуальную память процесса:
     * NULL — ядро само выбирает свободный адрес;
     * 64 — размер отображаемого участка в байтах;
     * PROT_READ | PROT_WRITE — разрешаем читать и изменять эту память;
     * MAP_SHARED — изменения в памяти напрямую связаны с файлом на диске и видны другим процессам;
     * fd, 0 — дескриптор файла и смещение от его начала (0 байт). */
    char *adress;
    if ((adress = mmap(NULL, 64, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0)) == MAP_FAILED) {
        ERR("adress");
    }

    /* Переводим стандартный ввод (клавиатуру) в неблокирующий режим */
    set_nonblocking(STDIN_FILENO);

    /* Настраиваем структуру pollfd для отслеживания ввода с клавиатуры */
    struct pollfd fds[1];
    fds[0].fd = STDIN_FILENO; // Следим за дескриптором 0 (stdin)
    fds[0].events = POLLIN;   // Ждём появления данных для чтения
    fds[0].revents = 0;       // Поле для результата, которое заполнит ядро

    while (1) {
        /* Ждём ввод пользователя максимум 7000 мс (7 секунд).
         * Процесс спит в ядре и не тратит ресурсы процессора, пока не нажмут клавишу или не выйдет время. */
        int result = poll(fds, 1, 7000);

        if (result == -1) {
            /* Ошибка системного вызова poll() */
            ERR("poll");
        } else if (result == 0) {
            /* Таймаут (7 секунд прошло, пользователь ничего не ввёл):
             * записываем статус "[AWAY]" напрямую в отображённую память файла (не больше 64 байт) */
            printf("Время вышло!\n");
            snprintf(adress, 64, "[AWAY]");

            /* Принудительно сбрасываем изменённую страницу памяти на диск (MS_SYNC — ждать завершения записи) */
            msync(adress, 64, MS_SYNC);

            /* Спецификатор %.*s печатает строку из adress, но гарантированно не более 64 символов,
             * защищая printf от выхода за пределы буфера, даже если там не окажется '\0' */
            printf("Таймаут: %.*s\n", 64, adress);
        } else {
            /* result > 0: пользователь ввёл данные. Проверяем бит готовности к чтению POLLIN */
            if (fds[0].revents & POLLIN) {
                char buf[64];
                /* Читаем до 63 байт из stdin, оставляя 1 байт под завершающий '\0' */
                ssize_t calk = read(STDIN_FILENO, buf, 63);
                if (calk > 0) {
                    buf[calk] = '\0';                 // Завершаем строку нулевым байтом
                    snprintf(adress, 64, "%s", buf);  // Копируем введённый текст в отображённую память файла
                    msync(adress, 64, MS_SYNC);       // Синхронизируем память с файлом на диске
                    printf("Записано: %s", buf);
                }
            }
        }
    }

    return EXIT_SUCCESS;
}