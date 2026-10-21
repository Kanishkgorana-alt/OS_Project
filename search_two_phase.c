#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <time.h>

#define CORPUS_FILE "/home/kanishk-gorana/Documents/OSP/IITB_score_gt_065.en"
#define INDEX_FILE  "/home/kanishk-gorana/Documents/OSP/line_index.txt"

#define MAX_LINE 10000

/* ---------------------------------------------------------
   TIMER
   --------------------------------------------------------- */

double get_time_sec()
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);

    return ts.tv_sec + ts.tv_nsec / 1e9;
}

/* ---------------------------------------------------------
   WORD MATCHING
   --------------------------------------------------------- */

int contains_word(const char *sentence, const char *word)
{
    const char *delimiters =
        " \t\r\n.,!?;:\"'()[]{}<>-/_";

    char *copy = strdup(sentence);

    if (copy == NULL)
        return 0;

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

/* ---------------------------------------------------------
   SENTENCE OFFSET
   --------------------------------------------------------- */

typedef struct
{
    long en_offset;
} SentenceInfo;

/* ---------------------------------------------------------
   RESULT ARRAY
   --------------------------------------------------------- */

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

/* ---------------------------------------------------------
   THREAD DATA
   --------------------------------------------------------- */

typedef struct
{
    int thread_id;

    const char *word;

    long start_id;
    long end_id;

    SentenceInfo *index;

    IDList phase1_ids;
    IDList phase2_ids;

} ThreadData;


/* ---------------------------------------------------------
   PHASE 1
   ---------------------------------------------------------

   Thread 0:
       searches W1 from START -> middle

   Thread 1:
       searches W2 from END -> middle

   Each thread scans ONLY its own half.
   --------------------------------------------------------- */

void *phase1_search(void *arg)
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
       One seek to the beginning of this thread's range.
    */

    if (fseek(fp, data->index[data->start_id].en_offset, SEEK_SET) != 0)
    {
        perror("fseek");
        fclose(fp);
        pthread_exit(NULL);
    }

    /*
       Sequential traversal through this thread's chunk.
    */

    for (long id = data->start_id;
         id <= data->end_id;
         id++)
    {
        if (fgets(line, sizeof(line), fp) == NULL)
            break;

        if (contains_word(line, data->word))
        {
            add_id(&data->phase1_ids, id);
        }
    }

    fclose(fp);

    pthread_exit(NULL);
}


/* ---------------------------------------------------------
   PHASE 2
   ---------------------------------------------------------

   Thread 0:
       takes IDs collected by Thread 1
       checks W1 at those IDs

   Thread 1:
       takes IDs collected by Thread 0
       checks W2 at those IDs

   IMPORTANT:
       No sequential traversal of the other half.
       We directly seek to selected sentence IDs.
   --------------------------------------------------------- */

void *phase2_search(void *arg)
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
       The other thread's list will be supplied through
       phase1_ids of the other ThreadData.
    */

    ThreadData *other;

    if (data->thread_id == 0)
        other = &((ThreadData *)arg)[1];

    /*
       We cannot obtain the other ThreadData this way because
       arg points to only one ThreadData.

       Therefore this function is not used directly.
    */

    fclose(fp);

    pthread_exit(NULL);
}


/* ---------------------------------------------------------
   PHASE 2 DATA
   --------------------------------------------------------- */

typedef struct
{
    int thread_id;

    const char *word;

    SentenceInfo *index;

    IDList *input_ids;

    IDList *output_ids;

} Phase2Data;


/* ---------------------------------------------------------
   ACTUAL PHASE 2 WORKER
   --------------------------------------------------------- */

void *phase2_worker(void *arg)
{
    Phase2Data *data = (Phase2Data *)arg;

    FILE *fp = fopen(CORPUS_FILE, "r");

    if (fp == NULL)
    {
        perror("fopen");
        pthread_exit(NULL);
    }

    char line[MAX_LINE];

    /*
       Traverse ONLY the sentence IDs selected
       by the other thread.
    */

    for (long i = 0;
         i < data->input_ids->count;
         i++)
    {
        long sentence_id = data->input_ids->ids[i];

        /*
           Direct jump to the required sentence.
        */

        if (fseek(
                fp,
                data->index[sentence_id].en_offset,
                SEEK_SET) != 0)
        {
            perror("fseek");
            continue;
        }

        if (fgets(line, sizeof(line), fp) == NULL)
            continue;

        /*
           Check this selected sentence for our word.
        */

        if (contains_word(line, data->word))
        {
            add_id(
                data->output_ids,
                sentence_id
            );
        }
    }

    fclose(fp);

    pthread_exit(NULL);
}


/* ---------------------------------------------------------
   LOAD LINE INDEX
   --------------------------------------------------------- */

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


/* ---------------------------------------------------------
   MAIN
   --------------------------------------------------------- */

