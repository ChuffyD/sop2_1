/* Включаем стандарты POSIX.1-2008 для доступа к системным вызовам fstat, mmap, msync и munmap */
#define _POSIX_C_SOURCE 200809L

#include <errno.h>     // переменная errno и коды системных ошибок
#include <fcntl.h>     // open(), fcntl(), флаги O_RDWR, O_NONBLOCK, F_GETFL, F_SETFL
#include <poll.h>      // заголовок из шаблона задания (в этой программе poll не используется)
#include <stdio.h>     // fprintf(), perror(), stderr
#include <stdlib.h>    // exit(), EXIT_SUCCESS, EXIT_FAILURE
#include <string.h>    // стандартные функции работы со строками и памятью (запасной заголовок)
#include <sys/mman.h>  // mmap(), msync(), munmap(), PROT_READ, PROT_WRITE, MAP_SHARED, MAP_FAILED
#include <sys/stat.h>  // fstat(), struct stat (для получения размера файла sb.st_size)
#include <unistd.h>    // close()

/* Макрос обработки критических ошибок системных вызовов:
 * 1. perror(source) выводит текстовое описание ошибки из errno;
 * 2. fprintf печатает имя файла (__FILE__) и номер строки (__LINE__), где произошёл сбой;
 * 3. exit(EXIT_FAILURE) немедленно завершает программу с кодом ошибки. */
#define ERR(source) (perror(source), fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), exit(EXIT_FAILURE))

/* Вспомогательная функция из шаблона для перевода дескриптора fd в неблокирующий режим
 * (в данной программе не вызывается, так как работа идёт с обычным файлом через mmap). */
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
    /* Проверяем, что передан ровно 1 аргумент — путь к существующему файлу (argc == 2) */
    if (argc != 2)
        usage(argv[0]);

    char *file_path = argv[1];
    int fd;

    /* Открываем существующий файл в режиме чтения и записи (O_RDWR).
     * ВАЖНО: флаг O_RDWR обязателен для связки PROT_READ | PROT_WRITE и MAP_SHARED в mmap().
     * Если открыть файл только на чтение (O_RDONLY), ядро отклонит mmap() с ошибкой EACCES.
     * Флаг O_CREAT здесь не указан — если файла нет, open() завершится ошибкой ENOENT. */
    if ((fd = open(file_path, O_RDWR)) == -1) {
        ERR("open");
    }

    /* Запрашиваем метаданные файла у ядра, чтобы узнать его точный размер на диске (sb.st_size) */
    struct stat sb;
    if (fstat(fd, &sb) == -1) {
        ERR("fstat");
    }

    int size = sb.st_size;

    /* Отображаем весь файл (size байт) напрямую в виртуальную память процесса:
     * NULL — ядро само выбирает свободный базовый адрес;
     * size — длина отображаемого участка (если файл пустой и size == 0, mmap вернёт EINVAL);
     * PROT_READ | PROT_WRITE — разрешаем чтение и модификацию байтов в памяти;
     * MAP_SHARED — изменения в массиве adress напрямую отражаются в файле на диске;
     * fd, 0 — дескриптор файла и смещение от его начала (0 байт). */
    char *adress;
    if ((adress = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0)) == MAP_FAILED) {
        ERR("adress");
    }

    /* Проходим по всему отображённому файлу как по обычному массиву символов в RAM
     * и заменяем каждый пробел (' ') на символ подчёркивания ('_') без вызовов read()/write() */
    for (int i = 0; i < size; i++) {
        if (adress[i] == ' ') {
            adress[i] = '_';
        }
    }

    /* Принудительно сбрасываем изменённые страницы памяти на диск (MS_SYNC — ждать завершения записи) */
    msync(adress, size, MS_SYNC);

    /* Освобождаем ресурсы: удаляем отображение из адресного пространства процесса и закрываем файл */
    munmap(adress, size);
    close(fd);

    return EXIT_SUCCESS;
}