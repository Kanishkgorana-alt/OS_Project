#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define TOTAL_SENTENCES 1030875
#define NUM_BLOCKS 8

#define EN_CORPUS_PATH \
"/home/kanishk-gorana/Documents/OSP/IITB_score_gt_065.en"

#define HI_CORPUS_PATH \
"/home/kanishk-gorana/Documents/OSP/IITB_score_gt_065.hi"

#define INDEX_PATH \
"/home/kanishk-gorana/Documents/OSP/line_index.txt"

#define EN_OUTPUT_PATH \
"/home/kanishk-gorana/Documents/OSP/en_partitions.txt"

#define HI_OUTPUT_PATH \
"/home/kanishk-gorana/Documents/OSP/hi_partitions.txt"


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
   SENTENCE OFFSET
   ========================================================= */

typedef struct
{
    long en_offset;
    long hi_offset;

} LineIndex;


/* =========================================================
   PARTITION
   ========================================================= */

typedef struct
{
    long start_sentence;
    long end_sentence;

    long long bytes;

} Partition;


/* =========================================================
   CREATE BYTE-BALANCED PARTITIONS
   ========================================================= */

void create_partitions(
    LineIndex *index,
    long long file_size,
    int language,
    Partition partitions[NUM_BLOCKS]
)
{
    /*
       language:

       0 = English
       1 = Hindi
    */


    /* -----------------------------------------------------
       Calculate total bytes
       ----------------------------------------------------- */

    long long total_bytes = file_size;


    double ideal_block_size =
        (double)total_bytes /
        NUM_BLOCKS;


    printf(
        "\n--------------------------------------------\n"
    );


    if (language == 0)
    {
        printf(
            "ENGLISH PARTITIONING\n"
        );
    }
    else
    {
        printf(
            "HINDI PARTITIONING\n"
        );
    }


    printf(
        "Total bytes        : %lld\n",
        total_bytes
    );


    printf(
        "Ideal block bytes  : %.2f\n",
        ideal_block_size
    );


    /* -----------------------------------------------------
       Initialize
       ----------------------------------------------------- */

    for (int i = 0; i < NUM_BLOCKS; i++)
    {
        partitions[i].start_sentence = 0;

        partitions[i].end_sentence = -1;

        partitions[i].bytes = 0;
    }


    long current_sentence = 0;

    int current_block = 0;

    long long current_block_bytes = 0;


    partitions[0].start_sentence = 0;


    /* -----------------------------------------------------
       Traverse all sentences
       ----------------------------------------------------- */

    while (
        current_sentence <
        TOTAL_SENTENCES
    )
    {
        long long sentence_start;
        long long sentence_end;

        /*
           Get sentence offsets
        */

        if (language == 0)
        {
            sentence_start =
                index[current_sentence]
                    .en_offset;
        }
        else
        {
            sentence_start =
                index[current_sentence]
                    .hi_offset;
        }


        /*
           Find sentence end
        */

        if (
            current_sentence <
            TOTAL_SENTENCES - 1
        )
        {
            if (language == 0)
            {
                sentence_end =
                    index[
                        current_sentence + 1
                    ].en_offset;
            }
            else
            {
                sentence_end =
                    index[
                        current_sentence + 1
                    ].hi_offset;
            }
        }
        else
        {
            sentence_end =
                file_size;
        }


        long long sentence_bytes =
            sentence_end -
            sentence_start;


        /* -------------------------------------------------
           Decide whether to start next block
           ------------------------------------------------- */

        if (
            current_block <
            NUM_BLOCKS - 1
            &&
            current_block_bytes > 0
            &&
            current_block_bytes +
                sentence_bytes
                >
                (long long)
                ideal_block_size
        )
        {
            /*
               Finish current block
            */

            partitions[
                current_block
            ].end_sentence =
                current_sentence - 1;


            current_block++;


            /*
               Start next block
            */

            partitions[
                current_block
            ].start_sentence =
                current_sentence;


            current_block_bytes = 0;
        }


        /* -------------------------------------------------
           Add sentence to current block
           ------------------------------------------------- */

        partitions[
            current_block
        ].bytes +=
            sentence_bytes;


        current_block_bytes +=
            sentence_bytes;


        current_sentence++;
    }


    /*
       Finish final block
    */

    partitions[
        NUM_BLOCKS - 1
    ].end_sentence =
        TOTAL_SENTENCES - 1;


    /* -----------------------------------------------------
       Display
       ----------------------------------------------------- */

    printf(
        "\n%-8s %-15s %-15s %-18s %-15s\n",
        "Block",
        "Start",
        "End",
        "Bytes",
        "Sentences"
    );


    double max_imbalance = 0.0;


    for (int i = 0; i < NUM_BLOCKS; i++)
    {
        long sentences =
            partitions[i].end_sentence
            -
            partitions[i].start_sentence
            +
            1;


        double difference =
            (
                (
                    (double)
                    partitions[i].bytes
                    -
                    ideal_block_size
                )
                /
                ideal_block_size
            )
            * 100.0;


        double absolute_difference =
            difference < 0
            ? -difference
            : difference;


        if (
            absolute_difference >
            max_imbalance
        )
        {
            max_imbalance =
                absolute_difference;
        }


        printf(
            "%-8d %-15ld %-15ld %-18lld %-15ld\n",
            i,
            partitions[i].start_sentence,
            partitions[i].end_sentence,
            partitions[i].bytes,
            sentences
        );


        printf(
            "         Difference: %+.4f%%\n",
            difference
        );
    }


    printf(
        "\nMaximum imbalance: %.4f%%\n",
        max_imbalance
    );
}


