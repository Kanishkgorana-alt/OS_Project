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

#define NUM_BLOCKS 8
#define THREADS_PER_BLOCK 2
#define TOTAL_THREADS 16

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
   ID LIST
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
        /*
           Skip delimiters
        */

        while (*p != '\0' &&
               is_delimiter((unsigned char)*p))
        {
            p++;
        }

        if (*p == '\0')
            break;


        /*
           Start of token
        */

        const char *start = p;


        /*
           Find end of token
        */

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
   LOAD INDEX
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


    *sentence_count =
        max_id + 1;


    return index;
}


/* ============================================================
   BLOCK
   ============================================================ */

typedef struct
{
    long start_id;
    long end_id;

} Block;


/* ============================================================
   LOAD BLOCKS
   ============================================================ */

int load_blocks(
    const char *filename,
    Block blocks[])
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


        if (sscanf(line,
                   "%ld %ld %ld %ld",
                   &block_id,
                   &start_id,
                   &end_id,
                   &sentence_count) == 4)
        {
            if (block_id >= 0 &&
                block_id < NUM_BLOCKS)
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
   THREAD TASK
   ============================================================ */

typedef struct
{
    int block_id;

    int child_id;

    long start_id;
    long end_id;

    SentenceInfo *index;

    /*
       Phase 1 candidate IDs.
    */

    IDList candidates;

    /*
       Phase 2 final IDs.
    */

    IDList final_results;

} SearchTask;


/* ============================================================
   PHASE 1
   ============================================================

   child 0:
       English WORD1

   child 1:
       Hindi WORD2

   Each child searches ONLY its half.
   ============================================================ */

void *phase1_worker(void *arg)
{
    SearchTask *task =
        (SearchTask *)arg;


    FILE *fp;


    /*
       Child 0 → English
       Child 1 → Hindi
    */

    if (task->child_id == 0)
    {
        fp = fopen(EN_FILE, "r");
    }
    else
    {
        fp = fopen(HI_FILE, "r");
    }


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
        long offset;


        if (task->child_id == 0)
        {
            offset =
                task->index[id].en_offset;
        }
        else
        {
            offset =
                task->index[id].hi_offset;
        }


        if (fseek(fp,
                  offset,
                  SEEK_SET) != 0)
        {
            continue;
        }


        ssize_t length =
            getline(&line,
                    &line_capacity,
                    fp);


        if (length == -1)
            continue;


        const char *word;


        if (task->child_id == 0)
        {
            word = WORD1;
        }
        else
        {
            word = WORD2;
        }


        if (contains_word(line, word))
        {
            add_id(&task->candidates, id);
        }
    }


    free(line);

    fclose(fp);


    pthread_exit(NULL);
}


/* ============================================================
   PHASE 2
   ============================================================

   child 0:
       receives candidates from child 1
       checks English WORD1

   child 1:
       receives candidates from child 0
       checks Hindi WORD2
   ============================================================ */

typedef struct
{
    int block_id;

    int child_id;

    SentenceInfo *index;

    IDList *other_candidates;

    IDList final_results;

} Phase2Task;


void *phase2_worker(void *arg)
{
    Phase2Task *task =
        (Phase2Task *)arg;


    FILE *fp;


    /*
       Child 0 checks English.
       Child 1 checks Hindi.
    */

    if (task->child_id == 0)
    {
        fp = fopen(EN_FILE, "r");
    }
    else
    {
        fp = fopen(HI_FILE, "r");
    }


    if (fp == NULL)
    {
        perror("fopen corpus");

        pthread_exit(NULL);
    }


    char *line = NULL;

    size_t line_capacity = 0;


    const char *word;


    if (task->child_id == 0)
    {
        word = WORD1;
    }
    else
    {
        word = WORD2;
    }


    /*
       Traverse candidates generated
       by the OTHER child.
    */

    for (size_t i = 0;
         i < task->other_candidates->count;
         i++)
    {
        long id =
            task->other_candidates->ids[i];


        long offset;


        if (task->child_id == 0)
        {
            /*
               English check
            */

            offset =
                task->index[id].en_offset;
        }
        else
        {
            /*
               Hindi check
            */

            offset =
                task->index[id].hi_offset;
        }


        if (fseek(fp,
                  offset,
                  SEEK_SET) != 0)
        {
            continue;
        }


        ssize_t length =
            getline(&line,
                    &line_capacity,
                    fp);


        if (length == -1)
            continue;


        if (contains_word(line, word))
        {
            add_id(&task->final_results,
                   id);
        }
    }


    free(line);

    fclose(fp);


    pthread_exit(NULL);
}


