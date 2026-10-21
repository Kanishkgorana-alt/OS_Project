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

#define NUM_THREADS 2

#define WORD1 "government"
#define WORD2 "सरकार"


/* ============================================================
   TIMER
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
        size_t new_capacity;

        if (list->capacity == 0)
            new_capacity = 1024;
        else
            new_capacity = list->capacity * 2;

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

    list->ids[list->count] = id;

    list->count++;
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


/*
   Exact word matching.

   Example:

       "government"  -> MATCH
       "Government"  -> MATCH
       "governments" -> NO MATCH
*/

int contains_word(const char *sentence,
                  const char *word)
{
    size_t word_len = strlen(word);

    const char *p = sentence;

    while (*p != '\0')
    {
        /* Skip delimiters */

        while (*p != '\0' &&
               is_delimiter((unsigned char)*p))
        {
            p++;
        }

        if (*p == '\0')
            break;


        /* Beginning of token */

        const char *start = p;


        /* Find end of token */

        while (*p != '\0' &&
               !is_delimiter((unsigned char)*p))
        {
            p++;
        }


        size_t token_len =
            p - start;


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


        /*
           Expected format:

           id en_offset hi_offset
        */

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


        /* Expand array if required */

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


        /*
           Store using the actual sentence ID.
        */

        index[id].en_offset = en_offset;
        index[id].hi_offset = hi_offset;


        if (id > max_id)
            max_id = id;
    }


    fclose(fp);


    *sentence_count =
        max_id + 1;


    return index;
}


/* ============================================================
   THREAD ARGUMENTS
   ============================================================ */

typedef struct
{
    long start_id;
    long end_id;

    SentenceInfo *index;

    IDList results;

} ThreadTask;


/* ============================================================
   THREAD FUNCTION
   ============================================================ */

void *search_worker(void *arg)
{
    ThreadTask *task =
        (ThreadTask *)arg;


    /* --------------------------------------------------------
       Each thread opens its OWN files.
       -------------------------------------------------------- */

    FILE *en_fp =
        fopen(EN_FILE, "r");

    FILE *hi_fp =
        fopen(HI_FILE, "r");


    if (en_fp == NULL)
    {
        perror("fopen English file");
        pthread_exit(NULL);
    }


    if (hi_fp == NULL)
    {
        perror("fopen Hindi file");
        fclose(en_fp);
        pthread_exit(NULL);
    }


    /* --------------------------------------------------------
       Thread-local line buffers
       -------------------------------------------------------- */

    char *en_line = NULL;
    size_t en_capacity = 0;

    char *hi_line = NULL;
    size_t hi_capacity = 0;


    /* --------------------------------------------------------
       Traverse assigned sentence IDs
       -------------------------------------------------------- */

    for (long id = task->start_id;
         id <= task->end_id;
         id++)
    {
        /*
           --------------------------------------------
           Read English sentence
           --------------------------------------------
        */

        if (fseek(en_fp,
                  task->index[id].en_offset,
                  SEEK_SET) != 0)
        {
            perror("fseek English");
            continue;
        }


        ssize_t en_length =
            getline(&en_line,
                    &en_capacity,
                    en_fp);


        if (en_length == -1)
            continue;


        /*
           --------------------------------------------
           First condition:
           English contains WORD1
           --------------------------------------------
        */

        if (!contains_word(en_line,
                           WORD1))
        {
            continue;
        }


        /*
           --------------------------------------------
           Read Hindi sentence
           --------------------------------------------
        */

        if (fseek(hi_fp,
                  task->index[id].hi_offset,
                  SEEK_SET) != 0)
        {
            perror("fseek Hindi");
            continue;
        }


        ssize_t hi_length =
            getline(&hi_line,
                    &hi_capacity,
                    hi_fp);


        if (hi_length == -1)
            continue;


        /*
           --------------------------------------------
           Second condition:
           Hindi contains WORD2
           --------------------------------------------
        */

        if (contains_word(hi_line,
                          WORD2))
        {
            add_id(&task->results,
                   id);
        }
    }


    /* --------------------------------------------------------
       Cleanup
       -------------------------------------------------------- */

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
    printf("   TWO-THREAD BILINGUAL AND SEARCH\n");
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
       CREATE TWO PARTITIONS
       ======================================================== */

    long middle =
        sentence_count / 2;


    ThreadTask tasks[NUM_THREADS];

    pthread_t threads[NUM_THREADS];


    /*
       Thread 0
       IDs: 0 -> middle-1
    */

    tasks[0].start_id = 0;
    tasks[0].end_id = middle - 1;
    tasks[0].index = index;

    init_idlist(&tasks[0].results);


    /*
       Thread 1
       IDs: middle -> last
    */

    tasks[1].start_id = middle;
    tasks[1].end_id = sentence_count - 1;
    tasks[1].index = index;

    init_idlist(&tasks[1].results);


    printf("\nPartitions:\n");

    printf("Thread 0: %ld -> %ld\n",
           tasks[0].start_id,
           tasks[0].end_id);

    printf("Thread 1: %ld -> %ld\n",
           tasks[1].start_id,
           tasks[1].end_id);


    /* ========================================================
       START TIMER
       ======================================================== */

    double start_time =
        get_time_sec();


    /* ========================================================
       CREATE THREADS
       ======================================================== */

    pthread_create(
        &threads[0],
        NULL,
        search_worker,
        &tasks[0]);


    pthread_create(
        &threads[1],
        NULL,
        search_worker,
        &tasks[1]);


    /* ========================================================
       WAIT FOR THREADS
       ======================================================== */

    pthread_join(
        threads[0],
        NULL);

    pthread_join(
        threads[1],
        NULL);


    /* ========================================================
       END TIMER
       ======================================================== */

    double end_time =
        get_time_sec();


    /* ========================================================
       RESULTS
       ======================================================== */

    size_t total_matches =
        tasks[0].results.count +
        tasks[1].results.count;


    printf("\n============================================\n");
    printf("RESULTS\n");
    printf("============================================\n");

    printf("Thread 0 matches: %zu\n",
           tasks[0].results.count);

    printf("Thread 1 matches: %zu\n",
           tasks[1].results.count);

    printf("Total matching sentence pairs: %zu\n",
           total_matches);

    printf("Time: %.6f seconds\n",
           end_time - start_time);

    printf("============================================\n");


    /* ========================================================
       CLEANUP
       ======================================================== */

    free_idlist(&tasks[0].results);
    free_idlist(&tasks[1].results);

    free(index);


    return 0;
}