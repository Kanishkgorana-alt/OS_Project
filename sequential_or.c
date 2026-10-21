#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#define CORPUS_FILE "/home/kanishk-gorana/Documents/OSP/IITB_score_gt_065.en"
#define MAX_LINE 10000

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
   CHECK WHETHER SENTENCE CONTAINS A WORD
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
   MAIN
   ============================================================ */

int main()
{
    const char *word1 = "government";
    const char *word2 = "policy";

    FILE *fp = fopen(CORPUS_FILE, "r");

    if (fp == NULL)
    {
        perror("fopen");
        return 1;
    }

    char line[MAX_LINE];

    long sentence_id = 0;
    long matching_sentences = 0;

    double start_time = get_time_sec();

    /* ========================================================
       FULL SEQUENTIAL TRAVERSAL
       ======================================================== */

    while (fgets(line, sizeof(line), fp) != NULL)
    {
        int has_word1 =
            contains_word(line, word1);

        int has_word2 =
            contains_word(line, word2);

        /*
           OR condition:
           sentence is selected if either word exists.
        */

        if (has_word1 || has_word2)
        {
            matching_sentences++;

            /*
               Uncomment if you want sentence IDs:

               printf("Sentence ID: %ld\n", sentence_id);
            */
        }

        sentence_id++;
    }

    double end_time = get_time_sec();

    fclose(fp);

    /* ========================================================
       RESULTS
       ======================================================== */

    printf("\n========================================\n");
    printf(" SEQUENTIAL TWO-WORD OR SEARCH\n");
    printf("========================================\n");

    printf("Word 1              : %s\n", word1);
    printf("Word 2              : %s\n", word2);

    printf(
        "Sentences traversed : %ld\n",
        sentence_id
    );

    printf(
        "Matching sentences  : %ld\n",
        matching_sentences
    );

    printf(
        "Time                : %.6f seconds\n",
        end_time - start_time
    );

    printf("========================================\n");

    return 0;
}