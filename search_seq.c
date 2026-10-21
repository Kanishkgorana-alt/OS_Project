#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

#define TOTAL_SENTENCES 1030875

#define EN_CORPUS_PATH \
"/home/kanishk-gorana/Documents/OSP/IITB_score_gt_065.en"

#define INDEX_PATH \
"/home/kanishk-gorana/Documents/OSP/line_index.txt"

#define OUTPUT_PATH \
"/home/kanishk-gorana/Documents/OSP/government_indices_seq.txt"

#define MAX_LINE_LENGTH 10000
#define MAX_WORD_LENGTH 100


/* =========================================================
   TIMER
   ========================================================= */

double get_time()
{
    struct timespec ts;

    clock_gettime(
        CLOCK_MONOTONIC,
        &ts
    );

    return ts.tv_sec +
           ts.tv_nsec / 1e9;
}


/* =========================================================
   INDEX STRUCTURE
   ========================================================= */

typedef struct
{
    long en_offset;

} LineIndex;


/* =========================================================
   LOWERCASE
   ========================================================= */

void to_lowercase(char *str)
{
    for (int i = 0; str[i] != '\0'; i++)
    {
        str[i] = (char)tolower(
            (unsigned char)str[i]
        );
    }
}


/* =========================================================
   WORD SEARCH
   ========================================================= */

int contains_word(
    char *line,
    const char *search_word
)
{
    char *saveptr;

    char *word = strtok_r(
        line,
        " \t\r\n.,!?;:\"'()[]{}<>-/_",
        &saveptr
    );


    while (word != NULL)
    {
        char temp[MAX_WORD_LENGTH];


        strncpy(
            temp,
            word,
            MAX_WORD_LENGTH - 1
        );


        temp[MAX_WORD_LENGTH - 1] = '\0';


        to_lowercase(temp);


        if (strcmp(
                temp,
                search_word
            ) == 0)
        {
            return 1;
        }


        word = strtok_r(
            NULL,
            " \t\r\n.,!?;:\"'()[]{}<>-/_",
            &saveptr
        );
    }


    return 0;
}


/* =========================================================
   LOAD INDEX
   ========================================================= */

long load_index(
    LineIndex **index
)
{
    FILE *fp = fopen(
        INDEX_PATH,
        "r"
    );


    if (fp == NULL)
    {
        perror(
            "Error opening line_index.txt"
        );

        return -1;
    }


    *index = malloc(
        TOTAL_SENTENCES *
        sizeof(LineIndex)
    );


    if (*index == NULL)
    {
        perror(
            "Memory allocation failed"
        );

        fclose(fp);

        return -1;
    }


    /* Skip header */

    char header[256];


    if (
        fgets(
            header,
            sizeof(header),
            fp
        ) == NULL
    )
    {
        printf(
            "Index file is empty.\n"
        );

        fclose(fp);

        free(*index);

        return -1;
    }


    long line_id;
    long en_offset;
    long hi_offset;

    long count = 0;


    while (
        count < TOTAL_SENTENCES &&
        fscanf(
            fp,
            "%ld %ld %ld",
            &line_id,
            &en_offset,
            &hi_offset
        ) == 3
    )
    {
        (*index)[count].en_offset =
            en_offset;

        count++;
    }


    fclose(fp);


    return count;
}


/* =========================================================
   MAIN
   ========================================================= */

