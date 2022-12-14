// Generic implementation of the algoritm
#define _USE_MATH_DEFINES
#define _CRT_SECURE_NO_WARNINGS
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <stdint.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <io.h>
#define F_OK 0
#define access _access
#else
#include <threads.h>
#include <unistd.h>
#endif

#include "bmp.h"


#ifndef WORKERS
#   define WORKERS 8
#endif

#ifndef TRANSFORMS
#   define TRANSFORMS 6
#endif

#ifndef TRANSFORM_SIZE
#   define TRANSFORM_SIZE 6
#endif
#ifndef TRANSFORM
#   define TRANSFORM transform_rgb6
#endif
#define TRANSFORM_SINCOS

#ifndef BLOCK_SIZE_LOG
#   define BLOCK_SIZE_LOG 4
#endif
#define BLOCK_SIZE (1 << BLOCK_SIZE_LOG)
#define BLOCK_SIZE_SQ (1 << BLOCK_SIZE_LOG << BLOCK_SIZE_LOG)

#ifdef TRANSFORM_SINCOS
#define PROTOTYPE_SIZE (BLOCK_SIZE_SQ * TRANSFORM_SIZE)
#define PROTOTYPE_MUL (2.0 / PROTOTYPE_SIZE)
#else
#define PROTOTYPE_SIZE (1 + BLOCK_SIZE_SQ * TRANSFORM_SIZE)
#endif

#define AGGRESSIVE_VECTORISATION
#define SINCOS_MATH

struct image;
struct worker_call;

typedef double real;
#define REAL_255 255.0
#define REAL_0 0.0

typedef real* const mutable_real;
typedef const real* const final_real;
typedef unsigned* const  mutable_uint;
typedef const unsigned* const final_uint;
typedef struct image* const mutable_image;
typedef const struct image* const final_image;

struct image {
    unsigned width;
    unsigned height;
    unsigned* data;
};

struct worker_call {
    final_image in_image;
    unsigned worker;
    unsigned x;
    unsigned y;
    unsigned blk;
    unsigned* writeback;
};

struct worker_call* work[WORKERS];
unsigned work_size[WORKERS];
real* prototypes = 0;
unsigned total_blocks = 0;
unsigned* buckets = 0;
real* similarities[WORKERS];

typedef void(*PFNTRANSFORM)(const unsigned color, mutable_real output);

void transform_rgb6(const unsigned, mutable_real);
void transform_yuv2(const unsigned, mutable_real);

