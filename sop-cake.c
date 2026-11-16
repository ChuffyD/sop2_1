/* Включаем стандарты POSIX.1-2008, чтобы были доступны современные системные вызовы и макросы */
#define _POSIX_C_SOURCE 200809L

#include <ctype.h>     // isdigit() — проверка, является ли символ цифрой
#include <errno.h>     // переменная errno и коды ошибок (EEXIST, EAGAIN, EWOULDBLOCK)
#include <fcntl.h>     // open() и флаги открытия файлов (O_RDWR, O_CREAT, O_EXCL, O_RDONLY, O_NONBLOCK)
#include <poll.h>      // poll(), struct pollfd и события (POLLIN, POLLHUP, POLLERR)
#include <stdio.h>     // fprintf(), printf(), perror(), stderr
#include <stdlib.h>    // exit(), EXIT_SUCCESS, EXIT_FAILURE, atoi()
#include <string.h>    // memset(), memcpy(), memmove(), memchr(), memcmp(), strchr(), strlen()
#include <sys/mman.h>  // mmap(), msync(), munmap(), PROT_READ, PROT_WRITE, MAP_SHARED, MAP_FAILED
#include <sys/stat.h>  // fstat(), struct stat, макрос S_ISFIFO()
#include <unistd.h>    // read(), write(), close(), lseek(), off_t, ssize_t

/* Макрос для обработки критических системных ошибок:
 * 1. perror(source) печатает описание последней ошибки из errno;
 * 2. fprintf выводит имя файла и номер строки, где произошёл сбой;
 * 3. exit(EXIT_FAILURE) немедленно завершает программу с кодом ошибки. */
#define ERR(source) (perror(source), fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), exit(EXIT_FAILURE))

/* Вывод подсказки по запуску при неверном количестве аргументов командной строки */
void usage(const char *const pname)
{
    fprintf(stderr, "USAGE:%s path\n", pname);
    exit(EXIT_FAILURE);
}

/* Разбор одной готовой строки формата "NN:treść" (где '\n' уже заменён на '\0')
 * и запись валидного шага в нужную 64-байтную ячейку файла рецепта. */
void process_line(int recipe_fd, const char *line)
{
    /* 1. Ищем первое двоеточие ':' в строке.
     * strchr возвращает указатель (адрес) на символ ':' или NULL, если его нет.
     * Условие colon == line отсекает строки вида ":текст" (где забыли написать номер). */
    char *colon;
    if ((colon = strchr(line, ':')) == NULL || colon == line) {
        fprintf(stderr, "Babcia się zagadała: %s\n", line);
        return;
    }

    /* 2. Вычисляем длину части ДО двоеточия (разность адресов даёт число символов).
     * Допустимы только номера 0..31 (максимум 2 символа), всё длиннее отбрасываем. */
    int len = colon - line;
    if (len > 2) {
        fprintf(stderr, "Babcia się zagadała: %s\n", line);
        return;
    }

    /* 3. Проверяем, что каждый символ до двоеточия — это цифра '0'..'9'.
     * Это отсекает пробелы (" 5:text"), минусы ("-1:text") и буквы ("1a:text").
     * Приведение (unsigned char) защищает isdigit от отрицательных значений char (например, UTF-8). */
    for (int i = 0; i < len; i++) {
        if (!isdigit((unsigned char)line[i])) {
            fprintf(stderr, "Babcia się zagadała: %s\n", line);
            return;
        }
    }

    /* 4. Переводим цифры в начале строки в целое число int.
     * atoi сама остановится, когда дойдёт до символа ':'.
     * Проверяем, что номер строки не выходит за пределы книги (0..31). */
    int step = atoi(line);
    if (step > 31) {
        fprintf(stderr, "Babcia się zagadała: %s\n", line);
        return;
    }

    /* 5. Готовим 64-байтный бланк строки для записи в файл:
     * первые 63 байта заполняем пробелами ' ', а в 64-й байт (индекс 63) ставим '\n'. */
    char record[64];
    memset(record, ' ', 63);
    record[63] = '\n';

    /* 6. Копируем текст шага (он начинается сразу после двоеточия, по адресу colon + 1).
     * Если текст длиннее 63 символов — обрезаем до 63, чтобы не затереть '\n' в конце.
     * Используем memcpy, а не strcpy, чтобы не скопировать в файл нулевой байт '\0'. */
    size_t text_len = strlen(colon + 1);
    if (text_len > 63) {
        text_len = 63;
    }
    memcpy(record, colon + 1, text_len);

    /* 7. Перемещаем файловый курсор на начало строки с номером step.
     * Так как каждая строка весит ровно 64 байта, смещение от начала файла (SEEK_SET)
     * вычисляется как step * 64. Затем перезаписываем 64 байта на месте. */
    if (lseek(recipe_fd, (off_t)step * 64, SEEK_SET) == -1) {
        ERR("lseek");
    }
    if (write(recipe_fd, record, 64) == -1) {
        ERR("write");
    }

    /* %02d выводит номер шага в 2 цифры, добавляя ведущий ноль для 0..9 (например, 05) */
    printf("Krok %02d zapisany\n", step);
}

