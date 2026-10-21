#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <pthread.h>
#include <time.h>

#define CORPUS_FILE "/home/kanishk-gorana/Documents/OSP/IITB_score_gt_065.en"
#define INDEX_FILE  "/home/kanishk-gorana/Documents/OSP/line_index.txt"

#define MAX_LINE 10000
#define NUM_THREADS 2

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
   WORD MATCHING
   ============================================================ */

int contains_word(const char *sentence, const char *word)
{
    const char *delimiters =
        " \t\r\n.,!?;:\"'()[]{}<>-/_";

    char *copy = strdup(sentence);

    if (copy == NULL)
    {
        perror("strdup");
        exit(EXIT_FAILURE);
    }

    char *saveptr = NULL;

    char *token = strtok_r(
        copy,
        delimiters,
        &saveptr
    );

    while (token != NULL)
    {
        if (strcasecmp(token, word) == 0)
        {
            free(copy);
            return 1;
        }

        token = strtok_r(
            NULL,
            delimiters,
            &saveptr
        );
    }

    free(copy);

    return 0;
}

/* ============================================================
   SENTENCE INDEX
   ============================================================ */

typedef struct
{
    long en_offset;
} SentenceInfo;


/* ============================================================
   RESULT LIST
   ============================================================ */

typedef struct
{
    long *ids;
    long count;
    long capacity;
} IDList;


void init_id_list(IDList *list)
{
    list->count = 0;
    list->capacity = 1024;

    list->ids = malloc(
        list->capacity * sizeof(long)
    );

    if (list->ids == NULL)
    {
        perror("malloc");
        exit(EXIT_FAILURE);
    }
}


void add_id(IDList *list, long id)
{
    if (list->count >= list->capacity)
    {
        list->capacity *= 2;

        list->ids = realloc(
            list->ids,
            list->capacity * sizeof(long)
        );

        if (list->ids == NULL)
        {
            perror("realloc");
            exit(EXIT_FAILURE);
        }
    }

    list->ids[list->count++] = id;
}


void free_id_list(IDList *list)
{
    free(list->ids);

    list->ids = NULL;
    list->count = 0;
    list->capacity = 0;
}


/* ============================================================
   THREAD DATA
   ============================================================ */

typedef struct
{
    int thread_id;

    long start_id;
    long end_id;

    SentenceInfo *index;

    const char *word1;
    const char *word2;

    IDList results;

} ThreadData;


/* ============================================================
   THREAD SEARCH
   ============================================================ */

void *search_thread(void *arg)
{
    ThreadData *data = (ThreadData *)arg;

    FILE *fp = fopen(CORPUS_FILE, "r");

    if (fp == NULL)
    {
        perror("fopen");
        pthread_exit(NULL);
    }

    char line[MAX_LINE];

    /*
       Jump directly to the first sentence
       of this thread's partition.
    */

    if (fseek(
            fp,
            data->index[data->start_id].en_offset,
            SEEK_SET) != 0)
    {
        perror("fseek");
        fclose(fp);
        pthread_exit(NULL);
    }

    /*
       Sequentially traverse ONLY this partition.
    */

    for (long id = data->start_id;
         id <= data->end_id;
         id++)
    {
        if (fgets(line, sizeof(line), fp) == NULL)
            break;

        /*
           OR operation:
           select the sentence if either word occurs.
        */

        int has_word1 =
            contains_word(line, data->word1);

        int has_word2 =
            contains_word(line, data->word2);

        if (has_word1 || has_word2)
        {
            add_id(&data->results, id);
        }
    }

    fclose(fp);

    pthread_exit(NULL);
}


/* ============================================================
   LOAD LINE INDEX
   ============================================================ */

