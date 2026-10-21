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

    char *token = strtok(copy, delimiters);

    while (token != NULL)
    {
        if (strcasecmp(token, word) == 0)
        {
            free(copy);
            return 1;
        }

        token = strtok(NULL, delimiters);
    }

    free(copy);

    return 0;
}

/* ============================================================
   THREAD DATA
   ============================================================ */

typedef struct
{
    int thread_id;

    long start_id;
    long end_id;

    const char *word1;
    const char *word2;

    long match_count;

} ThreadData;

/* ============================================================
   THREAD FUNCTION
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
       Open the corpus and jump directly to the
       beginning of this thread's partition.

       For this simple version, we use the fact that
       each partition is found by sequential traversal
       after the initial positioning.
    */

    /*
       Since arbitrary sentence byte offsets require
       line_index.txt, this version calculates the
       starting position by scanning until start_id.

       For the benchmark version, we should use your
       existing line_index.txt to make this O(1).
    */

    for (long id = 0; id <= data->start_id; id++)
    {
        if (fgets(line, sizeof(line), fp) == NULL)
        {
            fclose(fp);
            pthread_exit(NULL);
        }
    }

    /*
       Now search only this thread's partition.
    */

    for (long id = data->start_id;
         id <= data->end_id;
         id++)
    {
        /*
           Check BOTH words in the same sentence.
        */

        int has_word1 =
            contains_word(line, data->word1);

        int has_word2 =
            contains_word(line, data->word2);

        if (has_word1 && has_word2)
        {
            data->match_count++;
        }

        /*
           Read next sentence.
        */

        if (id < data->end_id)
        {
            if (fgets(line, sizeof(line), fp) == NULL)
                break;
        }
    }

    fclose(fp);

    pthread_exit(NULL);
}

/* ============================================================
   MAIN
   ============================================================ */

int main()
{
    const char *word1 = "government";
    const char *word2 = "policy";

    /*
       Total number of sentence pairs.
    */

    long total_sentences = 1030875;

    /*
       Divide corpus into two equal partitions.
    */

    long middle = total_sentences / 2;

    ThreadData data[NUM_THREADS];

    pthread_t threads[NUM_THREADS];

    /* --------------------------------------------------------
       THREAD 0
       -------------------------------------------------------- */

    data[0].thread_id = 0;

    data[0].start_id = 0;
    data[0].end_id = middle - 1;

    data[0].word1 = word1;
    data[0].word2 = word2;

    data[0].match_count = 0;

    /* --------------------------------------------------------
       THREAD 1
       -------------------------------------------------------- */

    data[1].thread_id = 1;

    data[1].start_id = middle;
    data[1].end_id = total_sentences - 1;

    data[1].word1 = word1;
    data[1].word2 = word2;

    data[1].match_count = 0;

    printf("\n========================================\n");
    printf(" TWO-THREAD TWO-WORD SEARCH\n");
    printf("========================================\n");

    printf("Word 1: %s\n", word1);
    printf("Word 2: %s\n", word2);

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

    /* ========================================================
       START PARALLEL SEARCH
       ======================================================== */

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

    /* ========================================================
       WAIT FOR BOTH THREADS
       ======================================================== */

    pthread_join(threads[0], NULL);
    pthread_join(threads[1], NULL);

    double end_time = get_time_sec();

    /* ========================================================
       COMBINE RESULTS
       ======================================================== */

    long total_matches =
        data[0].match_count +
        data[1].match_count;

    printf("\nResults:\n");

    printf(
        "Thread 0 matches: %ld\n",
        data[0].match_count
    );

    printf(
        "Thread 1 matches: %ld\n",
        data[1].match_count
    );

    printf(
        "Total matching sentences: %ld\n",
        total_matches
    );

    printf(
        "\nTotal time: %.6f seconds\n",
        end_time - start_time
    );

    printf("========================================\n");

    return 0;
}