int main(int argc, char *argv[])
{
    if (argc != 3)
    {
        printf(
            "Usage: %s <word1> <word2>\n",
            argv[0]
        );

        return 1;
    }

    const char *word1 = argv[1];
    const char *word2 = argv[2];

    /*
       ------------------------------------------------------
       LOAD INDEX
       ------------------------------------------------------
    */

    long sentence_count;

    SentenceInfo *index =
        load_index(&sentence_count);

    printf("\n========================================\n");
    printf(" TWO-WORD TWO-PHASE SEARCH\n");
    printf("========================================\n");

    printf("Word 1 : %s\n", word1);
    printf("Word 2 : %s\n", word2);

    printf("Sentences : %ld\n", sentence_count);

    /*
       ------------------------------------------------------
       DIVIDE CORPUS INTO TWO PARTITIONS
       ------------------------------------------------------
    */

    long middle = sentence_count / 2;

    /*
       Thread 0:
           0 -> middle-1

       Thread 1:
           middle -> N-1
    */

    ThreadData thread_data[2];

    thread_data[0].thread_id = 0;
    thread_data[0].word = word1;
    thread_data[0].start_id = 0;
    thread_data[0].end_id = middle - 1;
    thread_data[0].index = index;

    thread_data[1].thread_id = 1;
    thread_data[1].word = word2;
    thread_data[1].start_id = middle;
    thread_data[1].end_id = sentence_count - 1;
    thread_data[1].index = index;

    init_id_list(&thread_data[0].phase1_ids);
    init_id_list(&thread_data[1].phase1_ids);

    init_id_list(&thread_data[0].phase2_ids);
    init_id_list(&thread_data[1].phase2_ids);

    /*
       ------------------------------------------------------
       PHASE 1
       ------------------------------------------------------
    */

    printf("\nPHASE 1\n");

    printf(
        "Thread 0: W1=%s, IDs %ld -> %ld\n",
        word1,
        thread_data[0].start_id,
        thread_data[0].end_id
    );

    printf(
        "Thread 1: W2=%s, IDs %ld -> %ld\n",
        word2,
        thread_data[1].start_id,
        thread_data[1].end_id
    );

    pthread_t t1;
    pthread_t t2;

    double phase1_start = get_time_sec();

    pthread_create(
        &t1,
        NULL,
        phase1_search,
        &thread_data[0]
    );

    pthread_create(
        &t2,
        NULL,
        phase1_search,
        &thread_data[1]
    );

    pthread_join(t1, NULL);
    pthread_join(t2, NULL);

    double phase1_end = get_time_sec();

    printf(
        "Thread 0 selected IDs: %ld\n",
        thread_data[0].phase1_ids.count
    );

    printf(
        "Thread 1 selected IDs: %ld\n",
        thread_data[1].phase1_ids.count
    );

    printf(
        "Phase 1 time: %.6f seconds\n",
        phase1_end - phase1_start
    );

    /*
       ------------------------------------------------------
       PHASE 2
       ------------------------------------------------------

       Thread 0:
           uses Thread 1's IDs
           searches W1

       Thread 1:
           uses Thread 0's IDs
           searches W2
    */

    Phase2Data p2[2];

    p2[0].thread_id = 0;
    p2[0].word = word1;
    p2[0].index = index;

    p2[0].input_ids =
        &thread_data[1].phase1_ids;

    p2[0].output_ids =
        &thread_data[0].phase2_ids;

    p2[1].thread_id = 1;
    p2[1].word = word2;
    p2[1].index = index;

    p2[1].input_ids =
        &thread_data[0].phase1_ids;

    p2[1].output_ids =
        &thread_data[1].phase2_ids;

    printf("\nPHASE 2\n");

    printf(
        "Thread 0: W1 on Thread 1 selected IDs\n"
    );

    printf(
        "Thread 1: W2 on Thread 0 selected IDs\n"
    );

    double phase2_start = get_time_sec();

    pthread_create(
        &t1,
        NULL,
        phase2_worker,
        &p2[0]
    );

    pthread_create(
        &t2,
        NULL,
        phase2_worker,
        &p2[1]
    );

    pthread_join(t1, NULL);
    pthread_join(t2, NULL);

    double phase2_end = get_time_sec();

    printf(
        "Thread 0 Phase-2 matches: %ld\n",
        thread_data[0].phase2_ids.count
    );

    printf(
        "Thread 1 Phase-2 matches: %ld\n",
        thread_data[1].phase2_ids.count
    );

    printf(
        "Phase 2 time: %.6f seconds\n",
        phase2_end - phase2_start
    );

    /*
       ------------------------------------------------------
       TOTAL
       ------------------------------------------------------
    */

    printf("\n========================================\n");

    printf(
        "Total time: %.6f seconds\n",
        (phase1_end - phase1_start) +
        (phase2_end - phase2_start)
    );

    printf(
        "Thread 0 final matches: %ld\n",
        thread_data[0].phase2_ids.count
    );

    printf(
        "Thread 1 final matches: %ld\n",
        thread_data[1].phase2_ids.count
    );

    printf("========================================\n");

    /*
       ------------------------------------------------------
       CLEANUP
       ------------------------------------------------------
    */

    free_id_list(&thread_data[0].phase1_ids);
    free_id_list(&thread_data[1].phase1_ids);

    free_id_list(&thread_data[0].phase2_ids);
    free_id_list(&thread_data[1].phase2_ids);

    free(index);

    return 0;
}