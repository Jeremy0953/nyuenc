#include <stdio.h>       // stderr, stdout, perror, fprintf, fwrite
#include <stdlib.h>      // exit, EXIT_FAILURE, EXIT_SUCCESS, malloc
#include <pthread.h>     // pthread_t, pthread_create, phthread_exit, pthread_join
#include <fcntl.h>       // sysconf, open, O_*
#include <unistd.h>      // _SC_*
#include <semaphore.h>   // sem_t, sem_init, sem_wait, sem_post
#include <sys/sysinfo.h> // get_nprocs
#include <sys/stat.h>    // stat, fstat
#include <sys/mman.h>    // mmap
#include <string.h>      // memset
#include <unistd.h>

#define handle_error(msg)   \
    do                      \
    {                       \
        perror(msg);        \
        exit(EXIT_FAILURE); \
    } while (0)

/* Global object */

// Argument object for producer thread
typedef struct _arg
{
    int argc;
    char **argv;
} arg_t;

typedef struct {
    int jFlag;     // 标记是否有-j选项
    int jValue;    // -j选项的值
    int fileCount; // 文件数量
    char **files;  // 文件名数组
} CmdOptions;
// Page object for munmap
typedef struct _page
{
    char *addr;
    long size;
} page_t;

// Work produced by producer
typedef struct _work
{
    char *addr;
    long pagesz;
    long pagenm;
    long filenm;

    struct _work *next;
} work_t;

// Result after a work consumed by consumer
typedef struct _rle
{
    char c;
    int count;

    struct _rle *next;
} result_t;

/* Global variables */
long nprocs; // Number of processes
long nfiles; // Number of files
long pagesz; // Page size
long pagenm; // Page number #
long filenm; // File number #
static int done = 0;
static int encode_done = 0;
static int curr_page = 0;
static int *npage_onfile;

static work_t *works, *work_head, *work_tail;
static result_t *results, *result_tail, *result_curr;
static sem_t mutex, filled, page;
static sem_t *order;
static sem_t result_sem;

/* Global functions */
void *
producer(void *args);
void *consumer(void *args);
void wenqueue(work_t work);
work_t *wdequeue();
result_t *compress(work_t work);
void renqueue(result_t *result);
void singleThreadProcess(arg_t *args);
void *collectResult(void *args);

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        fprintf(stderr, "pzip: file1 [file2 ...]\n");
        exit(EXIT_FAILURE);
    }
    CmdOptions opts = {0, 0, 0, NULL};
    int opt;
    arg_t *args = malloc(sizeof(arg_t));
    while ((opt = getopt(argc, argv, "j:")) != -1) {
        switch (opt) {
            case 'j':
                opts.jFlag = 1;
                opts.jValue = atoi(optarg);
                break;
            case '?':
                fprintf(stderr, "Usage: %s [-j value] file [file...]\n", argv[0]);
                exit(EXIT_FAILURE);
        }
    }
    if (args == NULL)
        handle_error("Malloc args");
    opts.fileCount = argc - optind;
    opts.files = argv + optind;
    args->argc = opts.fileCount;
    args->argv = opts.files;

    if (opts.jFlag) {
        nprocs = opts.jValue;
    } else {
        sem_init(&result_sem, 0, 1);
        singleThreadProcess(args);
        sem_destroy(&result_sem);
        return 0;
    }         
    nfiles = opts.fileCount;               
    pagesz = sysconf(_SC_PAGE_SIZE); // Let the system decide how big a page is
    order = malloc(sizeof(sem_t) * nprocs);
    npage_onfile = malloc(sizeof(int) * nfiles);

    memset(npage_onfile, 0, sizeof(int) * nfiles);
    sem_init(&mutex, 0, 1);
    sem_init(&filled, 0, 0);
    sem_init(&page, 0, 0);
    sem_init(&result_sem, 0, 1);

    pthread_t pid, cid[nprocs], collector_pid;

    pthread_create(&pid, NULL, producer, (void *)args);

    for (int i = 0; i < nprocs; i++)
    {
        pthread_create(&cid[i], NULL, consumer, (void *)args);
        sem_init(&order[i], 0, i ? 0 : 1);
    }

    pthread_create(&collector_pid, NULL, collectResult, (void *)args);

    for (int i = 0; i < nprocs; i++)
    {
        pthread_join(cid[i], NULL);
    }
    encode_done = 1;
    pthread_join(pid, NULL);
    pthread_join(collector_pid, NULL);


    sem_destroy(&filled);
    sem_destroy(&mutex);
    sem_destroy(&page);
    sem_destroy(&result_sem);
    for (int i = 0; i < nprocs; i++)
    {
        sem_destroy(&order[i]);
    }

    for (result_t *curr = results; curr != NULL; curr = results)
    {
        results = results->next;
        free(curr);
        curr = NULL;
    }

    for (work_t *curr = works; curr != NULL; curr = works)
    {
        munmap(curr->addr, curr->pagesz);

        works = works->next;
        free(curr);
        curr = NULL;
    }

    free(order);
    free(npage_onfile);

    return 0;
}

