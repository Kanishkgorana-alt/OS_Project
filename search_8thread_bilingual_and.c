#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <pthread.h>
#include <ctype.h>
#include <time.h>

#define EN_FILE "/home/kanishk-gorana/Documents/OSP/IITB_score_gt_065.en"
#define HI_FILE "/home/kanishk-gorana/Documents/OSP/IITB_score_gt_065.hi"
#define INDEX_FILE "/home/kanishk-gorana/Documents/OSP/line_index.txt"
#define PARTITION_FILE "/home/kanishk-gorana/Documents/OSP/en_partitions.txt"

#define NUM_THREADS 8

#define WORD1 "government"
#define WORD2 "सरकार"


/* ============================================================
   TIMER code
   ============================================================ */

double get_time_sec()
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);

    return ts.tv_sec + ts.tv_nsec / 1e9;
}


/* ============================================================
   SENTENCE INDEX
   ============================================================ */

typedef struct
{
    long en_offset;
    long hi_offset;

} SentenceInfo;


/* ============================================================
   RESULT LIST
   ============================================================ */

typedef struct
{
    long *ids;

    size_t count;
    size_t capacity;

} IDList;


void init_idlist(IDList *list)
{
    list->ids = NULL;
    list->count = 0;
    list->capacity = 0;
}


void add_id(IDList *list, long id)
{
    if (list->count == list->capacity)
    {
        size_t new_capacity =
            (list->capacity == 0)
            ? 1024
            : list->capacity * 2;

        long *new_ids =
            realloc(list->ids,
                    new_capacity * sizeof(long));

        if (new_ids == NULL)
        {
            perror("realloc");
            exit(EXIT_FAILURE);
        }

        list->ids = new_ids;
        list->capacity = new_capacity;
    }

    list->ids[list->count++] = id;
}


void free_idlist(IDList *list)
{
    free(list->ids);

    list->ids = NULL;
    list->count = 0;
    list->capacity = 0;
}


/* ============================================================
   WORD MATCHING
   ============================================================ */

int is_delimiter(unsigned char c)
{
    return
        isspace(c) ||
        c == '.' ||
        c == ',' ||
        c == '!' ||
        c == '?' ||
        c == ';' ||
        c == ':' ||
        c == '"' ||
        c == '\'' ||
        c == '(' ||
        c == ')' ||
        c == '[' ||
        c == ']' ||
        c == '{' ||
        c == '}' ||
        c == '<' ||
        c == '>' ||
        c == '-' ||
        c == '/' ||
        c == '_';
}


int contains_word(const char *sentence,
                  const char *word)
{
    size_t word_len = strlen(word);

    const char *p = sentence;

    while (*p != '\0')
    {
        while (*p != '\0' &&
               is_delimiter((unsigned char)*p))
        {
            p++;
        }

        if (*p == '\0')
            break;

        const char *start = p;

        while (*p != '\0' &&
               !is_delimiter((unsigned char)*p))
        {
            p++;
        }

        size_t token_len = p - start;

        if (token_len == word_len)
        {
            if (strncasecmp(start,
                            word,
                            word_len) == 0)
            {
                return 1;
            }
        }
    }

    return 0;
}


/* ============================================================
   LOAD LINE INDEX
   ============================================================ */

SentenceInfo *load_index(
    const char *filename,
    long *sentence_count)
{
    FILE *fp = fopen(filename, "r");

    if (fp == NULL)
    {
        perror("fopen line_index.txt");
        exit(EXIT_FAILURE);
    }

    size_t capacity = 1024;

    SentenceInfo *index =
        calloc(capacity,
               sizeof(SentenceInfo));

    if (index == NULL)
    {
        perror("calloc");
        exit(EXIT_FAILURE);
    }

    long max_id = -1;

    char line[512];

    while (fgets(line,
                 sizeof(line),
                 fp))
    {
        long id;
        long en_offset;
        long hi_offset;

        if (sscanf(line,
                   "%ld %ld %ld",
                   &id,
                   &en_offset,
                   &hi_offset) != 3)
        {
            continue;
        }

        if (id < 0)
            continue;

        while ((size_t)id >= capacity)
        {
            capacity *= 2;

            SentenceInfo *new_index =
                realloc(index,
                        capacity *
                        sizeof(SentenceInfo));

            if (new_index == NULL)
            {
                perror("realloc");
                exit(EXIT_FAILURE);
            }

            index = new_index;
        }

        index[id].en_offset = en_offset;
        index[id].hi_offset = hi_offset;

        if (id > max_id)
            max_id = id;
    }

    fclose(fp);

    *sentence_count = max_id + 1;

    return index;
}


/* ============================================================
   BLOCK STRUCTURE
   ============================================================ */

typedef struct
{
    long start_id;
    long end_id;

} Block;


/* ============================================================
   LOAD 8 BLOCKS
   ============================================================ */

int load_blocks(
    const char *filename,
    Block blocks[],
    int max_blocks)
{
    FILE *fp = fopen(filename, "r");

    if (fp == NULL)
    {
        perror("fopen en_partitions.txt");
        exit(EXIT_FAILURE);
    }

    char line[512];

    int count = 0;

    while (fgets(line,
                 sizeof(line),
                 fp))
    {
        long block_id;
        long start_id;
        long end_id;
        long sentence_count;

        /*
           Expected partition format:

           block start end sentence_count
        */

        if (sscanf(line,
                   "%ld %ld %ld %ld",
                   &block_id,
                   &start_id,
                   &end_id,
                   &sentence_count) == 4)
        {
            if (block_id >= 0 &&
                block_id < max_blocks)
            {
                blocks[block_id].start_id =
                    start_id;

                blocks[block_id].end_id =
                    end_id;

                count++;
            }
        }
    }

    fclose(fp);

    return count;
}


