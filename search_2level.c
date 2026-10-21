#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <pthread.h>
#include <time.h>


/* =========================================================
   CONFIGURATION
   ========================================================= */

#define NUM_BLOCKS 8
#define CHUNKS_PER_BLOCK 2
#define NUM_THREADS (NUM_BLOCKS * CHUNKS_PER_BLOCK)

#define TOTAL_SENTENCES 1030875

#define EN_CORPUS_PATH \
"/home/kanishk-gorana/Documents/OSP/IITB_score_gt_065.en"

#define INDEX_PATH \
"/home/kanishk-gorana/Documents/OSP/line_index.txt"

#define EN_PARTITION_PATH \
"/home/kanishk-gorana/Documents/OSP/en_partitions.txt"

#define OUTPUT_PATH \
"/home/kanishk-gorana/Documents/OSP/government_indices_2level.txt"

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
   BLOCK STRUCTURE
   ========================================================= */

typedef struct
{
    long start_sentence;

    /*
       End is stored as inclusive
       in en_partitions.txt.
    */

    long end_sentence;

} Block;


/* =========================================================
   CHUNK STRUCTURE
   ========================================================= */

typedef struct
{
    long start_sentence;

    /*
       Internally we use an exclusive end.
    */

    long end_sentence;

} Chunk;


/* =========================================================
   THREAD DATA
   ========================================================= */