/* ============================================================
   MAIN
   ============================================================ */

int main()
{
    printf("============================================\n");
    printf("   2-LEVEL BILINGUAL AND SEARCH\n");
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

    Block blocks[NUM_BLOCKS];


    int block_count =
        load_blocks(PARTITION_FILE,
                    blocks);


    if (block_count != NUM_BLOCKS)
    {
        fprintf(stderr,
                "Expected %d blocks, found %d\n",
                NUM_BLOCKS,
                block_count);

        free(index);

        return EXIT_FAILURE;
    }


    printf("\nBlocks:\n");


    for (int i = 0;
         i < NUM_BLOCKS;
         i++)
    {
        printf("Block %d: %ld -> %ld\n",
               i,
               blocks[i].start_id,
               blocks[i].end_id);
    }


    /* ========================================================
       CREATE 16 PHASE-1 TASKS
       ======================================================== */

    pthread_t phase1_threads[TOTAL_THREADS];

    SearchTask phase1_tasks[TOTAL_THREADS];


    for (int block = 0;
         block < NUM_BLOCKS;
         block++)
    {
        long start =
            blocks[block].start_id;

        long end =
            blocks[block].end_id;


        /*
           Split block into two sentence-ID halves.
        */

        long middle =
            start + (end - start + 1) / 2 - 1;


        int index_a =
            block * 2;

        int index_b =
            block * 2 + 1;


        /*
           Child 0:
           First half → English WORD1
        */

        phase1_tasks[index_a].block_id =
            block;

        phase1_tasks[index_a].child_id =
            0;

        phase1_tasks[index_a].start_id =
            start;

        phase1_tasks[index_a].end_id =
            middle;

        phase1_tasks[index_a].index =
            index;

        init_idlist(
            &phase1_tasks[index_a].candidates);

        init_idlist(
            &phase1_tasks[index_a].final_results);


        /*
           Child 1:
           Second half → Hindi WORD2
        */

        phase1_tasks[index_b].block_id =
            block;

        phase1_tasks[index_b].child_id =
            1;

        phase1_tasks[index_b].start_id =
            middle + 1;

        phase1_tasks[index_b].end_id =
            end;

        phase1_tasks[index_b].index =
            index;

        init_idlist(
            &phase1_tasks[index_b].candidates);

        init_idlist(
            &phase1_tasks[index_b].final_results);


        printf("\nBlock %d split:\n",
               block);

        printf("  Child 0: %ld -> %ld | English\n",
               phase1_tasks[index_a].start_id,
               phase1_tasks[index_a].end_id);

        printf("  Child 1: %ld -> %ld | Hindi\n",
               phase1_tasks[index_b].start_id,
               phase1_tasks[index_b].end_id);
    }


    /* ========================================================
       PHASE 1 START
       ======================================================== */

    double phase1_start =
        get_time_sec();


    for (int i = 0;
         i < TOTAL_THREADS;
         i++)
    {
        pthread_create(
            &phase1_threads[i],
            NULL,
            phase1_worker,
            &phase1_tasks[i]);
    }


    for (int i = 0;
         i < TOTAL_THREADS;
         i++)
    {
        pthread_join(
            phase1_threads[i],
            NULL);
    }


    double phase1_end =
        get_time_sec();


    /* ========================================================
       PHASE 1 RESULTS
       ======================================================== */

    printf("\n============================================\n");
    printf("PHASE 1 RESULTS\n");
    printf("============================================\n");


    size_t total_candidates = 0;


    for (int block = 0;
         block < NUM_BLOCKS;
         block++)
    {
        int a = block * 2;
        int b = block * 2 + 1;


        printf("Block %d:\n",
               block);

        printf("  English candidates: %zu\n",
               phase1_tasks[a].candidates.count);

        printf("  Hindi candidates  : %zu\n",
               phase1_tasks[b].candidates.count);


        total_candidates +=
            phase1_tasks[a].candidates.count;

        total_candidates +=
            phase1_tasks[b].candidates.count;
    }


    printf("\nTotal Phase-1 candidates: %zu\n",
           total_candidates);


    printf("Phase 1 time: %.6f seconds\n",
           phase1_end - phase1_start);


    /* ========================================================
       CREATE PHASE-2 TASKS
       ======================================================== */

    pthread_t phase2_threads[TOTAL_THREADS];

    Phase2Task phase2_tasks[TOTAL_THREADS];


    for (int block = 0;
         block < NUM_BLOCKS;
         block++)
    {
        int a = block * 2;
        int b = block * 2 + 1;


        /*
           Child 0:
           Search English WORD1
           on Child 1's Hindi candidates.
        */

        phase2_tasks[a].block_id =
            block;

        phase2_tasks[a].child_id =
            0;

        phase2_tasks[a].index =
            index;

        phase2_tasks[a].other_candidates =
            &phase1_tasks[b].candidates;

        init_idlist(
            &phase2_tasks[a].final_results);


        /*
           Child 1:
           Search Hindi WORD2
           on Child 0's English candidates.
        */

        phase2_tasks[b].block_id =
            block;

        phase2_tasks[b].child_id =
            1;

        phase2_tasks[b].index =
            index;

        phase2_tasks[b].other_candidates =
            &phase1_tasks[a].candidates;

        init_idlist(
            &phase2_tasks[b].final_results);
    }


    /* ========================================================
       PHASE 2 START
       ======================================================== */

    double phase2_start =
        get_time_sec();


    for (int i = 0;
         i < TOTAL_THREADS;
         i++)
    {
        pthread_create(
            &phase2_threads[i],
            NULL,
            phase2_worker,
            &phase2_tasks[i]);
    }


    for (int i = 0;
         i < TOTAL_THREADS;
         i++)
    {
        pthread_join(
            phase2_threads[i],
            NULL);
    }


    double phase2_end =
        get_time_sec();


    /* ========================================================
       PHASE 2 RESULTS
       ======================================================== */

    printf("\n============================================\n");
    printf("PHASE 2 RESULTS\n");
    printf("============================================\n");


    size_t total_final = 0;


    for (int block = 0;
         block < NUM_BLOCKS;
         block++)
    {
        int a = block * 2;
        int b = block * 2 + 1;


        printf("Block %d:\n",
               block);


        /*
           Child 0:
           final matches from second half.
        */

        printf("  English cross-check: %zu\n",
               phase2_tasks[a].final_results.count);


        /*
           Child 1:
           final matches from first half.
        */

        printf("  Hindi cross-check  : %zu\n",
               phase2_tasks[b].final_results.count);


        total_final +=
            phase2_tasks[a].final_results.count;

        total_final +=
            phase2_tasks[b].final_results.count;
    }


    printf("\nFinal matching sentence pairs: %zu\n",
           total_final);


    printf("Phase 2 time: %.6f seconds\n",
           phase2_end - phase2_start);


    /* ========================================================
       TOTAL TIME
       ======================================================== */

    printf("\n============================================\n");
    printf("FINAL SUMMARY\n");
    printf("============================================\n");


    printf("Phase 1 time: %.6f seconds\n",
           phase1_end - phase1_start);

    printf("Phase 2 time: %.6f seconds\n",
           phase2_end - phase2_start);

    printf("Total time: %.6f seconds\n",
           phase2_end - phase1_start);

    printf("Total matching pairs: %zu\n",
           total_final);

    printf("============================================\n");


    /* ========================================================
       CLEANUP
       ======================================================== */

    for (int i = 0;
         i < TOTAL_THREADS;
         i++)
    {
        free_idlist(
            &phase1_tasks[i].candidates);

        free_idlist(
            &phase1_tasks[i].final_results);

        free_idlist(
            &phase2_tasks[i].final_results);
    }


    free(index);


    return 0;
}