/* ============================================================
   THREAD ARGUMENTS
   ============================================================ */

typedef struct
{
    int thread_id;

    long start_id;
    long end_id;

    SentenceInfo *index;

    IDList results;

} ThreadTask;


/* ============================================================
   WORKER
   ============================================================ */

void *search_worker(void *arg)
{
    ThreadTask *task =
        (ThreadTask *)arg;


    /*
       Each thread gets its own FILE handles.
    */

    FILE *en_fp =
        fopen(EN_FILE, "r");

    FILE *hi_fp =
        fopen(HI_FILE, "r");


    if (en_fp == NULL)
    {
        perror("fopen English");
        pthread_exit(NULL);
    }


    if (hi_fp == NULL)
    {
        perror("fopen Hindi");
        fclose(en_fp);
        pthread_exit(NULL);
    }


    /*
       Thread-local buffers.
    */

    char *en_line = NULL;
    size_t en_capacity = 0;

    char *hi_line = NULL;
    size_t hi_capacity = 0;


    /*
       Traverse this thread's block.
    */

    for (long id = task->start_id;
         id <= task->end_id;
         id++)
    {
        /* ----------------------------------------------------
           English lookup
           ---------------------------------------------------- */

        if (fseek(en_fp,
                  task->index[id].en_offset,
                  SEEK_SET) != 0)
        {
            continue;
        }


        ssize_t en_length =
            getline(&en_line,
                    &en_capacity,
                    en_fp);


        if (en_length == -1)
            continue;


        /*
           First AND condition.
        */

        if (!contains_word(en_line,
                           WORD1))
        {
            continue;
        }


        /* ----------------------------------------------------
           Hindi lookup
           ---------------------------------------------------- */

        if (fseek(hi_fp,
                  task->index[id].hi_offset,
                  SEEK_SET) != 0)
        {
            continue;
        }


        ssize_t hi_length =
            getline(&hi_line,
                    &hi_capacity,
                    hi_fp);


        if (hi_length == -1)
            continue;


        /*
           Second AND condition.
        */

        if (contains_word(hi_line,
                          WORD2))
        {
            add_id(&task->results,
                   id);
        }
    }


    free(en_line);
    free(hi_line);

    fclose(en_fp);
    fclose(hi_fp);

    pthread_exit(NULL);
}


/* ============================================================
   MAIN
   ============================================================ */

int main()
{
    printf("============================================\n");
    printf("      8-THREAD BILINGUAL AND SEARCH\n");
    printf("============================================\n");

    printf("English word : %s\n", WORD1);
    printf("Hindi word   : %s\n", WORD2);


    /* ========================================================
       LOAD INDEX
       ======================================================== */

    long sentence_count;

    SentenceInfo *index =
        load_index(INDEX_FILE,
                   &sentence_count);


    printf("Sentences    : %ld\n",
           sentence_count);


    /* ========================================================
       LOAD 8 BLOCKS
       ======================================================== */

    Block blocks[NUM_THREADS];

    int block_count =
        load_blocks(PARTITION_FILE,
                    blocks,
                    NUM_THREADS);


    if (block_count != NUM_THREADS)
    {
        fprintf(stderr,
                "Expected %d blocks, found %d\n",
                NUM_THREADS,
                block_count);

        free(index);

        return EXIT_FAILURE;
    }


    printf("\nBlocks:\n");

    for (int i = 0;
         i < NUM_THREADS;
         i++)
    {
        printf("Block %d: %ld -> %ld\n",
               i,
               blocks[i].start_id,
               blocks[i].end_id);
    }


    /* ========================================================
       CREATE THREADS
       ======================================================== */

    pthread_t threads[NUM_THREADS];

    ThreadTask tasks[NUM_THREADS];


    for (int i = 0;
         i < NUM_THREADS;
         i++)
    {
        tasks[i].thread_id = i;

        tasks[i].start_id =
            blocks[i].start_id;

        tasks[i].end_id =
            blocks[i].end_id;

        tasks[i].index = index;

        init_idlist(
            &tasks[i].results);
    }


    /* ========================================================
       TIMER
       ======================================================== */

    double start_time =
        get_time_sec();


    /* ========================================================
       START 8 THREADS
       ======================================================== */

    for (int i = 0;
         i < NUM_THREADS;
         i++)
    {
        pthread_create(
            &threads[i],
            NULL,
            search_worker,
            &tasks[i]);
    }


    /* ========================================================
       JOIN 8 THREADS
       ======================================================== */

    for (int i = 0;
         i < NUM_THREADS;
         i++)
    {
        pthread_join(
            threads[i],
            NULL);
    }


    /* ========================================================
       TIMER END
       ======================================================== */

    double end_time =
        get_time_sec();


    /* ========================================================
       RESULTS
       ======================================================== */

    size_t total_matches = 0;


    printf("\n============================================\n");
    printf("RESULTS\n");
    printf("============================================\n");


    for (int i = 0;
         i < NUM_THREADS;
         i++)
    {
        printf("Thread %d matches: %zu\n",
               i,
               tasks[i].results.count);

        total_matches +=
            tasks[i].results.count;
    }


    printf("--------------------------------------------\n");

    printf("Total matching sentence pairs: %zu\n",
           total_matches);

    printf("Time: %.6f seconds\n",
           end_time - start_time);

    printf("============================================\n");


    /* ========================================================
       CLEANUP
       ======================================================== */

    for (int i = 0;
         i < NUM_THREADS;
         i++)
    {
        free_idlist(
            &tasks[i].results);
    }

    free(index);


    return 0;
}