real get_time() {
#ifdef _WIN32
    FILETIME ft;
    GetSystemTimePreciseAsFileTime(&ft);
    uint64_t t = ((uint64_t)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
    return (real)t / (real)10000000;
#else
    struct timespec start;
    clock_gettime(CLOCK_MONOTONIC, &start);
    return start.tv_sec + start.tv_nsec / (real)1000000000;
#endif
}

void load_image(mutable_image output, const char* path) {
    if (!output) return;
    if (strstr(path, ".bin")) {
        FILE* f = fopen(path, "rb");
        if(!fread(output, sizeof(unsigned) * 2, 1, f)) {
            fclose(f);
            return;
        }
        output->data = malloc(sizeof(unsigned) * output->width * output->height);
        if(!fread(output->data, sizeof(unsigned) * output->width * output->height, 1, f)) {
            fclose(f);
            return;
        }
        fclose(f);
        return;
    }
    struct BMP_ bmp;
    load_bmp(&bmp, path);
    output->width = bmp.w;
    output->height = bmp.h;
    output->data = calloc(1, bmp.w * bmp.h * sizeof(int));
    if (!output->data) return;
    for (unsigned y = 0; y < bmp.h; y++) {
        for (unsigned x = 0; x < bmp.w; x++) {
            output->data[y * bmp.w + x] = get_bmp_pixel(&bmp, x, y);
        }
    }
    release_bmp(&bmp);
}

void save_image(final_image input, const char* path) {
    struct BMP_ bmp;
    if (strstr(path, ".bin")) {
        FILE* f = fopen(path, "wb");
        fwrite(input, sizeof(unsigned) * 2, 1, f);
        fwrite(input->data, sizeof(unsigned) * input->width * input->height, 1, f);
        fclose(f);
        return;
    }
    create_bmp(&bmp, input->width, input->height);
    for (unsigned y = 0; y < bmp.h; y++) {
        for (unsigned x = 0; x < bmp.w; x++) {
            set_bmp_pixel(&bmp, x, y, input->data[y * bmp.w + x]);
        }
    }
    save_bmp(&bmp, path);
    release_bmp(&bmp);
}

void release_image(final_image image) {
    free(image->data);
}

inline real cos_similarity(final_real arr1, final_real arr2, unsigned size) {
#ifndef TRANSFORM_SINCOS
    real A1A1 = arr1[0], A2A2 = arr2[0];
    unsigned i = 1;
#else
    unsigned i = 0;
#endif
    real A1A2 = REAL_0;
    for (; i < size; i++) {
        A1A2 += arr1[i] * arr2[i];
    }
#ifdef TRANSFORM_SINCOS
    return A1A2 * PROTOTYPE_MUL;
#else
    return A1A2 / (sqrt(A1A1 * A2A2));
#endif
}

int transform_block(final_image img, const unsigned x, const unsigned y, mutable_real out_prototype, mutable_uint bucket, const unsigned dir) {
    unsigned C = 0, tx = 0, ty = 0, rgb = 0;
#ifdef TRANSFORM_SINCOS
    unsigned c = 0;
#else
    unsigned c = 1;
    real value = 0.0, mag = 0.0;
#endif
    final_uint img_data = img->data;
    unsigned fw = img->width;
    real transformed[TRANSFORM_SIZE];
    for (unsigned _y = 0; _y < BLOCK_SIZE; _y++) {
        for (unsigned _x = 0; _x < BLOCK_SIZE; _x++) {
            switch (dir) {
            case 0:
                tx = x + _x;
                ty = y + _y;
                break;
            case 1:
                tx = x + BLOCK_SIZE - _x - 1;
                ty = y + BLOCK_SIZE - _y - 1;
                break;
            case 2:
                tx = x + _y;
                ty = y + _x;
                break;
            case 3:
                tx = x + BLOCK_SIZE - _y - 1;
                ty = y + BLOCK_SIZE - _x - 1;
                break;
            case 4:
                tx = x + _x;
                ty = y + BLOCK_SIZE - _y - 1;
                break;
            case 5:
                tx = x + BLOCK_SIZE - _x - 1;
                ty = y + _y;
                break;
            }
            rgb = img_data[ty * fw + tx] & 0xFFFFFF;
            if (bucket)
                bucket[C++] = rgb;
            TRANSFORM(rgb, transformed);
            for (unsigned K = 0; K < TRANSFORM_SIZE; K++) {
#ifdef TRANSFORM_SINCOS
                out_prototype[c++] = transformed[K];
#else
                value = transformed[K];
                mag += value * value;
                out_prototype[c++] = value;
#endif
            }
        }
    }
#ifndef TRANSFORM_SINCOS
    out_prototype[0] = mag;
#endif
    return c;
}

int find_nearest_similar(final_image image, mutable_real prototype, const unsigned int worker, const unsigned x, const unsigned y, const unsigned blockCount) {
    unsigned closest = 0;
    unsigned first = 1;
    unsigned i = 0;
    real max_similarity = REAL_0;
    real similarity = REAL_0;
#ifdef AGGRESSIVE_VECTORISATION
    real* writeback = similarities[worker];
#endif
    transform_block(image, x, y, prototype, NULL, 0);

    for (i = 0; i < blockCount; i++) {
#ifdef AGGRESSIVE_VECTORISATION
        writeback[i] = cos_similarity(prototype, prototypes + i * PROTOTYPE_SIZE, PROTOTYPE_SIZE);
#else
        similarity = cos_similarity(prototype, prototypes + i * PROTOTYPE_SIZE, PROTOTYPE_SIZE);
        if (first || similarity > max_similarity) {
            closest = i;
            max_similarity = similarity;
            first = 0;
        }
#endif
    }

#ifdef AGGRESSIVE_VECTORISATION
    for (i = 0; i < blockCount; i++) {
        real similarity = writeback[i];
        if (first || similarity > max_similarity) {
            closest = i;
            max_similarity = similarity;
            first = 0;
        }
    }
#endif

    return closest;
}

int start_worker(void* pid) {
    uintptr_t id = (uintptr_t)pid;
    unsigned i = 0, j = work_size[id];
    real prototype[PROTOTYPE_SIZE];
    for (i = 0; i < j; i++) {
        struct worker_call item = work[id][i];
        item.writeback[0] = find_nearest_similar(item.in_image, prototype, item.worker, item.x, item.y, item.blk);
    }
    return 0;
}

void process_image(final_image in_image, final_image in_palette, const char* outPath) {
    const unsigned pw = (in_palette->width >> BLOCK_SIZE_LOG) << BLOCK_SIZE_LOG;
    const unsigned ph = (in_palette->height >> BLOCK_SIZE_LOG) << BLOCK_SIZE_LOG;
    const unsigned iw = (in_image->width >> BLOCK_SIZE_LOG) << BLOCK_SIZE_LOG;
    const unsigned ih = (in_image->height >> BLOCK_SIZE_LOG) << BLOCK_SIZE_LOG;
    const unsigned blockCount = (pw * ph) / BLOCK_SIZE_SQ;
    const unsigned allocSize = BLOCK_SIZE_SQ;
    unsigned x = 0, y = 0, i = 0, j = 0, m = 0;
    unsigned* results = calloc(1, sizeof(unsigned) * (iw * ih) / BLOCK_SIZE_SQ);
    struct image output;

    output.width = iw;
    output.height = ih;
    output.data = malloc(sizeof(int) * ih * iw);

    if (!prototypes) {
        prototypes = malloc(sizeof(real) * TRANSFORMS * blockCount * PROTOTYPE_SIZE);
        buckets = malloc(sizeof(int) * TRANSFORMS * blockCount * BLOCK_SIZE_SQ);
        for (y = 0; y < ph; y += BLOCK_SIZE) {
            for (x = 0; x < pw; x += BLOCK_SIZE) {
                for (i = 0; i < TRANSFORMS; i++) {
                    transform_block(
                        in_palette,
                        x, y,
                        prototypes + total_blocks * PROTOTYPE_SIZE,
                        buckets + total_blocks * BLOCK_SIZE_SQ,
                        i
                    );
                    total_blocks++;
                }
            }
        }
    }

    for (i = 0; i < WORKERS; i++) {
        work_size[i] = 0;
        work[i] = malloc(sizeof(struct worker_call) * (iw * ih) / BLOCK_SIZE_SQ);
#ifdef AGGRESSIVE_VECTORISATION
        similarities[i] = malloc(sizeof(double) * total_blocks);
#endif
    }

    for (y = 0; y < ih; y += BLOCK_SIZE) {
        for (x = 0; x < iw; x += BLOCK_SIZE, m++) {
            struct worker_call item = {
                in_image,
                m % WORKERS,
                x,
                y,
                total_blocks,
                results + m
            };
            memcpy(&work[m % WORKERS][work_size[m % WORKERS]++], &item, sizeof(struct worker_call));
        }
    }

#ifdef _WIN32
    HANDLE threads[WORKERS];
    for (i = 1; i < WORKERS; i++)
        threads[i] = CreateThread(NULL, 65536, (LPTHREAD_START_ROUTINE)start_worker, (void*)(uintptr_t)i, 0, NULL);

    start_worker((void*)0);

    for (i = 1; i < WORKERS; i++)
        WaitForSingleObject(threads[i], INFINITE);
#else
    thrd_t threads[WORKERS];
    for (i = 1; i < WORKERS; i++)
        thrd_create(&threads[i], start_worker, (void*)(uintptr_t)i);

    start_worker((void*)0);

    for (i = 1; i < WORKERS; i++)
        thrd_join(threads[i], NULL);
#endif

    for (i = 0; i < WORKERS; i++) {
        free(work[i]);
#ifdef AGGRESSIVE_VECTORISATION
        free(similarities[i]);
#endif
    }

    m = 0;
    for (y = 0; y < ih; y += BLOCK_SIZE) {
        for (x = 0; x < iw; x += BLOCK_SIZE, m++) {
            for (j = 0; j < BLOCK_SIZE; j++) {
                for (i = 0; i < BLOCK_SIZE; i++) {
                    int col = buckets[results[m] * BLOCK_SIZE_SQ + j * BLOCK_SIZE + i];
                    output.data[(y + j) * iw + x + i] = col;
                }
            }
        }
    }

    save_image(&output, outPath);
    free(output.data);
    free(results);
}

void transform_yuv2(const unsigned l, mutable_real col) {
    col[0] = M_PI * (0.299 * ((l >> 16) & 255) + 0.587 * ((l >> 8) & 255) + 0.114 * (l & 255)) / 255.0;
    col[1] = cos(col[0]);
    col[0] = sin(col[0]);
}

void transform_rgb6(const unsigned l, mutable_real col) {
    col[0] = M_PI * ((l >> 16) & 255) / REAL_255;
    col[1] = M_PI * ((l >> 8) & 255) / REAL_255;
    col[2] = M_PI * (l & 255) / REAL_255;
#if defined(__GNUC__) && defined(SINCOS_MATH)
    sincos(col[0], col, col + 3);
    sincos(col[1], col + 1, col + 4);
    sincos(col[2], col + 2, col + 5);
#else
    col[3] = cos(col[0]);
    col[4] = cos(col[1]);
    col[5] = cos(col[2]);
    col[0] = sin(col[0]);
    col[1] = sin(col[1]);
    col[2] = sin(col[2]);
#endif
}

int main(int argc, const char** argv) {
    struct image img;
    struct image pattern;
    char in_image_f[255];
    char out_image_f[255];
    int convert = 0;

    const char* in_image = "1.bmp";
    const char* in_pattern = "2.bmp";
    const char* out_image = "3.bmp";
    int i = 0, run = 1;

    for (i = 1; i < argc; i++) {
        const char* prev = argv[i - 1];
        if (!strcmp(prev, "--in"))
            in_image = argv[i];
        else if (!strcmp(prev, "--pattern"))
            in_pattern = argv[i];
        else if (!strcmp(prev, "--out"))
            out_image = argv[i];
        else if (!strcmp(prev, "--convert"))
            convert = atoi(argv[i]);
    }

    load_image(&pattern, in_pattern);
    for (i = 1; run; i++) {
        memset(in_image_f, 0, sizeof(in_image_f));
        memset(out_image_f, 0, sizeof(out_image_f));
        if (strstr(in_image, "%") != NULL) {
            snprintf(in_image_f, sizeof(in_image_f) - 1, in_image, i);
            snprintf(out_image_f, sizeof(out_image_f) - 1, out_image, i);
        }
        else {
            strncpy(in_image_f, in_image, sizeof(in_image_f) - 1);
            strncpy(out_image_f, out_image, sizeof(out_image_f) - 1);
            run = 0;
        }
        if (access(in_image_f, F_OK) != 0) break;

        real start = get_time();
        load_image(&img, in_image_f);
        if (convert) {
            save_image(&img, out_image_f);
        } else {
            process_image(&img, &pattern, out_image_f);
        }
        release_image(&img);
        printf("Processed frame %d, (%s, %s) => %s (+%f)\n", i, in_image_f, in_pattern, out_image_f, get_time() - start);
    }
    release_image(&pattern);
}