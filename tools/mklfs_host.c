/* SPDX-License-Identifier: Apache-2.0 */
#include "lfs.h"

#include <errno.h>
#include <fts.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { BLOCK_SIZE = 4096, PROGRAM_SIZE = 256 };

static int image_read(const struct lfs_config *cfg, lfs_block_t block,
                      lfs_off_t offset, void *buffer, lfs_size_t size)
{
    const uint8_t *image = cfg->context;
    memcpy(buffer, image + (size_t)block * cfg->block_size + offset, size);
    return 0;
}

static int image_program(const struct lfs_config *cfg, lfs_block_t block,
                         lfs_off_t offset, const void *buffer, lfs_size_t size)
{
    uint8_t *image = cfg->context;
    memcpy(image + (size_t)block * cfg->block_size + offset, buffer, size);
    return 0;
}

static int image_erase(const struct lfs_config *cfg, lfs_block_t block)
{
    uint8_t *image = cfg->context;
    memset(image + (size_t)block * cfg->block_size, 0xff, cfg->block_size);
    return 0;
}

static int image_sync(const struct lfs_config *cfg)
{
    (void)cfg;
    return 0;
}

static void check_lfs(int result, const char *operation, const char *path)
{
    if (result < 0) {
        fprintf(stderr, "LittleFS %s %s: %d\n", operation, path, result);
        exit(EXIT_FAILURE);
    }
}

static void io_error(const char *operation, const char *path)
{
    fprintf(stderr, "%s %s: %s\n", operation, path, strerror(errno));
    exit(EXIT_FAILURE);
}

static void copy_file(lfs_t *lfs, const char *path, const char *name)
{
    FILE *input = fopen(path, "rb");
    if (!input)
        io_error("Open", path);
    lfs_file_t file;
    check_lfs(lfs_file_open(lfs, &file, name, LFS_O_WRONLY | LFS_O_CREAT | LFS_O_EXCL),
              "open", name);
    uint8_t buffer[BLOCK_SIZE];
    size_t count;
    while ((count = fread(buffer, 1, sizeof(buffer), input)) > 0) {
        lfs_ssize_t written = lfs_file_write(lfs, &file, buffer, count);
        check_lfs(written, "write", name);
        check_lfs(written == (lfs_ssize_t)count ? 0 : LFS_ERR_IO, "write", name);
    }
    if (ferror(input))
        io_error("Read", path);
    if (fclose(input))
        io_error("Close", path);
    check_lfs(lfs_file_close(lfs, &file), "close", name);
}

static int compare_names(const FTSENT **left, const FTSENT **right)
{
    return strcmp((*left)->fts_name, (*right)->fts_name);
}

static void copy_tree(lfs_t *lfs, char *directory)
{
    char *paths[] = {directory, NULL};
    FTS *tree = fts_open(paths, FTS_PHYSICAL | FTS_NOCHDIR, compare_names);
    if (!tree)
        io_error("Traverse", directory);
    size_t prefix = strlen(directory) + 1;
    for (;;) {
        errno = 0;
        FTSENT *entry = fts_read(tree);
        if (!entry) {
            if (errno)
                io_error("Traverse", directory);
            break;
        }
        if (entry->fts_info == FTS_DP)
            continue;
        if (entry->fts_level == FTS_ROOTLEVEL) {
            if (entry->fts_info != FTS_D) {
                fprintf(stderr, "Invalid LittleFS source directory: %s\n", directory);
                exit(EXIT_FAILURE);
            }
            continue;
        }
        const char *name = entry->fts_path + prefix;
        if (entry->fts_info == FTS_D)
            check_lfs(lfs_mkdir(lfs, name), "mkdir", name);
        else if (entry->fts_info == FTS_F)
            copy_file(lfs, entry->fts_accpath, name);
        else {
            fprintf(stderr, "Cannot copy %s to LittleFS\n", entry->fts_path);
            exit(EXIT_FAILURE);
        }
    }
    if (fts_close(tree))
        io_error("Close directory", directory);
}

int main(int argc, char **argv)
{
    if (argc != 4) {
        fprintf(stderr, "Usage: mklfsimg-host SOURCE OUTPUT SIZE_BYTES\n");
        return EXIT_FAILURE;
    }
    char *end;
    errno = 0;
    unsigned long long bytes = strtoull(argv[3], &end, 10);
    if (errno || *end || bytes < 2 * BLOCK_SIZE || bytes % BLOCK_SIZE ||
        bytes > SIZE_MAX || bytes / BLOCK_SIZE > UINT32_MAX) {
        fprintf(stderr, "Invalid LittleFS image size: %s\n", argv[3]);
        return EXIT_FAILURE;
    }
    size_t size = (size_t)bytes;
    uint8_t *image = malloc(size);
    if (!image)
        io_error("Allocate image for", argv[2]);
    memset(image, 0xff, size);
    const struct lfs_config config = {
        .context = image, .read = image_read, .prog = image_program,
        .erase = image_erase, .sync = image_sync,
#ifdef LFS_THREADSAFE
        /* All image operations run on the same thread. */
        .lock = image_sync, .unlock = image_sync,
#endif
        .read_size = 32, .prog_size = PROGRAM_SIZE, .block_size = BLOCK_SIZE,
        .block_count = size / BLOCK_SIZE, .cache_size = PROGRAM_SIZE,
        .lookahead_size = 128, .block_cycles = -1,
    };
    lfs_t lfs;
    check_lfs(lfs_format(&lfs, &config), "format", argv[2]);
    check_lfs(lfs_mount(&lfs, &config), "mount", argv[2]);
    copy_tree(&lfs, argv[1]);
    check_lfs(lfs_unmount(&lfs), "unmount", argv[2]);
    FILE *output = fopen(argv[2], "wb");
    if (!output)
        io_error("Create", argv[2]);
    if (fwrite(image, 1, size, output) != size)
        io_error("Write", argv[2]);
    if (fclose(output))
        io_error("Close", argv[2]);
    free(image);
    return EXIT_SUCCESS;
}
