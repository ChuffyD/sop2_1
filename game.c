/* Включаем стандарты POSIX.1-2008 для доступа к системным вызовам fcntl, fstat, mmap и poll */
#define _POSIX_C_SOURCE 200809L

#include <errno.h>     // переменная errno и коды системных ошибок
#include <fcntl.h>     // open(), fcntl(), флаги O_WRONLY, O_RDWR, O_CREAT, O_TRUNC, O_NONBLOCK, F_GETFL, F_SETFL
#include <poll.h>      // poll(), struct pollfd, событие POLLIN
#include <stdio.h>     // printf(), fprintf(), perror(), stderr
#include <stdlib.h>    // exit(), EXIT_FAILURE
#include <string.h>    // функции работы со строками и памятью
#include <sys/mman.h>  // mmap(), msync(), munmap(), PROT_READ, PROT_WRITE, MAP_SHARED, MAP_FAILED
#include <sys/stat.h>  // fstat(), struct stat, константы прав доступа (0644)
#include <unistd.h>    // write(), read(), close(), STDIN_FILENO, ssize_t

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
        ERR("fcntl F_GETFL");
    flags |= O_NONBLOCK;
    if (fcntl(fd, F_SETFL, flags) == -1)
        ERR("fcntl F_SETFL");
}

int main(void)
{
    /* === ШАГ 1: Подготовка игрового поля в файле === */
    /* Создаём файл "game.txt" (или очищаем его до 0 байт флагом O_TRUNC, если он уже был)
     * с правами rw-r--r-- (0644) для начальной инициализации поля */
    int fd = open("game.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd == -1) {
        ERR("open");
    }

    /* Записываем стартовое поле из 10 точек (без завершающего '\0') и закрываем дескриптор */
    char *field = "..........";
    if ((write(fd, field, 10)) == -1) {
        ERR("write");
    }
    close(fd);

    /* === ШАГ 2: Открытие для mmap и получение размера файла === */
    /* Открываем файл повторно, но теперь обязательно в режиме O_RDWR (чтение и запись):
     * для отображения в память с PROT_READ | PROT_WRITE и MAP_SHARED ядро требует,
     * чтобы дескриптор был открыт на чтение и запись (при O_WRONLY или O_RDONLY mmap упадёт с EACCES). */
    int new_fd = open("game.txt", O_RDWR);
    if (new_fd == -1) {
        ERR("open");
    }

    /* Узнаём точный размер файла на диске через fstat() (в sb.st_size запишется 10 байт) */
    struct stat sb;
    if (fstat(new_fd, &sb) == -1)
        ERR("fstat");

    /* Отображаем весь файл (sb.st_size байт) в виртуальную память процесса:
     * NULL — ядро само подбирает свободный адрес;
     * sb.st_size — длина отображаемого участка (10 байт);
     * PROT_READ | PROT_WRITE — разрешаем чтение и запись в эту память;
     * MAP_SHARED — изменения в массиве map_ptr напрямую меняют файл на диске;
     * new_fd, 0 — дескриптор файла и смещение от начала (0 байт). */
    char *map_ptr = mmap(NULL, sb.st_size, PROT_READ | PROT_WRITE, MAP_SHARED, new_fd, 0);
    if (map_ptr == MAP_FAILED) {
        ERR("mmap");
    }

    /* === ШАГ 3: Настройка неблокирующего ввода и игрового цикла === */
    set_nonblocking(STDIN_FILENO);

    struct pollfd fds[1];
    fds[0].fd = STDIN_FILENO; // Следим за стандартным вводом (клавиатурой)
    fds[0].events = POLLIN;   // Ждём появления данных для чтения
    fds[0].revents = 0;       // Поле для результата, которое заполнит ядро

    char buf[16];
    int step = 0; // Индекс текущей ячейки поля (от 0 до 9)

    printf("Начали! У тебя 3 секунды на каждый символ:\n");

    /* Игровой цикл: заполняем 10 позиций поля, давая по 3 секунды на каждый ход */
    while (step < 10) {
        /* Ждём ввод пользователя максимум 3000 мс (3 секунды) */
        int result = poll(fds, 1, 3000);

        if (result == -1) {
            /* Ошибка системного вызова poll() */
            ERR("poll");
        } else if (result == 0) {
            /* Таймаут: за 3 секунды пользователь не нажал Enter — игра проиграна */
            printf("Время вышло!\n");
            break;
        } else {
            /* result > 0: пришли данные со стандартного ввода. Проверяем бит POLLIN */
            if (fds[0].revents & POLLIN) {
                /* Считываем строку из stdin (например, символ + '\n') */
                ssize_t n = read(STDIN_FILENO, buf, 15);
                if (n > 0) {
                    /* Берём первый введённый символ (buf[0]) и записываем его напрямую
                     * в отображённую память файла по индексу step */
                    map_ptr[step] = buf[0];

                    /* Принудительно сбрасываем обновлённую страницу памяти на диск (MS_SYNC) */
                    msync(map_ptr, sb.st_size, MS_SYNC);
                    step++;

                    /* Выводим текущее состояние поля: %.*s ограничивает печать ровно 10 байтами,
                     * так как в файле нет завершающего нулевого байта '\0' */
                    printf("Поле: %.*s\n", 10, map_ptr);
                }
            }
        }
    }

    /* === ШАГ 4: Освобождение ресурсов === */
    /* Отменяем отображение файла в память (ядро также сбросит оставшиеся изменения) */
    munmap(map_ptr, sb.st_size);
    /* Закрываем файловый дескриптор */
    close(new_fd);

    return 0;
}