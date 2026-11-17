/* Включаем стандарты POSIX.1-2008 для доступа к системным вызовам fcntl, ftruncate, poll и mmap */
#define _POSIX_C_SOURCE 200809L

#include <errno.h>     // переменная errno и коды системных ошибок
#include <fcntl.h>     // open(), fcntl(), флаги O_RDWR, O_CREAT, O_NONBLOCK, F_GETFL, F_SETFL
#include <poll.h>      // poll(), struct pollfd, событие POLLIN
#include <stdio.h>     // printf(), fprintf(), perror(), stderr
#include <stdlib.h>    // exit(), EXIT_SUCCESS, EXIT_FAILURE
#include <string.h>    // memset(), memcpy() — побайтовая работа с областями памяти
#include <sys/mman.h>  // mmap(), msync(), PROT_READ, PROT_WRITE, MAP_SHARED, MAP_FAILED
#include <sys/stat.h>  // константы прав доступа (0644) и структуры метаданных файлов
#include <unistd.h>    // read(), ftruncate(), STDIN_FILENO, ssize_t

/* Макрос обработки критических ошибок системных вызовов:
 * 1. perror(source) выводит текстовое описание ошибки из errno;
 * 2. fprintf печатает имя файла (__FILE__) и номер строки (__LINE__), где произошёл сбой;
 * 3. exit(EXIT_FAILURE) немедленно завершает программу с кодом ошибки. */
#define ERR(source) (perror(source), fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), exit(EXIT_FAILURE))

/* Перевод открытого файлового дескриптора fd в неблокирующий режим (O_NONBLOCK).
 * Сначала читаем текущую битовую маску флагов (F_GETFL), добавляем бит O_NONBLOCK через
 * побитовое ИЛИ (|=), и записываем обновлённые флаги обратно (F_SETFL), чтобы не затереть старые. */
void set_nonblocking(int fd)
{
    int flags = fcntl(fd, F_GETFL);
    if (flags == -1)
        ERR("fcntl");
    flags |= O_NONBLOCK;
    if (fcntl(fd, F_SETFL, flags) == -1)
        ERR("fcntl");
}

/* Вывод подсказки по запуску программы и выход при неверном количестве аргументов */
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

    /* Открываем файл или создаём его (O_CREAT) с правами rw-r--r-- (0644).
     * ВАЖНО: режим O_RDWR обязателен, так как ниже мы будем отображать файл в память
     * с флагами PROT_READ | PROT_WRITE и MAP_SHARED. Если открыть файл только на чтение
     * (O_RDONLY), ядро отклонит вызов mmap() с ошибкой EACCES. */
    if ((fd = open(file_path, O_RDWR | O_CREAT, 0644)) == -1) {
        ERR("open");
    }

    /* Устанавливаем физический размер файла ровно в 16 байт.
     * Это критически важный шаг перед mmap(): только что созданный файл имеет размер 0 байт,
     * и попытка обратиться к памяти за пределами реального размера файла вызовет сигнал SIGBUS. */
    if (ftruncate(fd, 16) == -1) {
        ERR("ftruncate");
    }

    /* Отображаем первые 16 байт файла напрямую в виртуальную память процесса:
     * NULL — ядро само выбирает свободный базовый адрес;
     * 16 — размер отображаемого участка в байтах;
     * PROT_READ | PROT_WRITE — разрешаем чтение и модификацию этой области памяти;
     * MAP_SHARED — изменения в массиве adress напрямую связаны с файлом на диске и видны другим процессам;
     * fd, 0 — дескриптор файла и смещение от его начала (0 байт). */
    char *adress;
    if ((adress = mmap(NULL, 16, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0)) == MAP_FAILED) {
        ERR("adress");
    }

    /* Переводим стандартный ввод (клавиатуру, дескриптор 0) в неблокирующий режим */
    set_nonblocking(STDIN_FILENO);

    /* Настраиваем структуру pollfd для отслеживания ввода с клавиатуры */
    struct pollfd fds[1];
    fds[0].fd = STDIN_FILENO; // За каким дескриптором следим
    fds[0].events = POLLIN;   // Какое событие ждём (появление данных для чтения)
    fds[0].revents = 0;       // Поле для результата, которое заполнит ядро после возврата из poll()

    while (1) {
        /* Ждём ввод пользователя максимум 2000 мс (2 секунды).
         * Пока клавиши не нажаты, процесс спит в ядре и не тратит ресурсы процессора. */
        int result = poll(fds, 1, 2000);

        if (result == -1) {
            /* Ошибка системного вызова poll() */
            ERR("poll");
        } else if (result == 0) {
            /* Таймаут (прошло 2 секунды без ввода):
             * заполняем все 16 байт отображённой памяти символом '-' через memset */
            printf("Время вышло!\n");
            memset(adress, '-', 16);

            /* Принудительно сбрасываем изменённую страницу памяти на диск (MS_SYNC — ждать завершения записи) */
            msync(adress, 16, MS_SYNC);

            /* Так как в 16-байтном буфере adress нет завершающего нулевого байта '\0',
             * используем спецификатор %.*s с ограничением 16, чтобы printf не вышел за границы массива */
            printf("Таймаут: %.*s\n", 16, adress);
        } else {
            /* result > 0: пришли данные со стандартного ввода. Проверяем бит готовности POLLIN */
            if (fds[0].revents & POLLIN) {
                char buf[16];
                /* Читаем до 16 сырых байт из stdin (включая символ '\n' при нажатии Enter) */
                ssize_t calk = read(STDIN_FILENO, buf, 16);
                if (calk > 0) {
                    /* Копируем прочитанные calk байт в начало отображённого файла поверх старых символов
                     * и сразу синхронизируем изменения на диск */
                    memcpy(adress, buf, calk);
                    msync(adress, 16, MS_SYNC);
                }
            }
        }
    }

    return EXIT_SUCCESS;
}