void wenqueue(work_t work)
{
    if (works == NULL)
    {
        works = malloc(sizeof(work_t));

        if (works == NULL)
        {
            handle_error("malloc work");
            sem_post(&mutex);
        }

        works->addr = work.addr;
        works->filenm = work.filenm;
        works->pagenm = work.pagenm;
        works->pagesz = work.pagesz;

        works->next = NULL;
        work_head = works;
        work_tail = works;
    }
    else
    {
        work_tail->next = malloc(sizeof(work_t));

        if (work_tail->next == NULL)
        {
            handle_error("malloc work");
            sem_post(&mutex);
        }

        work_tail->next->addr = work.addr;
        work_tail->next->filenm = work.filenm;
        work_tail->next->pagenm = work.pagenm;
        work_tail->next->pagesz = work.pagesz;

        work_tail->next->next = NULL;
        work_tail = work_tail->next;
    }
}

void *producer(void *args)
{
    arg_t *arg = (arg_t *)args;
    char **fnames = arg->argv;

    for (int i = 0; i < arg->argc; i++)
    {
        int fd = open(fnames[i], O_RDONLY);

        if (fd == -1)
            handle_error("open error");

        struct stat sb;

        if (fstat(fd, &sb) == -1)
            handle_error("fstat error");

        if (sb.st_size == 0)
            continue;

        int p4f = sb.st_size / pagesz;

        if ((double)sb.st_size / pagesz > p4f)
            p4f++;

        int offset = 0;
        npage_onfile[i] = p4f;
        char *addr = mmap(NULL, sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0);

        for (int j = 0; j < p4f; j++)
        {
            // it should be less than or equal to the default page size
            int curr_pagesz = (j < p4f - 1) ? pagesz : sb.st_size - ((p4f - 1) * pagesz);
            offset += curr_pagesz;

            work_t work;
            work.addr = addr;
            work.filenm = i;
            work.pagenm = j;
            work.pagesz = curr_pagesz;
            work.next = NULL;

            sem_wait(&mutex);
            wenqueue(work);
            sem_post(&mutex);
            sem_post(&filled);

            addr += curr_pagesz;
        }

        close(fd);
    }

    done = 1;
    for (int i = 0; i < nprocs; i++)
    {
        sem_post(&filled);
    }

    pthread_exit(NULL);
}

work_t *wdequeue()
{
    if (work_head == NULL)
        return NULL;

    work_t *tmp = work_head;
    work_head = work_head->next;

    return tmp;
}

result_t *compress(work_t work)
{
    result_t *result = malloc(sizeof(result_t));
    if (result == NULL)
        handle_error("malloc result");
    result_t *tmp = result;

    int count = 0;
    char c, last;

    for (int i = 0; i < work.pagesz; i++)
    {
        c = work.addr[i];
        if (count && last != c)
        {
            tmp->c = last;
            tmp->count = count;

            tmp->next = malloc(sizeof(result_t));
            tmp = tmp->next;
            count = 0;
        }

        last = c;
        count++;
    }

    if (count)
    {
        tmp->c = last;
        tmp->count = count;
        tmp->next = NULL;
    }

    return result;
}

