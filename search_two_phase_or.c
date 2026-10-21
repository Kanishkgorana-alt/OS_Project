#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <pthread.h>
#include <time.h>
#include <ctype.h>

#define EN_FILE "/home/kanishk-gorana/Documents/OSP/IITB_score_gt_065.en"
#define INDEX_FILE "/home/kanishk-gorana/Documents/OSP/line_index.txt"

#define NUM_THREADS 2

#define WORD1 "government"
#define WORD2 "policy"


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
   DATA STRUCTURES
   ============================================================ */

typedef struct
{
    long en_offset;
} SentenceInfo;


typedef struct
{
    long *ids;
    size_t count;
    size_t capacity;
} IDList;


typedef struct
{
    const char *word;

    long start_id;
    long end_id;

    SentenceInfo *index;

    IDList results;

} SearchTask;


/* ============================================================
   ID LIST FUNCTIONS
   ============================================================ */

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


int contains_word(const char *sentence, const char *word)
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

        /* Start of current token */

        const char *start = p;

        while (*p != '\0' &&
               !is_delimiter((unsigned char)*p))
        {
            p++;
        }

        size_t token_len = p - start;

        if (token_len == word_len)
        {
            if (strncasecmp(start, word, word_len) == 0)
                return 1;
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
        perror("fopen index");
        exit(EXIT_FAILURE);
    }

    size_t capacity = 1024;

    SentenceInfo *index =
        calloc(capacity, sizeof(SentenceInfo));

    if (index == NULL)
    {
        perror("calloc");
        exit(EXIT_FAILURE);
    }

    long max_id = -1;

    char line[512];

    while (fgets(line, sizeof(line), fp))
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

        /* Expand if necessary */

        while ((size_t)id >= capacity)
        {
            capacity *= 2;

            SentenceInfo *new_index =
                realloc(index,
                        capacity * sizeof(SentenceInfo));

            if (new_index == NULL)
            {
                perror("realloc");
                exit(EXIT_FAILURE);
            }

            index = new_index;
        }

        /*
           IMPORTANT:
           Store using the actual sentence ID.
        */

        index[id].en_offset = en_offset;

        if (id > max_id)
            max_id = id;
    }

    fclose(fp);

    *sentence_count = max_id + 1;

    return index;
}


/* ============================================================
   PHASE 1 SEARCH
   ============================================================ */

void *phase1_worker(void *arg)
{
    SearchTask *task = (SearchTask *)arg;

    FILE *fp = fopen(EN_FILE, "r");

    if (fp == NULL)
    {
        perror("fopen corpus");
        pthread_exit(NULL);
    }

    char *line = NULL;
    size_t line_capacity = 0;

    for (long id = task->start_id;
         id <= task->end_id;
         id++)
    {
        if (fseek(fp,
                  task->index[id].en_offset,
                  SEEK_SET) != 0)
        {
            perror("fseek");
            continue;
        }

        ssize_t length =
            getline(&line,
                    &line_capacity,
                    fp);

        if (length == -1)
            continue;

        if (contains_word(line, task->word))
        {
            add_id(&task->results, id);
        }
    }

    free(line);

    fclose(fp);

    pthread_exit(NULL);
}


/* ============================================================
   BUILD COMPLEMENT
   ============================================================

   Given:

       partition = [start ... end]

       selected = IDs already selected

   Create:

       complement =
       partition - selected

   Since selected IDs are sorted, this is O(n).
   ============================================================ */

void build_complement(
    long start,
    long end,
    const IDList *selected,
    IDList *complement)
{
    size_t selected_pos = 0;

    for (long id = start;
         id <= end;
         id++)
    {
        if (selected_pos < selected->count &&
            selected->ids[selected_pos] == id)
        {
            selected_pos++;
        }
        else
        {
            add_id(complement, id);
        }
    }
}


/* ============================================================
   PHASE 2 SEARCH
   ============================================================ */

typedef struct
{
    const char *word;

    const IDList *candidate_ids;

    SentenceInfo *index;

    IDList results;

} Phase2Task;


