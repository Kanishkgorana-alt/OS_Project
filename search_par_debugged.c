#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <pthread.h>
#include <time.h>
#include <errno.h>

#define NUM_THREADS 4
#define TOTAL_SENTENCES 1030875L

#define EN_CORPUS_PATH "/home/kanishk-gorana/Documents/OSP/IITB_score_gt_065.en"
#define INDEX_PATH     "/home/kanishk-gorana/Documents/OSP/line_index.txt"
#define OUTPUT_PATH    "/home/kanishk-gorana/Documents/OSP/government_indices.txt"

#define MAX_LINE_LENGTH 10000
#define MAX_WORD_LENGTH 100

typedef struct {
    long en_offset;
} LineIndex;

typedef struct {
    int thread_id;
    long start_sentence; /* inclusive */
    long end_sentence;   /* exclusive */
    const char *search_word;
    const LineIndex *index;
    long *match_indices;
    long match_count;
    double thread_time;
    int error_code;
} ThreadData;

static double get_time(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        perror("clock_gettime");
        exit(EXIT_FAILURE);
    }
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

static void to_lowercase(char *str) {
    for (size_t i = 0; str[i] != '\0'; ++i) {
        str[i] = (char)tolower((unsigned char)str[i]);
    }
}

static int contains_word(char *line, const char *search_word) {
    static const char delimiters[] = " \t\r\n.,!?;:\"'()[]{}<>-/_";

    char *saveptr = NULL;
    char *word = strtok_r(line, delimiters, &saveptr);

    while (word != NULL) {
        char temp[MAX_WORD_LENGTH];
        size_t len = strlen(word);

        if (len >= sizeof(temp)) {
            len = sizeof(temp) - 1;
        }
        memcpy(temp, word, len);
        temp[len] = '\0';
        to_lowercase(temp);

        if (strcmp(temp, search_word) == 0) {
            return 1;
        }
        word = strtok_r(NULL, delimiters, &saveptr);
    }
    return 0;
}

/* Loads sentence IDs and English byte offsets from:
   header line, then: line_id en_offset hi_offset
   Returns the number of index rows loaded, or -1 on error. */
static long load_index(LineIndex **index) {
    FILE *fp = fopen(INDEX_PATH, "r");
    if (fp == NULL) {
        perror("Error opening index file");
        return -1;
    }

    LineIndex *loaded = calloc((size_t)TOTAL_SENTENCES, sizeof(*loaded));
    if (loaded == NULL) {
        perror("Memory allocation for index failed");
        fclose(fp);
        return -1;
    }

    char header[256];
    if (fgets(header, sizeof(header), fp) == NULL) {
        fprintf(stderr, "Index file is empty or unreadable.\n");
        fclose(fp);
        free(loaded);
        return -1;
    }

    long line_id, en_offset, hi_offset;
    long count = 0;
    while (count < TOTAL_SENTENCES &&
           fscanf(fp, "%ld %ld %ld", &line_id, &en_offset, &hi_offset) == 3) {
        if (en_offset < 0) {
            fprintf(stderr, "Invalid negative offset at index row %ld.\n", count);
            fclose(fp);
            free(loaded);
            return -1;
        }
        loaded[count].en_offset = en_offset;
        ++count;
    }

    if (ferror(fp)) {
        perror("Error reading index file");
        fclose(fp);
        free(loaded);
        return -1;
    }

    fclose(fp);
    *index = loaded;
    return count;
}

static void *search_partition(void *arg) {
    ThreadData *data = (ThreadData *)arg;
    double thread_start = get_time();

    FILE *fp = fopen(EN_CORPUS_PATH, "rb");
    if (fp == NULL) {
        data->error_code = errno ? errno : EIO;
        data->thread_time = get_time() - thread_start;
        return NULL;
    }

    long start_offset = data->index[data->start_sentence].en_offset;
    if (fseek(fp, start_offset, SEEK_SET) != 0) {
        data->error_code = errno ? errno : EIO;
        fclose(fp);
        data->thread_time = get_time() - thread_start;
        return NULL;
    }

    char line[MAX_LINE_LENGTH];
    for (long sentence_id = data->start_sentence;
         sentence_id < data->end_sentence;
         ++sentence_id) {
        if (fgets(line, sizeof(line), fp) == NULL) {
            if (ferror(fp)) {
                data->error_code = errno ? errno : EIO;
            } else {
                data->error_code = EIO; /* Corpus ended before expected row count. */
            }
            break;
        }

        if (contains_word(line, data->search_word)) {
            data->match_indices[data->match_count++] = sentence_id;
        }
    }

    fclose(fp);
    data->thread_time = get_time() - thread_start;
    return NULL;
}

