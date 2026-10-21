#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <pthread.h>
#include <time.h>


/* =========================================================
   CONFIGURATION
   ========================================================= */

#define NUM_THREADS 8
#define TOTAL_SENTENCES 1030875

#define EN_CORPUS_PATH \
"/home/kanishk-gorana/Documents/OSP/IITB_score_gt_065.en"

#define INDEX_PATH \
"/home/kanishk-gorana/Documents/OSP/line_index.txt"

#define EN_PARTITION_PATH \
"/home/kanishk-gorana/Documents/OSP/en_partitions.txt"

#define OUTPUT_PATH \
"/home/kanishk-gorana/Documents/OSP/government_indices.txt"

#define MAX_LINE_LENGTH 10000
#define MAX_WORD_LENGTH 100


/* =========================================================
   INDEX STRUCTURE
   ========================================================= */

typedef struct
{
    long en_offset;

} LineIndex;


/* =========================================================
   PARTITION STRUCTURE
   ========================================================= */

typedef struct
{
    long start_sentence;

    /*
       Stored in partition file as inclusive end.
       We convert it to exclusive end when
       assigning to a thread.
    */
    long end_sentence;

} Partition;


/* =========================================================
   THREAD DATA
   ========================================================= */

typedef struct
{
    int thread_id;

    long start_sentence;
    long end_sentence;

    const char *search_word;

    LineIndex *index;

    /*
       Matching sentence indices
       found by this thread.
    */
    long *match_indices;

    long match_count;

} ThreadData;


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
   LOWERCASE
   ========================================================= */

void to_lowercase(char *str)
{
    for (
        int i = 0;
        str[i] != '\0';
        i++
    )
    {
        str[i] =
            (char)tolower(
                (unsigned char)str[i]
            );
    }
}


/* =========================================================
   CHECK WHETHER WORD EXISTS IN SENTENCE
   ========================================================= */

int contains_word(
    char *line,
    const char *search_word
)
{
    char *saveptr;

    char *word =
        strtok_r(
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

        temp[MAX_WORD_LENGTH - 1] =
            '\0';

        to_lowercase(temp);

        if (
            strcmp(
                temp,
                search_word
            ) == 0
        )
        {
            return 1;
        }

        word =
            strtok_r(
                NULL,
                " \t\r\n.,!?;:\"'()[]{}<>-/_",
                &saveptr
            );
    }

    return 0;
}


/* =========================================================
   LOAD SENTENCE INDEX
   ========================================================= */