int main()
{
    LineIndex *index = NULL;


    /* -----------------------------------------------------
       LOAD INDEX
       ----------------------------------------------------- */

    printf(
        "Loading sentence index...\n"
    );


    long indexed_sentences =
        load_index(&index);


    if (
        indexed_sentences <
        TOTAL_SENTENCES
    )
    {
        printf(
            "Error: expected %d sentences, "
            "but index contains %ld.\n",
            TOTAL_SENTENCES,
            indexed_sentences
        );

        free(index);

        return 1;
    }


    printf(
        "Loaded %ld sentences.\n",
        indexed_sentences
    );


    /* -----------------------------------------------------
       SEARCH WORD
       ----------------------------------------------------- */

    char search_word[MAX_WORD_LENGTH];


    printf(
        "\nEnter word to search: "
    );


    scanf(
        "%99s",
        search_word
    );


    to_lowercase(search_word);


    /* -----------------------------------------------------
       OPEN ENGLISH CORPUS
       ----------------------------------------------------- */

    FILE *fp = fopen(
        EN_CORPUS_PATH,
        "rb"
    );


    if (fp == NULL)
    {
        perror(
            "Error opening English corpus"
        );

        free(index);

        return 1;
    }


    /* -----------------------------------------------------
       ALLOCATE RESULT STORAGE
       
       Worst case:
       every sentence contains the word.
       ----------------------------------------------------- */

    long *match_indices = malloc(
        TOTAL_SENTENCES *
        sizeof(long)
    );


    if (match_indices == NULL)
    {
        perror(
            "Memory allocation failed"
        );

        fclose(fp);
        free(index);

        return 1;
    }


    /* -----------------------------------------------------
       START TIMER
       ----------------------------------------------------- */

    double start = get_time();


    /* -----------------------------------------------------
       Go to first sentence
       ----------------------------------------------------- */

    if (
        fseek(
            fp,
            index[0].en_offset,
            SEEK_SET
        ) != 0
    )
    {
        perror(
            "fseek failed"
        );

        fclose(fp);
        free(index);
        free(match_indices);

        return 1;
    }


    /* -----------------------------------------------------
       SEARCH
       
       EXACTLY 1,030,875 SENTENCES
       ----------------------------------------------------- */

    char line[MAX_LINE_LENGTH];

    long match_count = 0;

    long sentences_searched = 0;


    for (
        long sentence_id = 0;
        sentence_id < TOTAL_SENTENCES;
        sentence_id++
    )
    {
        if (
            fgets(
                line,
                sizeof(line),
                fp
            ) == NULL
        )
        {
            printf(
                "Unexpected EOF at sentence %ld\n",
                sentence_id
            );

            break;
        }


        sentences_searched++;


        if (
            contains_word(
                line,
                search_word
            )
        )
        {
            /*
               Store sentence index
            */

            match_indices[
                match_count
            ] = sentence_id;


            match_count++;
        }
    }


    /* -----------------------------------------------------
       STOP TIMER
       ----------------------------------------------------- */

    double end = get_time();


    /* -----------------------------------------------------
       WRITE MATCHING INDICES
       ----------------------------------------------------- */

    FILE *out = fopen(
        OUTPUT_PATH,
        "w"
    );


    if (out == NULL)
    {
        perror(
            "Error creating output file"
        );

        fclose(fp);
        free(index);
        free(match_indices);

        return 1;
    }


    fprintf(
        out,
        "Search word: %s\n",
        search_word
    );


    fprintf(
        out,
        "Total matches: %ld\n\n",
        match_count
    );


    fprintf(
        out,
        "Sentence_ID\n"
    );


    for (
        long i = 0;
        i < match_count;
        i++
    )
    {
        fprintf(
            out,
            "%ld\n",
            match_indices[i]
        );
    }


    fclose(out);


    /* -----------------------------------------------------
       RESULTS
       ----------------------------------------------------- */

    printf(
        "\n==============================\n"
    );


    printf(
        "Search word         : %s\n",
        search_word
    );


    printf(
        "Sentences searched  : %ld\n",
        sentences_searched
    );


    printf(
        "Matching sentences  : %ld\n",
        match_count
    );


    printf(
        "Search time         : %.6f seconds\n",
        end - start
    );


    printf(
        "Indices saved to    :\n%s\n",
        OUTPUT_PATH
    );


    printf(
        "==============================\n"
    );


    /* -----------------------------------------------------
       CLEANUP
       ----------------------------------------------------- */

    fclose(fp);

    free(index);

    free(match_indices);


    return 0;
}