void renqueue(result_t *result)
{
    sem_wait(&result_sem);
    if (results == NULL)
    {
        results = result;
    }
    else
    {
        result_t *tmp;
        if (result_tail->c == result->c)
        {
            result_tail->count += result->count;
            tmp = result;
            result = result->next;
            free(tmp);
        }
        result_tail->next = result;
    }

    result_t *curr = result;
    for (; curr->next != NULL; curr = curr->next)
    {
    }

    result_tail = curr;
    sem_post(&result_sem);
}

void *consumer(void *args)
{
    work_t *work;
    while (!done || work_head != NULL)
    {
        sem_wait(&filled);
        sem_wait(&mutex);
        if (work_head == work_tail && !done)
        {
            sem_post(&mutex);
            continue;
        }
        else if (work_head == NULL)
        {
            sem_post(&mutex);
            return NULL;
        }
        else
        {
            work = work_head;
            work_head = work_head->next;
            sem_post(&mutex);
        }

        result_t *result = compress(*work);

        if (work->filenm == 0 && work->pagenm == 0)
        {
            sem_wait(&order[0]);
            renqueue(result);

            if (work->pagenm == npage_onfile[work->filenm] - 1)
            {
                sem_post(&order[0]);
                curr_page++;
            }
            else
                sem_post(&order[1]);

            sem_post(&page);
        }
        else
        {
            while (1)
            {
                sem_wait(&page);

                if (curr_page != work->filenm)
                {
                    sem_post(&page);
                    continue;
                }
                if (curr_page == nfiles)
                {
                    sem_post(&page);
                    return NULL;
                }
                sem_post(&page);
                sem_wait(&order[work->pagenm % nprocs]);

                sem_wait(&page);
                renqueue(result);
                if (work->filenm == curr_page && work->pagenm < npage_onfile[work->filenm] - 1)
                {
                    sem_post(&order[(work->pagenm + 1) % nprocs]);
                }
                else if (work->filenm == curr_page && work->pagenm == npage_onfile[work->filenm] - 1)
                {
                    sem_post(&order[0]);
                    curr_page++;
                }
                sem_post(&page);
                break;
            }
        }
    }

    return NULL;
}
void singleThreadProcess(arg_t *args) {
    char **fnames = args->argv;
    for (int i = 0; i < args->argc; i++) {
        int fd = open(fnames[i], O_RDONLY);
        if (fd == -1)
            handle_error("open error");

        struct stat sb;
        if (fstat(fd, &sb) == -1)
            handle_error("fstat error");

        if (sb.st_size == 0)
            continue;

        char *addr = mmap(NULL, sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
        if (addr == MAP_FAILED)
            handle_error("mmap failed");

        work_t work;
        work.addr = addr;
        work.pagesz = sb.st_size;
        work.filenm = i;
        work.pagenm = 0; // 仅一个页面的简化处理
        work.next = NULL;

        result_t *result = compress(work);
        renqueue(result);

        munmap(addr, sb.st_size);
        close(fd);
    }

    // 输出结果
    for (result_t *curr = results; curr != NULL; curr = curr->next) {
        fwrite((char *)&(curr->c), sizeof(char), 1, stdout);
        fwrite((int *)&(curr->count), sizeof(char), 1, stdout);
    }

    // 清理结果链表
    while (results != NULL) {
        result_t *temp = results;
        results = results->next;
        free(temp);
    }
}

void *collectResult(void *args) {
    while(!encode_done) {
        sem_wait(&result_sem);
        if(results != NULL) {
            if (result_curr == NULL) {
                result_curr = results;
            }
            while (result_curr != result_tail)
            {
                fwrite((char *)&(result_curr->c), sizeof(char), 1, stdout);
                fwrite((char *)&(result_curr->count), sizeof(char), 1, stdout);
                result_curr = result_curr->next;
            }
        }
        sem_post(&result_sem);
    }
    fwrite((char *)&(result_tail->c), sizeof(char), 1, stdout);
    fwrite((char *)&(result_tail->count), sizeof(char), 1, stdout);
    return NULL;
}