#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include <time.h>
#include <unistd.h>     // nice()

typedef struct bucket
{
    int *ids;
} bucket;

typedef struct keys
{
    int *en_id;
    int en_mode;

    int *hi_id;
    int hi_mode;
    // mode = 0 -> it should be there
    // mode = 1 -> if there, select
    // mode = 2 -> should not be there
} keys;


/* -------------------------------------------------
   Thread function
   ------------------------------------------------- */

void *threadhandle(void *arg)
{
    char *word = (char *)arg;

    printf("Thread searching for: %s\n", word);

    return NULL;
}


/* -------------------------------------------------
   Sequential Retriever
   ------------------------------------------------- */

bucket *seq_retriever(keys *keywords)
{
    char hi_corpus_path[] =
        "/mnt/d/Sur_Pl/AP/Dataset/IITB_score_065/IITB_score_gt_065.hi";

    char en_corpus_path[] =
        "/mnt/d/Sur_Pl/AP/Dataset/IITB_score_065/IITB_score_gt_065.en";

    FILE *hi_ptr = fopen(hi_corpus_path, "r");
    FILE *en_ptr = fopen(en_corpus_path, "r");

    if (hi_ptr == NULL || en_ptr == NULL)
    {
        perror("Error opening corpus");
        return NULL;
    }

    /*
       Sequential retrieval implementation will go here.
    */

    fclose(hi_ptr);
    fclose(en_ptr);

    return NULL;
}


/* -------------------------------------------------
   Parallel Retriever
   ------------------------------------------------- */

bucket *par_retriever(keys *keywords)
{
    pthread_t tid1, tid2, tid3, tid4;

    pthread_attr_t attr;
    pthread_attr_init(&attr);

    /*
       nice() changes the process priority.
       nice(10) lowers the priority of this process.
    */
    nice(10);

    char *word1 = "व्यायाम";
    char *word2 = "भारत";
    char *word3 = "शिक्षा";
    char *word4 = "स्वास्थ्य";

    pthread_create(
        &tid1,
        &attr,
        threadhandle,
        word1
    );

    pthread_create(
        &tid2,
        &attr,
        threadhandle,
        word2
    );

    pthread_create(
        &tid3,
        &attr,
        threadhandle,
        word3
    );

    pthread_create(
        &tid4,
        &attr,
        threadhandle,
        word4
    );

    pthread_join(tid1, NULL);
    pthread_join(tid2, NULL);
    pthread_join(tid3, NULL);
    pthread_join(tid4, NULL);

    pthread_attr_destroy(&attr);

    return NULL;
}


/* -------------------------------------------------
   Main
   ------------------------------------------------- */

int main()
{
    char *a = "व्यायाम";

    char hi_corpus_path[] =
        "/mnt/d/Sur_Pl/AP/Dataset/IITB_score_065/IITB_score_gt_065.hi";

    char en_corpus_path[] =
        "/mnt/d/Sur_Pl/AP/Dataset/IITB_score_065/IITB_score_gt_065.en";

    FILE *hi_ptr = fopen(hi_corpus_path, "r");
    FILE *en_ptr = fopen(en_corpus_path, "r");

    if (hi_ptr == NULL)
    {
        perror("Could not open Hindi corpus");
        return 1;
    }

    if (en_ptr == NULL)
    {
        perror("Could not open English corpus");
        fclose(hi_ptr);
        return 1;
    }

    char hi_sentence[1000];

    /* Start timing */
    struct timespec start, end;

    clock_gettime(CLOCK_MONOTONIC, &start);

    char *c = fgets(
        hi_sentence,
        sizeof(hi_sentence),
        hi_ptr
    );

    if (c != NULL)
    {
        if (strstr(c, a) != NULL)
        {
            printf("Found\n");
        }
        else
        {
            printf("Not Found\n");
        }
    }

    /* End timing */
    clock_gettime(CLOCK_MONOTONIC, &end);

    double time_taken =
        (end.tv_sec - start.tv_sec) +
        (end.tv_nsec - start.tv_nsec) / 1e9;

    printf("Execution time: %.9f seconds\n", time_taken);

    if (c != NULL)
    {
        printf("%s", hi_sentence);
    }

    fclose(hi_ptr);
    fclose(en_ptr);

    return 0;
}