int main(void) {
    LineIndex *index = NULL;

    printf("Loading sentence index...\n");
    double total_start = get_time();

    long indexed_sentences = load_index(&index);
    if (indexed_sentences < 0) {
        return EXIT_FAILURE;
    }
    if (indexed_sentences != TOTAL_SENTENCES) {
        fprintf(stderr,
                "Error: expected %ld index rows, but loaded %ld.\n",
                TOTAL_SENTENCES, indexed_sentences);
        free(index);
        return EXIT_FAILURE;
    }
    printf("Loaded %ld sentence offsets.\n", indexed_sentences);

    /* Validate that the requested index and corpus path can be opened. */
    FILE *check_fp = fopen(EN_CORPUS_PATH, "rb");
    if (check_fp == NULL) {
        perror("Error opening English corpus");
        free(index);
        return EXIT_FAILURE;
    }
    fclose(check_fp);

    char search_word[MAX_WORD_LENGTH];
    printf("\nEnter word to search: ");
    fflush(stdout);
    if (scanf("%99s", search_word) != 1) {
        fprintf(stderr, "Failed to read search word.\n");
        free(index);
        return EXIT_FAILURE;
    }
    to_lowercase(search_word);

    double search_start = get_time();
    pthread_t threads[NUM_THREADS];
    ThreadData thread_data[NUM_THREADS];
    int created[NUM_THREADS] = {0};

    long base_size = TOTAL_SENTENCES / NUM_THREADS;
    long remainder = TOTAL_SENTENCES % NUM_THREADS;
    long current_start = 0;

    printf("\nCreating %d threads...\n", NUM_THREADS);

    for (int i = 0; i < NUM_THREADS; ++i) {
        long partition_size = base_size + (i < remainder ? 1L : 0L);

        thread_data[i].thread_id = i;
        thread_data[i].start_sentence = current_start;
        thread_data[i].end_sentence = current_start + partition_size;
        thread_data[i].search_word = search_word;
        thread_data[i].index = index;
        thread_data[i].match_count = 0;
        thread_data[i].thread_time = 0.0;
        thread_data[i].error_code = 0;
        thread_data[i].match_indices = malloc((size_t)partition_size * sizeof(long));

        if (thread_data[i].match_indices == NULL) {
            perror("Memory allocation for match indices failed");
            for (int j = 0; j < i; ++j) {
                free(thread_data[j].match_indices);
            }
            free(index);
            return EXIT_FAILURE;
        }

        printf("Thread %2d -> sentence IDs %ld to %ld (%ld sentences)\n",
               i, thread_data[i].start_sentence,
               thread_data[i].end_sentence - 1, partition_size);

        int rc = pthread_create(&threads[i], NULL, search_partition, &thread_data[i]);
        if (rc != 0) {
            fprintf(stderr, "Failed to create thread %d: %s\n", i, strerror(rc));
            for (int j = 0; j <= i; ++j) {
                free(thread_data[j].match_indices);
            }
            for (int j = 0; j < i; ++j) {
                if (created[j]) {
                    pthread_join(threads[j], NULL);
                }
            }
            free(index);
            return EXIT_FAILURE;
        }
        created[i] = 1;
        current_start += partition_size;
    }

    if (current_start != TOTAL_SENTENCES) {
        fprintf(stderr, "Internal error: partitions cover %ld of %ld sentences.\n",
                current_start, (long)TOTAL_SENTENCES);
        for (int i = 0; i < NUM_THREADS; ++i) {
            if (created[i]) pthread_join(threads[i], NULL);
        }
        for (int i = 0; i < NUM_THREADS; ++i) free(thread_data[i].match_indices);
        free(index);
        return EXIT_FAILURE;
    }

    long total_matches = 0;
    int worker_error = 0;
    for (int i = 0; i < NUM_THREADS; ++i) {
        int rc = pthread_join(threads[i], NULL);
        if (rc != 0) {
            fprintf(stderr, "Failed to join thread %d: %s\n", i, strerror(rc));
            worker_error = 1;
            continue;
        }

        total_matches += thread_data[i].match_count;
        printf("Thread %2d | IDs %ld-%ld | Matches: %ld | Time: %.6f s\n",
               thread_data[i].thread_id,
               thread_data[i].start_sentence,
               thread_data[i].end_sentence - 1,
               thread_data[i].match_count,
               thread_data[i].thread_time);

        if (thread_data[i].error_code != 0) {
            fprintf(stderr, "Thread %d failed: %s\n",
                    i, strerror(thread_data[i].error_code));
            worker_error = 1;
        }
    }

    if (worker_error) {
        fprintf(stderr, "Search did not complete successfully; output not written.\n");
        for (int i = 0; i < NUM_THREADS; ++i) free(thread_data[i].match_indices);
        free(index);
        return EXIT_FAILURE;
    }

    FILE *out = fopen(OUTPUT_PATH, "w");
    if (out == NULL) {
        perror("Error creating output file");
        for (int i = 0; i < NUM_THREADS; ++i) free(thread_data[i].match_indices);
        free(index);
        return EXIT_FAILURE;
    }

    fprintf(out, "Search word: %s\n", search_word);
    fprintf(out, "Total matches: %ld\n\n", total_matches);
    fprintf(out, "Sentence_ID\n");

    /* Partitions are ordered, so this writes matching sentence IDs in ascending order. */
    int write_error = 0;
    for (int i = 0; i < NUM_THREADS && !write_error; ++i) {
        for (long j = 0; j < thread_data[i].match_count; ++j) {
            if (fprintf(out, "%ld\n", thread_data[i].match_indices[j]) < 0) {
                perror("Error writing output file");
                write_error = 1;
                break;
            }
        }
    }

    if (fclose(out) != 0) {
        perror("Error closing output file");
        write_error = 1;
    }

    double search_end = get_time();
    double total_end = get_time();

    printf("\n========================================\n");
    printf("Search word       : %s\n", search_word);
    printf("Total matches     : %ld\n", total_matches);
    printf("Threads requested : %d\n", NUM_THREADS);
    printf("Sentence count    : %ld\n", (long)TOTAL_SENTENCES);
    printf("Search time       : %.6f seconds (threads + joins + output excluded)\n",
           search_end - search_start);
    printf("Total elapsed time: %.6f seconds (includes index loading)\n",
           total_end - total_start);
    printf("Indices saved to  : %s\n", OUTPUT_PATH);
    printf("========================================\n");

    for (int i = 0; i < NUM_THREADS; ++i) {
        free(thread_data[i].match_indices);
    }
    free(index);

    return write_error ? EXIT_FAILURE : EXIT_SUCCESS;
}