void *phase2_worker(void *arg)
{
    Phase2Task *task = (Phase2Task *)arg;

    FILE *fp = fopen(EN_FILE, "r");

    if (fp == NULL)
    {
        perror("fopen corpus");
        pthread_exit(NULL);
    }

    char *line = NULL;
    size_t line_capacity = 0;

    for (size_t i = 0;
         i < task->candidate_ids->count;
         i++)
    {
        long id =
            task->candidate_ids->ids[i];

        if (fseek(fp,
                  task->index[id].en_offset,
                  SEEK_SET) != 0)
        {
            perror("fseek");
            continue;
        }

        ssize_t length =
            getline(&line,
                    &line_capacity,
                    fp);

        if (length == -1)
            continue;

        if (contains_word(line, task->word))
        {
            add_id(&task->results, id);
        }
    }

    free(line);

    fclose(fp);

    pthread_exit(NULL);
}


/* ============================================================
   FINAL ID COMPARISON
   ============================================================ */

int compare_ids(const void *a, const void *b)
{
    long x = *(const long *)a;
    long y = *(const long *)b;

    if (x < y)
        return -1;

    if (x > y)
        return 1;

    return 0;
}


size_t count_unique(IDList *list)
{
    if (list->count == 0)
        return 0;

    qsort(list->ids,
          list->count,
          sizeof(long),
          compare_ids);

    size_t unique_count = 1;

    for (size_t i = 1;
         i < list->count;
         i++)
    {
        if (list->ids[i] !=
            list->ids[unique_count - 1])
        {
            list->ids[unique_count++] =
                list->ids[i];
        }
    }

    return unique_count;
}


/* ============================================================
   MAIN
   ============================================================ */