SentenceInfo *load_index(long *sentence_count)
{
    FILE *fp = fopen(INDEX_FILE, "r");

    if (fp == NULL)
    {
        perror("line_index.txt");
        exit(EXIT_FAILURE);
    }

    long capacity = 100000;

    SentenceInfo *index =
        malloc(capacity * sizeof(SentenceInfo));

    if (index == NULL)
    {
        perror("malloc");
        exit(EXIT_FAILURE);
    }

    char line[256];

    long count = 0;

    while (fgets(line, sizeof(line), fp))
    {
        long id;
        long en_offset;
        long hi_offset;

        if (sscanf(
                line,
                "%ld %ld %ld",
                &id,
                &en_offset,
                &hi_offset
            ) != 3)
        {
            continue;
        }

        if (count >= capacity)
        {
            capacity *= 2;

            index = realloc(
                index,
                capacity * sizeof(SentenceInfo)
            );

            if (index == NULL)
            {
                perror("realloc");
                exit(EXIT_FAILURE);
            }
        }

        index[count].en_offset = en_offset;

        count++;
    }

    fclose(fp);

    *sentence_count = count;

    return index;
}


/* ============================================================
   MAIN
   ============================================================ */

int main()
{
    const char *word1 = "government";
    const char *word2 = "policy";

    /* --------------------------------------------------------
       LOAD INDEX
       -------------------------------------------------------- */

    long sentence_count;

    SentenceInfo *index =
        load_index(&sentence_count);

    printf("\n========================================\n");
    printf(" TWO-THREAD TWO-WORD OR SEARCH\n");
    printf("========================================\n");

    printf("Word 1: %s\n", word1);
    printf("Word 2: %s\n", word2);

    printf(
        "Sentences: %ld\n",
        sentence_count
    );

    /* --------------------------------------------------------
       DIVIDE CORPUS INTO TWO PARTITIONS
       -------------------------------------------------------- */

    long middle = sentence_count / 2;

    ThreadData data[NUM_THREADS];

    /*
       Thread 0
       -----------
       First half
    */

    data[0].thread_id = 0;

    data[0].start_id = 0;
    data[0].end_id = middle - 1;

    data[0].index = index;

    data[0].word1 = word1;
    data[0].word2 = word2;

    init_id_list(&data[0].results);


    /*
       Thread 1
       -----------
       Second half
    */

    data[1].thread_id = 1;

    data[1].start_id = middle;
    data[1].end_id = sentence_count - 1;

    data[1].index = index;

    data[1].word1 = word1;
    data[1].word2 = word2;

    init_id_list(&data[1].results);


    printf("\nPartitions:\n");

    printf(
        "Thread 0: %ld -> %ld\n",
        data[0].start_id,
        data[0].end_id
    );

    printf(
        "Thread 1: %ld -> %ld\n",
        data[1].start_id,
        data[1].end_id
    );


    /* --------------------------------------------------------
       CREATE THREADS
       -------------------------------------------------------- */

    pthread_t threads[NUM_THREADS];

    double start_time = get_time_sec();

    pthread_create(
        &threads[0],
        NULL,
        search_thread,
        &data[0]
    );

    pthread_create(
        &threads[1],
        NULL,
        search_thread,
        &data[1]
    );


    /* --------------------------------------------------------
       WAIT FOR BOTH THREADS
       -------------------------------------------------------- */

    pthread_join(threads[0], NULL);
    pthread_join(threads[1], NULL);

    double end_time = get_time_sec();


    /* --------------------------------------------------------
       COMBINE PRIVATE RESULTS
       -------------------------------------------------------- */

    long total_matches =
        data[0].results.count +
        data[1].results.count;


    printf("\nResults:\n");

    printf(
        "Thread 0 matches: %ld\n",
        data[0].results.count
    );

    printf(
        "Thread 1 matches: %ld\n",
        data[1].results.count
    );

    printf(
        "Total matching sentences: %ld\n",
        total_matches
    );

    printf(
        "Total time: %.6f seconds\n",
        end_time - start_time
    );

    printf("========================================\n");


    /* --------------------------------------------------------
       CLEANUP
       -------------------------------------------------------- */

    free_id_list(&data[0].results);
    free_id_list(&data[1].results);

    free(index);

    return 0;
}