long load_index(LineIndex **index)
{
    FILE *fp =
        fopen(
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


    /*
       Allocate memory for all
       sentence offsets.
    */

    *index =
        malloc(
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


    /*
       Skip header.
    */

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


    /*
       Read:

       sentence_id
       english_offset
       hindi_offset
    */

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
   LOAD ENGLISH PARTITIONS
   ========================================================= */

/* =========================================================
   LOAD ENGLISH PARTITIONS
   ========================================================= */

int load_partitions(Partition partitions[])
{
    FILE *fp = fopen(
        EN_PARTITION_PATH,
        "r"
    );

    if (fp == NULL)
    {
        perror("Error opening en_partitions.txt");
        return -1;
    }

    char line[1024];

    int blocks_loaded = 0;

    /*
       Read the file line by line.

       We do NOT assume a fixed number of
       header lines.

       We look for lines beginning with
       a numeric block ID.
    */

    while (
        fgets(
            line,
            sizeof(line),
            fp
        ) != NULL
    )
    {
        int block_id;

        long start;
        long end;

        long bytes;
        long sentences;


        /*
           Expected data format:

           Block_ID
           Start_Sentence
           End_Sentence
           Bytes
           Sentences

           Example:

           0 0 135437 12117235 135438
        */

        int fields = sscanf(
            line,
            "%d %ld %ld %ld %ld",
            &block_id,
            &start,
            &end,
            &bytes,
            &sentences
        );


        /*
           If this is not a data line,
           simply skip it.

           This automatically skips:

           TOTAL_SENTENCES ...
           NUM_BLOCKS ...
           TOTAL_BYTES ...
           IDEAL_BLOCK_BYTES ...
           Block Start_Sentence ...
        */

        if (fields != 5)
        {
            continue;
        }


        /*
           Validate block ID.
        */

        if (
            block_id < 0 ||
            block_id >= NUM_THREADS
        )
        {
            continue;
        }


        /*
           Store the partition.
        */

        partitions[block_id].start_sentence =
            start;

        partitions[block_id].end_sentence =
            end;


        blocks_loaded++;
    }


    fclose(fp);


    /*
       Make sure all 8 blocks were loaded.
    */

    if (
        blocks_loaded != NUM_THREADS
    )
    {
        printf(
            "Error: expected %d partitions, "
            "but loaded %d.\n",
            NUM_THREADS,
            blocks_loaded
        );

        return -1;
    }


    return 0;
}

/* =========================================================
   THREAD FUNCTION
   ========================================================= */

void *search_partition(void *arg)
{
    ThreadData *data =
        (ThreadData *)arg;


    /*
       Each thread opens its own
       FILE pointer.

       Therefore there is no shared
       FILE-position race.
    */

    FILE *fp =
        fopen(
            EN_CORPUS_PATH,
            "rb"
        );

    if (fp == NULL)
    {
        perror(
            "Error opening English corpus"
        );

        pthread_exit(NULL);
    }


    /*
       Jump directly to the beginning
       of this thread's block.

       IMPORTANT:

       We perform only ONE fseek()
       per thread.

       After that, fgets() reads
       sequentially until the block ends.
    */

    if (
        fseek(
            fp,
            data->index[
                data->start_sentence
            ].en_offset,
            SEEK_SET
        ) != 0
    )
    {
        perror(
            "fseek failed"
        );

        fclose(fp);

        pthread_exit(NULL);
    }


    char line[MAX_LINE_LENGTH];


    /*
       Search only inside this block.

       start_sentence = inclusive

       end_sentence = exclusive
    */

    for (
        long sentence_id =
            data->start_sentence;

        sentence_id <
            data->end_sentence;

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
            break;
        }


        /*
           contains_word() modifies
           line using strtok_r().
        */

        if (
            contains_word(
                line,
                data->search_word
            )
        )
        {
            /*
               Store matching sentence ID
               in this thread's private array.
            */

            data->match_indices[
                data->match_count
            ] = sentence_id;

            data->match_count++;
        }
    }


    fclose(fp);

    pthread_exit(NULL);
}


/* =========================================================
   MAIN
   ========================================================= */

int main()
{
    LineIndex *index = NULL;


    /* =====================================================
       LOAD SENTENCE INDEX
       ===================================================== */

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


    /* =====================================================
       LOAD ENGLISH BLOCK PARTITIONS
       ===================================================== */

    Partition partitions[
        NUM_THREADS
    ];


    if (
        load_partitions(
            partitions
        ) != 0
    )
    {
        free(index);

        return 1;
    }


    printf(
        "\nLoaded English partitions:\n"
    );


    for (
        int i = 0;
        i < NUM_THREADS;
        i++
    )
    {
        printf(
            "Block %d -> %ld to %ld\n",
            i,
            partitions[i].start_sentence,
            partitions[i].end_sentence
        );
    }


    /* =====================================================
       SEARCH WORD
       ===================================================== */

    char search_word[
        MAX_WORD_LENGTH
    ];


    printf(
        "\nEnter word to search: "
    );


    scanf(
        "%99s",
        search_word
    );


    to_lowercase(
        search_word
    );


    /* =====================================================
       START TIMER
       ===================================================== */

    double start =
        get_time();


    /* =====================================================
       THREAD ARRAYS
       ===================================================== */

    pthread_t threads[
        NUM_THREADS
    ];

    ThreadData thread_data[
        NUM_THREADS
    ];


    /* =====================================================
       CREATE THREADS
       ===================================================== */

    for (
        int i = 0;
        i < NUM_THREADS;
        i++
    )
    {
        thread_data[i].thread_id =
            i;


        /*
           Partition file stores:

           Start = inclusive
           End   = inclusive

           Thread search uses:

           Start = inclusive
           End   = exclusive

           Therefore:

           exclusive_end = inclusive_end + 1
        */

        thread_data[i].start_sentence =
            partitions[i].start_sentence;


        thread_data[i].end_sentence =
            partitions[i].end_sentence + 1;


        thread_data[i].search_word =
            search_word;


        thread_data[i].index =
            index;


        thread_data[i].match_count =
            0;


        /*
           Maximum possible matches
           inside this block.

           Worst case:
           every sentence matches.
        */

        long partition_size =
            thread_data[i].end_sentence -
            thread_data[i].start_sentence;


        thread_data[i].match_indices =
            malloc(
                partition_size *
                sizeof(long)
            );


        if (
            thread_data[i].match_indices
            == NULL
        )
        {
            perror(
                "Memory allocation failed"
            );

            free(index);

            return 1;
        }


        printf(
            "Thread %d -> "
            "sentences %ld to %ld "
            "(%ld sentences)\n",

            i,

            thread_data[i].start_sentence,

            thread_data[i].end_sentence - 1,

            partition_size
        );


        /*
           Create thread.

           The thread receives its own
           independent block.
        */

        if (
            pthread_create(
                &threads[i],
                NULL,
                search_partition,
                &thread_data[i]
            ) != 0
        )
        {
            perror(
                "pthread_create failed"
            );

            /*
               Free allocations already made.
            */

            for (
                int j = 0;
                j <= i;
                j++
            )
            {
                free(
                    thread_data[j]
                        .match_indices
                );
            }

            free(index);

            return 1;
        }
    }


    /* =====================================================
       WAIT FOR ALL THREADS
       ===================================================== */

    long total_matches = 0;


    for (
        int i = 0;
        i < NUM_THREADS;
        i++
    )
    {
        pthread_join(
            threads[i],
            NULL
        );


        total_matches +=
            thread_data[i].match_count;
    }


    /* =====================================================
       STOP TIMER
       ===================================================== */

    double end =
        get_time();


    /* =====================================================
       OPEN OUTPUT FILE
       ===================================================== */

    FILE *out =
        fopen(
            OUTPUT_PATH,
            "w"
        );


    if (out == NULL)
    {
        perror(
            "Error creating output file"
        );


        for (
            int i = 0;
            i < NUM_THREADS;
            i++
        )
        {
            free(
                thread_data[i]
                    .match_indices
            );
        }


        free(index);

        return 1;
    }


    /* =====================================================
       WRITE OUTPUT HEADER
       ===================================================== */

    fprintf(
        out,
        "Search word: %s\n",
        search_word
    );


    fprintf(
        out,
        "Total matches: %ld\n\n",
        total_matches
    );


    fprintf(
        out,
        "Sentence_ID\n"
    );


    /* =====================================================
       WRITE MATCHING INDICES
       ===================================================== */

    /*
       Threads are ordered according
       to sentence ranges.

       Therefore:

       T0 → lowest sentence IDs
       T1 → next range
       ...
       T7 → highest range

       Writing T0 -> T7 preserves
       global sentence-ID order.
    */

    for (
        int i = 0;
        i < NUM_THREADS;
        i++
    )
    {
        for (
            long j = 0;
            j <
                thread_data[i].match_count;
            j++
        )
        {
            fprintf(
                out,
                "%ld\n",
                thread_data[i]
                    .match_indices[j]
            );
        }
    }


    fclose(out);


    /* =====================================================
       PRINT RESULT
       ===================================================== */

    printf(
        "\n====================================\n"
    );


    printf(
        "Search word     : %s\n",
        search_word
    );


    printf(
        "Total matches   : %ld\n",
        total_matches
    );


    printf(
        "Indices saved to:\n%s\n",
        OUTPUT_PATH
    );


    printf(
        "Search time     : %.6f seconds\n",
        end - start
    );


    printf(
        "====================================\n"
    );


    /* =====================================================
       FREE MEMORY
       ===================================================== */

    for (
        int i = 0;
        i < NUM_THREADS;
        i++
    )
    {
        free(
            thread_data[i]
                .match_indices
        );
    }


    free(index);


    return 0;
}