int main()
{
    printf("============================================\n");
    printf("        TWO-PHASE OR SEARCH\n");
    printf("============================================\n");

    printf("Word 1: %s\n", WORD1);
    printf("Word 2: %s\n\n", WORD2);


    /* --------------------------------------------------------
       LOAD INDEX
       -------------------------------------------------------- */

    long sentence_count;

    SentenceInfo *index =
        load_index(INDEX_FILE,
                   &sentence_count);

    printf("Sentence count: %ld\n",
           sentence_count);


    /* --------------------------------------------------------
       CREATE TWO PARTITIONS
       -------------------------------------------------------- */

    long middle =
        sentence_count / 2;

    long first_start = 0;
    long first_end = middle - 1;

    long second_start = middle;
    long second_end = sentence_count - 1;


    printf("\nPartitions:\n");

    printf("Partition 1: %ld - %ld\n",
           first_start,
           first_end);

    printf("Partition 2: %ld - %ld\n",
           second_start,
           second_end);


    /* ========================================================
       PHASE 1
       ======================================================== */

    printf("\n============================================\n");
    printf("PHASE 1\n");
    printf("============================================\n");

    pthread_t threads[NUM_THREADS];

    SearchTask phase1_tasks[NUM_THREADS];


    /*
       T0:
       Word 1 on Partition 1

       T1:
       Word 2 on Partition 2
    */

    phase1_tasks[0].word = WORD1;
    phase1_tasks[0].start_id = first_start;
    phase1_tasks[0].end_id = first_end;
    phase1_tasks[0].index = index;

    init_idlist(&phase1_tasks[0].results);


    phase1_tasks[1].word = WORD2;
    phase1_tasks[1].start_id = second_start;
    phase1_tasks[1].end_id = second_end;
    phase1_tasks[1].index = index;

    init_idlist(&phase1_tasks[1].results);


    double phase1_start =
        get_time_sec();


    pthread_create(
        &threads[0],
        NULL,
        phase1_worker,
        &phase1_tasks[0]);


    pthread_create(
        &threads[1],
        NULL,
        phase1_worker,
        &phase1_tasks[1]);


    pthread_join(
        threads[0],
        NULL);

    pthread_join(
        threads[1],
        NULL);


    double phase1_end =
        get_time_sec();


    printf("P1 - %s: %zu\n",
           WORD1,
           phase1_tasks[0].results.count);

    printf("P2 - %s: %zu\n",
           WORD2,
           phase1_tasks[1].results.count);

    printf("Phase 1 time: %.6f seconds\n",
           phase1_end - phase1_start);


    /* ========================================================
       PHASE 2
       ======================================================== */

    printf("\n============================================\n");
    printf("PHASE 2\n");
    printf("============================================\n");


    IDList complement1;
    IDList complement2;

    init_idlist(&complement1);
    init_idlist(&complement2);


    double phase2_start =
        get_time_sec();


    /*
       Partition 2:
       remove IDs selected by T1 in Phase 1.

       T0 searches Word 1 on the remaining IDs.
    */

    build_complement(
        second_start,
        second_end,
        &phase1_tasks[1].results,
        &complement2);


    /*
       Partition 1:
       remove IDs selected by T0 in Phase 1.

       T1 searches Word 2 on the remaining IDs.
    */

    build_complement(
        first_start,
        first_end,
        &phase1_tasks[0].results,
        &complement1);


    printf("Complement of P2-W2: %zu IDs\n",
           complement2.count);

    printf("Complement of P1-W1: %zu IDs\n",
           complement1.count);


    Phase2Task phase2_tasks[NUM_THREADS];


    /*
       T0:
       Word 1 on NOT(P2-W2)
    */

    phase2_tasks[0].word = WORD1;
    phase2_tasks[0].candidate_ids = &complement2;
    phase2_tasks[0].index = index;

    init_idlist(&phase2_tasks[0].results);


    /*
       T1:
       Word 2 on NOT(P1-W1)
    */

    phase2_tasks[1].word = WORD2;
    phase2_tasks[1].candidate_ids = &complement1;
    phase2_tasks[1].index = index;

    init_idlist(&phase2_tasks[1].results);


    pthread_create(
        &threads[0],
        NULL,
        phase2_worker,
        &phase2_tasks[0]);


    pthread_create(
        &threads[1],
        NULL,
        phase2_worker,
        &phase2_tasks[1]);


    pthread_join(
        threads[0],
        NULL);

    pthread_join(
        threads[1],
        NULL);


    double phase2_end =
        get_time_sec();


    printf("P2 - %s: %zu\n",
           WORD1,
           phase2_tasks[0].results.count);

    printf("P1 - %s: %zu\n",
           WORD2,
           phase2_tasks[1].results.count);

    printf("Phase 2 time: %.6f seconds\n",
           phase2_end - phase2_start);


    /* ========================================================
       FINAL RESULT
       ======================================================== */

    printf("\n============================================\n");
    printf("FINAL RESULT\n");
    printf("============================================\n");


    size_t partition1_result =
        phase1_tasks[0].results.count +
        phase2_tasks[1].results.count;


    size_t partition2_result =
        phase1_tasks[1].results.count +
        phase2_tasks[0].results.count;


    size_t total_result =
        partition1_result +
        partition2_result;


    printf("Partition 1 OR result: %zu\n",
           partition1_result);

    printf("Partition 2 OR result: %zu\n",
           partition2_result);

    printf("Final OR matches: %zu\n",
           total_result);


    double total_time =
        phase2_end - phase1_start;


    printf("Total time: %.6f seconds\n",
           total_time);


    /* ========================================================
       FINAL DUPLICATE CHECK
       ======================================================== */

    IDList final_ids;

    init_idlist(&final_ids);


    /*
       Add all four result sets.
    */

    for (size_t i = 0;
         i < phase1_tasks[0].results.count;
         i++)
    {
        add_id(
            &final_ids,
            phase1_tasks[0].results.ids[i]);
    }


    for (size_t i = 0;
         i < phase1_tasks[1].results.count;
         i++)
    {
        add_id(
            &final_ids,
            phase1_tasks[1].results.ids[i]);
    }


    for (size_t i = 0;
         i < phase2_tasks[0].results.count;
         i++)
    {
        add_id(
            &final_ids,
            phase2_tasks[0].results.ids[i]);
    }


    for (size_t i = 0;
         i < phase2_tasks[1].results.count;
         i++)
    {
        add_id(
            &final_ids,
            phase2_tasks[1].results.ids[i]);
    }


    size_t unique_count =
        count_unique(&final_ids);


    printf("\nUnique final IDs: %zu\n",
           unique_count);


    /* ========================================================
       CLEANUP
       ======================================================== */

    free_idlist(&phase1_tasks[0].results);
    free_idlist(&phase1_tasks[1].results);

    free_idlist(&phase2_tasks[0].results);
    free_idlist(&phase2_tasks[1].results);

    free_idlist(&complement1);
    free_idlist(&complement2);

    free_idlist(&final_ids);

    free(index);


    return 0;
}