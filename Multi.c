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
 * 1. perror(source) печатает текстовое описание ошибки из errno;
 * 2. fprintf выводит имя файла (__FILE__) и номер строки (__LINE__), где произошёл сбой;
 * 3. exit(EXIT_FAILURE) немедленно завершает программу с кодом ошибки. */
#define ERR(source) (perror(source), fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), exit(EXIT_FAILURE))

/* Перевод открытого дескриптора fd в неблокирующий режим (O_NONBLOCK).
 * Сначала читаем текущую битовую маску флагов (F_GETFL), добавляем бит O_NONBLOCK через
 * побитовое ИЛИ (|=), и записываем обновлённые флаги обратно (F_SETFL), не затирая старые. */
void set_nonblocking(int fd)
{
    int flags = fcntl(fd, F_GETFL);
    if (flags == -1)
        ERR("fcntl");
    flags |= O_NONBLOCK;
    if (fcntl(fd, F_SETFL, flags) == -1)
        ERR("fcntl");
}

/* Вывод подсказки по запуску программы при неверном количестве аргументов */
void usage(const char *const pname)
{
    fprintf(stderr, "USAGE:%s path\n", pname);
    exit(EXIT_FAILURE);
}

int main(int argc, char *argv[])
{
    /* Проверяем, что передан ровно 1 аргумент — путь к файлу-таблице (argc == 2) */
    if (argc != 2)
        usage(argv[0]);

    char *file_path = argv[1];
    int fd;

    /* Открываем файл или создаём его (O_CREAT) с правами rw-r--r-- (0644).
     * Режим O_RDWR обязателен, так как ниже мы будем отображать файл в память с правами
     * на запись (PROT_WRITE | MAP_SHARED). При O_RDONLY вызов mmap завершился бы ошибкой EACCES. */
    if ((fd = open(file_path, O_RDWR | O_CREAT, 0644)) == -1) {
        ERR("open");
    }

    /* Устанавливаем физический размер файла ровно в 16 байт.
     * Это обязательно перед mmap(): если файл пустой (0 байт), попытка записать данные
     * в отображённую память за пределами размера файла вызовет аварийный сигнал SIGBUS. */
    if (ftruncate(fd, 16) == -1) {
        ERR("ftruncate");
    }

    /* Отображаем первые 16 байт файла напрямую в виртуальную память процесса:
     * NULL — ядро само выбирает свободный адрес;
     * 16 — размер отображаемого участка;
     * PROT_READ | PROT_WRITE — разрешаем чтение и модификацию этой области;
     * MAP_SHARED — все изменения в массиве adress напрямую попадают в файл на диске;
     * fd, 0 — дескриптор файла и смещение от начала (0 байт). */
    char *adress;
    if ((adress = mmap(NULL, 16, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0)) == MAP_FAILED) {
        ERR("adress");
    }

    /* Переводим стандартный ввод (клавиатуру) в неблокирующий режим */
    set_nonblocking(STDIN_FILENO);

    /* Настраиваем структуру pollfd для отслеживания ввода с клавиатуры */
    struct pollfd fds[1];
    fds[0].fd = STDIN_FILENO; // Следим за дескриптором 0 (stdin)
    fds[0].events = POLLIN;   // Ждём появления данных для чтения
    fds[0].revents = 0;       // Поле для возвращаемых ядром событий

    while (1) {
        /* Ждём ввод пользователя максимум 2000 мс (2 секунды).
         * Процесс спит в ядре и не тратит ресурсы CPU, пока не появятся данные или не истечёт таймер. */
        int result = poll(fds, 1, 2000);

        if (result == -1) {
            /* Ошибка системного вызова poll() */
            ERR("poll");
        } else if (result == 0) {
            /* Таймаут (прошло 2 секунды без ввода):
             * заполняем все 16 байт отображённой памяти символом '-' через memset */
            printf("Время вышло!\n");
            memset(adress, '-', 16);

            /* Принудительно сбрасываем изменённую страницу памяти на диск (MS_SYNC) */
            msync(adress, 16, MS_SYNC);

            /* Так как в 16-байтном буфере adress нет завершающего нулевого байта '\0',
             * используем спецификатор %.*s с ограничением 16, чтобы printf не вышел за границы памяти */
            printf("Таймаут: %.*s\n", 16, adress);
        } else {
            /* result > 0: пришли данные со стандартного ввода. Проверяем флаг POLLIN */
            if (fds[0].revents & POLLIN) {
                char buf[16];
                /* Читаем до 16 сырых байт из stdin (включая символ перевода строки '\n') */
                ssize_t calk = read(STDIN_FILENO, buf, 16);
                if (calk > 0) {
                    /* Копируем прочитанные calk байт в начало отображённого файла (поверх старых символов)
                     * и сразу синхронизируем изменения на диск */
                    memcpy(adress, buf, calk);
                    msync(adress, 16, MS_SYNC);
                }
            }
        }
    }

    return EXIT_SUCCESS;
}