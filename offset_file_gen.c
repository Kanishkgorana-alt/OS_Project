#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#define EN_PATH "/home/kanishk-gorana/Documents/OSP/IITB_score_gt_065.en"
#define HI_PATH "/home/kanishk-gorana/Documents/OSP/IITB_score_gt_065.hi"

#define INDEX_PATH "/home/kanishk-gorana/Documents/OSP/line_index.txt"

#define MAX_LINES 1030875


int main()
{
    FILE *en_fp = fopen(EN_PATH, "rb");
    FILE *hi_fp = fopen(HI_PATH, "rb");

    if (en_fp == NULL)
    {
        perror("Error opening English corpus");
        return 1;
    }

    if (hi_fp == NULL)
    {
        perror("Error opening Hindi corpus");
        fclose(en_fp);
        return 1;
    }

    FILE *index_fp = fopen(INDEX_PATH, "w");

    if (index_fp == NULL)
    {
        perror("Error creating index file");
        fclose(en_fp);
        fclose(hi_fp);
        return 1;
    }

    /*
        Format:

        line_id    English offset    Hindi offset

        Example:

        0          0                 0
        1          85                91
        2          143               158
    */

    fprintf(index_fp, "line_id en_offset hi_offset\n");

    long en_pos = 0;
    long hi_pos = 0;

    int en_line = 0;
    int hi_line = 0;

    int line_id = 0;

    while (line_id < MAX_LINES)
    {
        /*
            Store the beginning position of this line.
        */

        en_pos = ftell(en_fp);
        hi_pos = ftell(hi_fp);

        /*
            Check whether both files still have data.
        */

        int en_char = fgetc(en_fp);
        int hi_char = fgetc(hi_fp);

        if (en_char == EOF || hi_char == EOF)
        {
            break;
        }

        /*
            We consumed the first character of each line.
            Continue until newline.
        */

        while (en_char != '\n' && en_char != EOF)
        {
            en_char = fgetc(en_fp);
        }

        while (hi_char != '\n' && hi_char != EOF)
        {
            hi_char = fgetc(hi_fp);
        }

        /*
            Write:

            line_id
            English starting byte
            Hindi starting byte
        */

        fprintf(
            index_fp,
            "%d %ld %ld\n",
            line_id,
            en_pos,
            hi_pos
        );

        line_id++;

        /*
            Progress information every 100,000 lines.
        */

        if (line_id % 100000 == 0)
        {
            printf(
                "Processed %d lines...\n",
                line_id
            );
        }
    }

    printf("\nIndex generation completed.\n");
    printf("Total lines indexed: %d\n", line_id);

    fclose(en_fp);
    fclose(hi_fp);
    fclose(index_fp);

    return 0;
}