int main(int argc, char *argv[])
{
    /* Проверяем количество аргументов: имя программы + файл рецепта + от 1 до 5 башен = 3..7 */
    if (argc < 3 || argc > 7) {
        usage(argv[0]);
    }

    /* ==================== ЭТАП 1: ПОДГОТОВКА ФАЙЛА РЕЦЕПТА ==================== */
    char *file_path = argv[1];

    /* Пытаемся атомарно создать новый файл (O_CREAT | O_EXCL) для чтения и записи (O_RDWR)
     * с правами rw-r--r-- (0644). Если файл уже есть, O_EXCL заставит open вернуть -1 и errno = EEXIST. */
    int fd = open(file_path, O_RDWR | O_CREAT | O_EXCL, 0644);
    if (fd != -1) {
        /* Ветка 1: файла не было, мы его создали. Заполняем 32 пустыми строками по 64 байта = 2048 байт. */
        char line[64];
        memset(line, ' ', 63);
        line[63] = '\n';
        for (int i = 0; i < 32; i++) {
            if (write(fd, line, 64) == -1) {
                ERR("write");
            }
        }
    } else if (errno == EEXIST) {
        /* Ветка 2: файл уже существует. Открываем его обычным вызовом O_RDWR. */
        if ((fd = open(file_path, O_RDWR)) == -1) {
            ERR("open");
        }
        /* Переводим курсор в конец файла (SEEK_END), чтобы узнать его полный размер в байтах. */
        off_t size;
        if ((size = lseek(fd, 0, SEEK_END)) == -1) {
            ERR("lseek");
        }
        /* Если размер не равен ровно 2048 байтам — это не книга рецептов. */
        if (size != 2048) {
            fprintf(stderr, "Ta ksiega to nie przepis!\n");
            close(fd);
            exit(EXIT_FAILURE);
        }
    } else {
        /* Ветка 3: любая другая системная ошибка при открытии (например, нет прав на каталог). */
        ERR("open");
    }

    /* ==================== ЭТАПЫ 2 и 4: ОТКРЫТИЕ БАШЕН (FIFO) ==================== */
    int count = argc - 2;      // Сколько башен передано (от 1 до 5)
    struct pollfd pfds[5];     // Массив структур для отслеживания дескрипторов в poll()

    for (int i = 0; i < count; i++) {
        int tow_fd;
        /* Открываем башню на чтение (O_RDONLY) с флагом O_NONBLOCK.
         * O_NONBLOCK критически важен: без него open() заблокирует программу и будет ждать,
         * пока писатель откроет эту трубу, мешая нам открыть остальные башни. */
        if ((tow_fd = open(argv[i + 2], O_RDONLY | O_NONBLOCK)) == -1) {
            fprintf(stderr, "Nie ma babci na wieży %s\n", argv[i + 2]);
            close(fd);
            for (int j = 0; j < i; j++) {
                close(pfds[j].fd); // Закрываем все башни, которые уже успели открыть ранее
            }
            exit(EXIT_FAILURE);
        }

        /* Получаем метаданные открытого дескриптора через fstat() и проверяем макросом S_ISFIFO,
         * что это действительно именованный канал (FIFO), а не обычный файл или каталог. */
        struct stat sb;
        if (fstat(tow_fd, &sb) == -1) {
            ERR("fstat");
        }
        if (!S_ISFIFO(sb.st_mode)) {
            fprintf(stderr, "Nie ma babci na wieży %s\n", argv[i + 2]);
            close(fd);
            close(tow_fd);
            for (int j = 0; j < i; j++) {
                close(pfds[j].fd);
            }
            exit(EXIT_FAILURE);
        }

        /* Регистрируем валидный дескриптор башни в массиве для poll() */
        pfds[i].fd = tow_fd;
        pfds[i].events = POLLIN; // Просим ядро сообщать, когда в канале появятся данные для чтения
    }

    /* ============ ЭТАПЫ 3 и 4: ПАРАЛЛЕЛЬНОЕ ЧТЕНИЕ И СБОРКА СТРОК ============ */
    char buf[256];             // Временный буфер для одного вызова read()
    char acc[5][1024];         // Личная «копилка» байт для каждой из 5 башен (чтобы фразы не мешались)
    size_t acc_len[5] = {0};   // Количество накопленных байт в каждой копилке
    int active = count;        // Счётчик ещё не закрытых (активных) башен

    /* Крутим цикл, пока хотя бы одна бабушка ещё не закончила диктовать */
    while (active > 0) {
        /* Ждём события на любом из каналов (таймаут -1 = ждать бесконечно долго, не грузя процессор) */
        if (poll(pfds, count, -1) == -1) {
            ERR("poll");
        }

        for (int i = 0; i < count; i++) {
            /* Если эту башню мы уже закрыли ранее (fd == -1), пропускаем её */
            if (pfds[i].fd == -1) {
                continue;
            }

            /* Проверяем маску событий revents, которую вернуло ядро:
             * POLLIN  — есть новые данные для чтения;
             * POLLHUP — бабушка закрыла свой конец трубы (Hang Up / EOF);
             * POLLERR — ошибка канала. */
            if (pfds[i].revents & (POLLIN | POLLHUP | POLLERR)) {
                ssize_t n = read(pfds[i].fd, buf, sizeof(buf));

                if (n > 0) {
                    /* Пришли n новых байт. Защита от переполнения буфера копилки: */
                    if (acc_len[i] + (size_t)n >= sizeof(acc[i])) {
                        acc_len[i] = 0;
                    }
                    /* Дописываем свежие байты в конец копилки i-й башни */
                    memcpy(acc[i] + acc_len[i], buf, n);
                    acc_len[i] += n;

                    /* Вырезаем все целые строки, оканчивающиеся на '\n'.
                     * Используем memchr (а не strchr), так как в сырых байтах ещё нет '\0'. */
                    char *nl;
                    while ((nl = memchr(acc[i], '\n', acc_len[i])) != NULL) {
                        size_t line_len = nl - acc[i]; // Длина строки без '\n'
                        *nl = '\0';                    // Заменяем '\n' на '\0', получая обычную C-строку
                        process_line(fd, acc[i]);      // Разбираем строку и пишем шаг в файл

                        /* Сдвигаем оставшийся недописанный хвост после '\n' в начало копилки.
                         * memmove безопасен при перекрытии областей памяти внутри одного массива. */
                        size_t consumed = line_len + 1;
                        memmove(acc[i], acc[i] + consumed, acc_len[i] - consumed);
                        acc_len[i] -= consumed;
                    }
                } else if (n == 0) {
                    /* n == 0 означает EOF: бабушка додиктовала и закрыла канал.
                     * Закрываем дескриптор, ставим fd = -1 (poll его больше не проверяет) и уменьшаем active. */
                    close(pfds[i].fd);
                    pfds[i].fd = -1;
                    active--;
                } else {
                    /* При O_NONBLOCK отсутствие данных возвращает -1 с кодом EAGAIN/EWOULDBLOCK — это норма. */
                    if (errno == EAGAIN || errno == EWOULDBLOCK) {
                        continue;
                    }
                    ERR("read");
                }
            }
        }
    }

    /* ================ ЭТАП 5: ЗАМЕНА В ПАМЯТИ ЧЕРЕЗ MMAP ================ */
    /* Отображаем весь 2048-байтный файл рецепта напрямую в виртуальную память процесса.
     * PROT_READ | PROT_WRITE — разрешаем чтение и запись;
     * MAP_SHARED — все изменения в массиве map автоматически связаны с файлом на диске. */
    char *map = mmap(NULL, 2048, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (map == MAP_FAILED) {
        ERR("mmap");
    }

    /* Проходим по всему отображённому файлу и меняем "raw~berries" (11 байт) на "raspberries" (11 байт).
     * Используем memcmp/memcpy вместо strstr, так как в файле нет завершающего '\0'. */
    for (size_t i = 0; i <= 2048 - 11; i++) {
        if (memcmp(map + i, "raw~berries", 11) == 0) {
            memcpy(map + i, "raspberries", 11);
        }
    }

    /* Принудительно сбрасываем изменённые страницы памяти на диск (MS_SYNC) и освобождаем отображение */
    if (msync(map, 2048, MS_SYNC) == -1) {
        ERR("msync");
    }
    if (munmap(map, 2048) == -1) {
        ERR("munmap");
    }

    /* Закрываем файл рецепта и успешно завершаем программу */
    close(fd);
    return EXIT_SUCCESS;
}