/* =========================================================
   SAVE PARTITIONS
   ========================================================= */

void save_partitions(
    const char *path,
    Partition partitions[NUM_BLOCKS],
    long long file_size
)
{
    FILE *fp = fopen(
        path,
        "w"
    );


    if (fp == NULL)
    {
        perror(
            "Error creating partition file"
        );

        return;
    }


    double ideal_block_size =
        (double)file_size /
        NUM_BLOCKS;


    fprintf(
        fp,
        "TOTAL_SENTENCES %d\n",
        TOTAL_SENTENCES
    );


    fprintf(
        fp,
        "NUM_BLOCKS %d\n",
        NUM_BLOCKS
    );


    fprintf(
        fp,
        "TOTAL_BYTES %lld\n",
        file_size
    );


    fprintf(
        fp,
        "IDEAL_BLOCK_BYTES %.2f\n\n",
        ideal_block_size
    );


    fprintf(
        fp,
        "Block Start_Sentence End_Sentence "
        "Bytes Sentences\n"
    );


    for (int i = 0; i < NUM_BLOCKS; i++)
    {
        long sentences =
            partitions[i].end_sentence
            -
            partitions[i].start_sentence
            +
            1;


        fprintf(
            fp,
            "%d %ld %ld %lld %ld\n",
            i,
            partitions[i].start_sentence,
            partitions[i].end_sentence,
            partitions[i].bytes,
            sentences
        );
    }


    fclose(fp);
}


/* =========================================================
   MAIN
   ========================================================= */

int main()
{
    double start_time = get_time();


    /* =====================================================
       LOAD LINE INDEX
       ===================================================== */

    FILE *index_fp = fopen(
        INDEX_PATH,
        "r"
    );


    if (index_fp == NULL)
    {
        perror(
            "Error opening line_index.txt"
        );

        return 1;
    }


    LineIndex *index = malloc(
        TOTAL_SENTENCES *
        sizeof(LineIndex)
    );


    if (index == NULL)
    {
        perror(
            "Memory allocation failed"
        );

        fclose(index_fp);

        return 1;
    }


    /* -----------------------------------------------------
       Skip header
       ----------------------------------------------------- */

    char header[256];

    if (
        fgets(
            header,
            sizeof(header),
            index_fp
        ) == NULL
    )
    {
        printf(
            "Index file is empty.\n"
        );

        fclose(index_fp);
        free(index);

        return 1;
    }


    /* -----------------------------------------------------
       Read offsets
       ----------------------------------------------------- */

    long line_id;
    long en_offset;
    long hi_offset;

    long count = 0;


    while (
        count < TOTAL_SENTENCES
        &&
        fscanf(
            index_fp,
            "%ld %ld %ld",
            &line_id,
            &en_offset,
            &hi_offset
        ) == 3
    )
    {
        index[count].en_offset =
            en_offset;

        index[count].hi_offset =
            hi_offset;

        count++;
    }


    fclose(index_fp);


    if (count != TOTAL_SENTENCES)
    {
        printf(
            "Error: expected %d sentences, "
            "found %ld.\n",
            TOTAL_SENTENCES,
            count
        );

        free(index);

        return 1;
    }


    printf(
        "Loaded %ld sentence offsets.\n",
        count
    );


    /* =====================================================
       GET FILE SIZES
       ===================================================== */

    FILE *en_fp = fopen(
        EN_CORPUS_PATH,
        "rb"
    );


    FILE *hi_fp = fopen(
        HI_CORPUS_PATH,
        "rb"
    );


    if (
        en_fp == NULL ||
        hi_fp == NULL
    )
    {
        perror(
            "Error opening corpus"
        );

        if (en_fp)
            fclose(en_fp);

        if (hi_fp)
            fclose(hi_fp);

        free(index);

        return 1;
    }


    fseek(
        en_fp,
        0,
        SEEK_END
    );


    long long en_file_size =
        ftell(en_fp);


    fseek(
        hi_fp,
        0,
        SEEK_END
    );


    long long hi_file_size =
        ftell(hi_fp);


    printf(
        "\nEnglish file size : %lld bytes\n",
        en_file_size
    );


    printf(
        "Hindi file size   : %lld bytes\n",
        hi_file_size
    );


    /* =====================================================
       CREATE PARTITIONS
       ===================================================== */

    Partition en_partitions[
        NUM_BLOCKS
    ];


    Partition hi_partitions[
        NUM_BLOCKS
    ];


    create_partitions(
        index,
        en_file_size,
        0,
        en_partitions
    );


    create_partitions(
        index,
        hi_file_size,
        1,
        hi_partitions
    );


    /* =====================================================
       SAVE PARTITION INDICES
       ===================================================== */

    save_partitions(
        EN_OUTPUT_PATH,
        en_partitions,
        en_file_size
    );


    save_partitions(
        HI_OUTPUT_PATH,
        hi_partitions,
        hi_file_size
    );


    /* =====================================================
       CLOSE / CLEANUP
       ===================================================== */

    fclose(en_fp);
    fclose(hi_fp);

    free(index);


    /* =====================================================
       TIMER
       ===================================================== */

    double end_time = get_time();


    printf(
        "\nEnglish partition index:\n%s\n",
        EN_OUTPUT_PATH
    );


    printf(
        "Hindi partition index:\n%s\n",
        HI_OUTPUT_PATH
    );


    printf(
        "\nTotal program time: %.6f seconds\n",
        end_time - start_time
    );


    return 0;
}