typedef struct
{
    int thread_id;

    int block_id;

    int chunk_id;


    long start_sentence;

    /*
       Exclusive end.
    */

    long end_sentence;


    const char *search_word;

    LineIndex *index;


    /*
       Private result array.

       No other thread accesses this array.
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
   LOAD ENGLISH BLOCK PARTITIONS
   ========================================================= */

int load_blocks(Block blocks[])
{
    FILE *fp =
        fopen(
            EN_PARTITION_PATH,
            "r"
        );

    if (fp == NULL)
    {
        perror(
            "Error opening en_partitions.txt"
        );

        return -1;
    }


    char line[1024];

    int blocks_loaded = 0;


    /*
       Do not assume a fixed number of
       header lines.

       Search for valid numeric
       partition records.
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


        int fields =
            sscanf(
                line,
                "%d %ld %ld %ld %ld",
                &block_id,
                &start,
                &end,
                &bytes,
                &sentences
            );


        if (fields != 5)
        {
            continue;
        }


        if (
            block_id < 0 ||
            block_id >= NUM_BLOCKS
        )
        {
            continue;
        }


        blocks[block_id].start_sentence =
            start;

        blocks[block_id].end_sentence =
            end;


        blocks_loaded++;
    }


    fclose(fp);


    if (
        blocks_loaded != NUM_BLOCKS
    )
    {
        printf(
            "Error: expected %d blocks, "
            "but loaded %d.\n",
            NUM_BLOCKS,
            blocks_loaded
        );

        return -1;
    }


    return 0;
}


/* =========================================================
   CREATE CHUNKS FOR ONE BLOCK
   ========================================================= */

void create_chunks(
    Block block,
    Chunk chunks[]
)
{
    /*
       Convert inclusive block end
       to number of sentences.
    */

    long total_sentences =
        block.end_sentence -
        block.start_sentence +
        1;


    /*
       Divide the block into
       CHUNKS_PER_BLOCK chunks.

       Since we use 2 chunks:

       chunk 0 = first half
       chunk 1 = second half
    */

    long base_size =
        total_sentences /
        CHUNKS_PER_BLOCK;

    long remainder =
        total_sentences %
        CHUNKS_PER_BLOCK;


    long current_start =
        block.start_sentence;


    for (
        int i = 0;
        i < CHUNKS_PER_BLOCK;
        i++
    )
    {
        long chunk_size =
            base_size;


        /*
           Distribute remainder among
           the first chunks.
        */

        if (i < remainder)
        {
            chunk_size++;
        }


        chunks[i].start_sentence =
            current_start;


        /*
           Exclusive end.
        */

        chunks[i].end_sentence =
            current_start +
            chunk_size;


        current_start +=
            chunk_size;
    }
}


/* =========================================================
   SECOND-LEVEL THREAD FUNCTION
   ========================================================= */

void *search_chunk(void *arg)
{
    ThreadData *data =
        (ThreadData *)arg;


    /*
       Every second-level worker
       gets its own FILE pointer.
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
       One fseek per chunk.

       The thread jumps directly to
       its chunk's first sentence.
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
       Search only inside this chunk.
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


        if (
            contains_word(
                line,
                data->search_word
            )
        )
        {
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
       LOAD BLOCKS
       ===================================================== */

    Block blocks[NUM_BLOCKS];


    if (
        load_blocks(blocks) != 0
    )
    {
        free(index);

        return 1;
    }


    printf(
        "\nLoaded English blocks:\n"
    );


    for (
        int i = 0;
        i < NUM_BLOCKS;
        i++
    )
    {
        printf(
            "Block %d -> %ld to %ld\n",
            i,
            blocks[i].start_sentence,
            blocks[i].end_sentence
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


    if (
        scanf(
            "%99s",
            search_word
        ) != 1
    )
    {
        printf(
            "Invalid search word.\n"
        );

        free(index);

        return 1;
    }


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


    /*
       We keep chunks here so that
       their boundaries remain valid
       while the threads are running.
    */

    Chunk all_chunks[
        NUM_THREADS
    ];


    /* =====================================================
       CREATE SECOND-LEVEL THREADS
       ===================================================== */

    int thread_index = 0;


    for (
        int block_id = 0;
        block_id < NUM_BLOCKS;
        block_id++
    )
    {
        /*
           Each block is divided into
           CHUNKS_PER_BLOCK chunks.
        */

        Chunk chunks[
            CHUNKS_PER_BLOCK
        ];


        create_chunks(
            blocks[block_id],
            chunks
        );


        for (
            int chunk_id = 0;
            chunk_id < CHUNKS_PER_BLOCK;
            chunk_id++
        )
        {
            /*
               Save chunk information.
            */

            all_chunks[
                thread_index
            ] = chunks[chunk_id];


            thread_data[
                thread_index
            ].thread_id =
                thread_index;


            thread_data[
                thread_index
            ].block_id =
                block_id;


            thread_data[
                thread_index
            ].chunk_id =
                chunk_id;


            thread_data[
                thread_index
            ].start_sentence =
                chunks[chunk_id]
                    .start_sentence;


            thread_data[
                thread_index
            ].end_sentence =
                chunks[chunk_id]
                    .end_sentence;


            thread_data[
                thread_index
            ].search_word =
                search_word;


            thread_data[
                thread_index
            ].index =
                index;


            thread_data[
                thread_index
            ].match_count =
                0;


            /*
               Maximum possible matches
               in this chunk.
            */

            long chunk_size =
                chunks[chunk_id]
                    .end_sentence -
                chunks[chunk_id]
                    .start_sentence;


            thread_data[
                thread_index
            ].match_indices =
                malloc(
                    chunk_size *
                    sizeof(long)
                );


            if (
                thread_data[
                    thread_index
                ].match_indices == NULL
            )
            {
                perror(
                    "Memory allocation failed"
                );


                for (
                    int j = 0;
                    j < thread_index;
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


            printf(
                "Block %d | Chunk %d | "
                "Thread %d -> "
                "sentences %ld to %ld "
                "(%ld sentences)\n",

                block_id,

                chunk_id,

                thread_index,

                chunks[chunk_id]
                    .start_sentence,

                chunks[chunk_id]
                    .end_sentence - 1,

                chunk_size
            );


            /*
               Create second-level worker.
            */

            if (
                pthread_create(
                    &threads[thread_index],
                    NULL,
                    search_chunk,
                    &thread_data[
                        thread_index
                    ]
                ) != 0
            )
            {
                perror(
                    "pthread_create failed"
                );


                for (
                    int j = 0;
                    j <= thread_index;
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


            thread_index++;
        }
    }


    /* =====================================================
       WAIT FOR ALL SECOND-LEVEL THREADS
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
            thread_data[i]
                .match_count;
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
       WRITE OUTPUT
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


    /*
       Thread numbering follows:

       Block 0:
           Thread 0
           Thread 1

       Block 1:
           Thread 2
           Thread 3

       ...

       Block 7:
           Thread 14
           Thread 15

       Therefore writing in thread order
       preserves sentence-ID order.
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
                thread_data[i]
                    .match_count;
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