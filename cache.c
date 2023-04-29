#include "cache.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static cache_entry_t *cache = NULL;
static int cache_size = 0;
static int clock = 0;
static int num_queries = 0;
static int num_hits = 0;

// it keeps track of whether the cache has been created or destroyed (0 or 1)
int cache_intialized = 0;

// it keeps track of whether any entries have been inserted into the cache (0 or 1)
int cache_populated = 0;

int cache_create(int num_entries) {

    // checks for failures from test_cache_create_destroy()
    if (num_entries < 2 || num_entries > 4096) {
        return -1;
    }

    // if cache is not created, then start with the create operation by dynamically allocating space for cache
    if (cache_intialized == 0) {
        cache = calloc(num_entries, sizeof(cache_entry_t));
        cache_size = num_entries;
        cache_intialized = 1;
        return 1;
    }
    return -1;
}

int cache_destroy(void) {

    // if cache has already been created, then start with the destroying operation
    if (cache_intialized == 1) {
        free(cache);
        cache = NULL;
        cache_size = 0;
        cache_intialized = 0;
        cache_populated = 0;
        clock = 0;
        return 1;
    }

    return -1;
}

int cache_lookup(int disk_num, int block_num, uint8_t *buf) {

    // checks if anything has been inserted in cache
    if (cache_populated == 0) {
        return -1;
    }

    num_queries++;
    for (int i = 0; i < cache_size; i++) {

        // lookup the block identified by disk_num and block_num in the cache; if found then copy the block into buf (!= NULL)
        if ((cache[i].disk_num == disk_num) && (cache[i].block_num == block_num) && (buf != NULL)) {
            num_hits++;
            clock++;
            cache[i].access_time = clock;
            memcpy(buf, cache[i].block, JBOD_BLOCK_SIZE);
            return 1;
        }
    }

    return -1;
}

int cache_insert(int disk_num, int block_num, const uint8_t *buf) {

    int location = -1;
    int lowest_access_time;

    // checks for failures from test_cache_invalid_parameters()
    if (cache_intialized == 0 || buf == NULL || cache_size == 0) {
        return -1;
    }
    if (disk_num > 16 || disk_num < 0 || block_num > 256 || block_num < 0) {
        return -1;
    }

    // indicates that cache has at least one valid entry
    cache_populated = 1;

    // using linear search to find an empty slot in the cache
    for (int i = 0; i < cache_size; i++) {

        // inserting an entry with the same disk_num and block_num should fail
        if ((cache[i].disk_num == disk_num) && (cache[i].block_num == block_num) && (block_num != 0) && (disk_num != 0)) {
            return -1;
        }

        if (cache[i].valid == 0) {
            location = i;
            break;
        }
    }

    // if no empty slot is found
    if (location == -1) {

        lowest_access_time = cache[0].access_time;
        location = 0;

        // determine the entry with the smallest access_time and replace it with the new block of data
        for (int i = 1; i < cache_size; i++) {
            if (cache[i].access_time < lowest_access_time) {
                lowest_access_time = cache[i].access_time;
                location = i;
            }
        }
    }

    // copy the buffer bef into the block of the corresponding entry in the cache
    memcpy(cache[location].block, buf, JBOD_BLOCK_SIZE);

    // update disk_num and block_num of the corresponding entry in the cache
    cache[location].disk_num = disk_num;
    cache[location].block_num = block_num;

    // indicates that the block has valid data
    cache[location].valid = 1;

    // increment the clock
    clock++;

    // set the access_time of the corresponding entry to the current clock
    cache[location].access_time = clock;

    return 1;
}

void cache_update(int disk_num, int block_num, const uint8_t *buf) {

    for (int i = 0; i < cache_size; i++) {

        // if the entry exists in cache, updates its block content with the new data in buf, also update the access_time
        if ((cache[i].disk_num == disk_num) && (cache[i].block_num == block_num)) {
            memcpy(cache[i].block, buf, JBOD_BLOCK_SIZE);
            clock++;
            cache[i].access_time = clock;
        }
    }
}

bool cache_enabled(void) {
    if ((cache != NULL) && (cache_size > 0)) {
        return true;
    }
    return false;
}

void cache_print_hit_rate(void) { fprintf(stderr, "Hit rate: %5.1f%%\n", 100 * (float)num_hits / num_queries); }
