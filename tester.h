#ifndef TEST_H_
#define TEST_H_

#include "jbod.h"

void jbod_fill_block_test_write_within_block(uint8_t *buf);
void jbod_fill_block_test_write_across_blocks(uint8_t *buf);
void jbod_fill_block_test_write_three_blocks(uint8_t *buf);
void jbod_fill_block_test_write_across_disks(uint8_t *buf);

int jbod_sign_block(int disk_num, int block_num);
void jbod_initialize_drives_contents();
void jbod_print_cost(void);

#define MAX_IO_SIZE 1024

#endif
