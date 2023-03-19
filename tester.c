#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <err.h>
#include <assert.h>

#include "jbod.h"
#include "mdadm.h"
#include "util.h"
#include "tester.h"

#define TESTER_ARGUMENTS "hw:"
#define USAGE                                               \
  "USAGE: test [-h] [-w workload-file]\n"                   \
  "\n"                                                      \
  "where:\n"                                                \
  "    -h - help mode (display this message)\n"             \
  "\n"                                                      \

/* Test functions. */
int test_mount_unmount();
int test_read_before_mount();
int test_read_invalid_parameters();
int test_read_within_block();
int test_read_across_blocks();
int test_read_three_blocks();
int test_read_across_disks();

/* New test functions for the assignment 3. */
int test_write_before_mount();
int test_write_invalid_parameters();
int test_write_within_block();
int test_write_across_blocks();
int test_write_three_blocks();
int test_write_across_disks();

/* Utility functions. */
char *stringify(const uint8_t *buf, int length) {
  char *p = (char *)malloc(length * 6);
  for (int i = 0, n = 0; i < length; ++i) {
    if (i && i % 16 == 0)
      n += sprintf(p + n, "\n");
    n += sprintf(p + n, "0x%02x ", buf[i]);
  }
  return p;
}

int run_workload(char *workload);

int main(int argc, char *argv[])
{
  int ch;
  char *workload = NULL;

  while ((ch = getopt(argc, argv, TESTER_ARGUMENTS)) != -1) {
    switch (ch) {
      case 'h':
        fprintf(stderr, USAGE);
        return 0;
      case 'w':
        workload = optarg;
        break;
      default:
        fprintf(stderr, "Unknown command line option (%c), aborting.\n", ch);
        return -1;
    }
  }

  if (workload) {
    run_workload(workload);
    return 0;
  }
    
  int score = 0;

  score += test_mount_unmount();
  score += test_read_before_mount();
  score += test_read_invalid_parameters();
  score += test_read_within_block();
  score += test_read_across_blocks();
  score += test_read_three_blocks();
  score += test_read_across_disks();

  score += test_write_before_mount();
  score += test_write_invalid_parameters();
  score += test_write_within_block();
  score += test_write_across_blocks();
  score += test_write_three_blocks();
  score += test_write_across_disks();

  printf("Total score: %d/%d\n", score, 17);

  return 0;
}

int test_mount_unmount() {
  printf("running %s: ", __func__);

  int rc = mdadm_mount();
  if (rc != 1) {
    printf("failed: mount should succeed on an unmounted system but it failed.\n");
    return 0;
  }

  rc = mdadm_mount();
  if (rc == 1) {
    printf("failed: mount should fail on an already mounted system but it succeeded.\n");
    return 0;
  }

  if (rc != -1) {
    printf("failed: mount should return -1 on failure but returned %d\n", rc);
    return 0;
  }

  rc = mdadm_unmount();
  if (rc != 1) {
    printf("failed: unmount should succeed on a mounted system but it failed.\n");
    return 0;
  }

  rc = mdadm_unmount();
  if (rc == 1) {
    printf("failed: unmount should fail on an already unmounted system but it succeeded.\n");
    return 0;
  }

  if (rc != -1) {
    printf("failed: unmount should return -1 on failure but returned %d\n", rc);
    return 0;
  }

  printf("passed\n");
  return 3;
}

#define SIZE 16

int test_read_before_mount() {
  printf("running %s: ", __func__);

  uint8_t buf[SIZE];
  if (mdadm_read(0, SIZE, buf) != -1) {
    printf("failed: read should fail on an umounted system but it did not.\n");
    return 0;
  }

  printf("passed\n");
  return 1;
}

int test_read_invalid_parameters() {
  printf("running %s: ", __func__);

  mdadm_mount();

  bool success = false;
  uint8_t buf1[SIZE];
  uint32_t addr = 0x1fffffff;
  if (mdadm_read(addr, SIZE, buf1) != -1) {
    printf("failed: read should fail on an out-of-bound linear address but it did not.\n");
    goto out;
  }

  addr = 1048570;
  if (mdadm_read(addr, SIZE, buf1) != -1) {
    printf("failed: read should fail if it goes beyond the end of the linear address space but it did not.\n");
    goto out;
  }

  uint8_t buf2[2048];
  if (mdadm_read(0, sizeof(buf2), buf2) != -1) {
    printf("failed: read should fail on larger than 1024-byte I/O sizes but it did not.\n");
    goto out;
  }

  if (mdadm_read(0, SIZE, NULL) != -1) {
    printf("failed: read should fail when passed a NULL pointer and non-zero length but it did not.\n");
    goto out;
  }

  if (mdadm_read(0, 0, NULL) != 0) {
    printf("failed: 0-length read should succeed with a NULL pointer but it did not.\n");
    goto out;
  }
  success = true;

out:
  mdadm_unmount();
  if (!success)
    return 0;

  printf("passed\n");
  return 1;
}

/*
 * This test reads the first 16 bytes of the linear address, which corresponds
 * to the first 16 bytes of the 0th block of the 0th disk.
 */
int test_read_within_block() {
  printf("running %s: ", __func__);

  mdadm_mount();

  /* Set the contents of JBOD drives to a specific pattern. */
  jbod_initialize_drives_contents();

  bool success = false;
  uint8_t out[SIZE];
  if (mdadm_read(0, SIZE, out) != SIZE) {
    printf("failed: read failed\n");
    return 0;
  }

  uint8_t expected[SIZE] = {
    0xaa, 0xaa, 0xaa, 0xaa,
    0xaa, 0xaa, 0xaa, 0xaa,
    0xaa, 0xaa, 0xaa, 0xaa,
    0xaa, 0xaa, 0xaa, 0xaa,
  };

  if (memcmp(out, expected, SIZE) != 0) {
    char *out_s = stringify(out, SIZE);
    char *expected_s = stringify(expected, SIZE);

    printf("failed:\n  got:      %s\n  expected: %s\n", out_s, expected_s);

    free(out_s);
    free(expected_s);
    goto out;
  }
  success = true;

out:
  mdadm_unmount();
  if (!success)
    return 0;

  printf("passed\n");
  return 1;
}

/*
 * This test reads 16 bytes starting at the linear address 248, which
 * corresponds to the last 8 bytes of the 0th block and first 8 bytes of the 1st
 * block, both on the 0th disk.
 */
int test_read_across_blocks() {
  printf("running %s: ", __func__);

  mdadm_mount();

  /* Set the contents of JBOD drives to a specific pattern. */
  jbod_initialize_drives_contents();

  bool success = false;
  uint8_t out[SIZE];
  if (mdadm_read(248, SIZE, out) != SIZE) {
    printf("failed: read failed\n");
    goto out;
  }

  uint8_t expected[SIZE] = {
    0xaa, 0xaa, 0xaa, 0xaa,
    0xaa, 0xaa, 0xaa, 0xaa,
    0xbb, 0xbb, 0xbb, 0xbb,
    0xbb, 0xbb, 0xbb, 0xbb,
  };

  if (memcmp(out, expected, SIZE) != 0) {
    char *out_s = stringify(out, SIZE);
    char *expected_s = stringify(expected, SIZE);

    printf("failed:\n  got:      %s\n  expected: %s\n", out_s, expected_s);

    free(out_s);
    free(expected_s);
    goto out;
  }
  success = true;

out:
  mdadm_unmount();
  if (!success)
    return 0;

  printf("passed\n");
  return 1;
}

/*
 * This test reads 258 bytes starting at the linear address 255, which
 * corresponds to the last byte of the 0th block, all bytes of the 1st block,
 * and the first byte of the 2nd block, where all blocks are the 0th disk.
 */

#define TEST3_SIZE 258

int test_read_three_blocks() {
  printf("running %s: ", __func__);

  mdadm_mount();

  /* Set the contents of JBOD drives to a specific pattern. */
  jbod_initialize_drives_contents();

  bool success = false;
  uint8_t out[TEST3_SIZE];
  if (mdadm_read(255, TEST3_SIZE, out) != TEST3_SIZE) {
    printf("failed: read failed\n");
    goto out;
  }

  uint8_t expected[TEST3_SIZE] = {
    0xaa, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb,
    0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb,
    0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb,
    0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb,
    0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb,
    0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb,
    0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb,
    0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb,
    0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb,
    0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb,
    0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb,
    0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb,
    0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb,
    0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb,
    0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb,
    0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb,
    0xbb, 0xcc,
  };

  if (memcmp(out, expected, TEST3_SIZE) != 0) {
    char *out_s = stringify(out, TEST3_SIZE);
    char *expected_s = stringify(expected, TEST3_SIZE);

    printf("failed:\n  got:\n%s\n  expected:\n%s\n", out_s, expected_s);

    free(out_s);
    free(expected_s);
    goto out;
  }
  success = true;

out:
  mdadm_unmount();
  if (!success)
    return 0;

  printf("passed\n");
  return 1;
}

/*
 * This test reads 16 bytes starting at the linear address 983032, which
 * corresponds to the last 8 bytes of disk 14 and first 8 bytes on disk 15.
 */
int test_read_across_disks() {
  printf("running %s: ", __func__);

  mdadm_mount();

  /* Set the contents of JBOD drives to a specific pattern. */
  jbod_initialize_drives_contents();

  bool success = false;
  uint8_t out[SIZE];
  if (mdadm_read(983032, SIZE, out) != SIZE) {
    printf("failed: read failed\n");
    goto out;
  }

  uint8_t expected[SIZE] = {
    0xee, 0xee, 0xee, 0xee,
    0xee, 0xee, 0xee, 0xee,
    0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff,
  };

  if (memcmp(out, expected, SIZE) != 0) {
    char *out_s = stringify(out, SIZE);
    char *expected_s = stringify(expected, SIZE);

    printf("failed:\n  got:\n%s\n  expected:\n%s\n", out_s, expected_s);

    free(out_s);
    free(expected_s);
    goto out;
  }
  success = true;

out:
  mdadm_unmount();
  if (!success)
    return 0;

  printf("passed\n");
  return 2;
}

int test_write_before_mount() {
  printf("running %s: ", __func__);

  uint8_t buf[SIZE];
  if (mdadm_write(0, SIZE, buf) != -1) {
    printf("failed: write should fail on an umounted system but it did not.\n");
    return 0;
  }

  printf("passed\n");
  return 1;
}

int test_write_invalid_parameters() {
  printf("running %s: ", __func__);

  mdadm_mount();

  bool success = false;
  uint8_t buf1[SIZE];
  uint32_t addr = 0x1fffffff;
  if (mdadm_write(addr, SIZE, buf1) != -1) {
    printf("failed: write should fail on an out-of-bound linear address but it did not.\n");
    goto out;
  }

  addr = 1048560;
  if (mdadm_write(addr, SIZE, buf1) == -1) {
    printf("failed: write should succeed because it is within the linear address space but it failed.\n");
    goto out;
  }

  addr = 1048561;
  if (mdadm_write(addr, SIZE, buf1) != -1) {
    printf("failed: write should fail if it goes beyond the end of the linear address space but it did not.\n");
    goto out;
  }

  uint8_t buf2[2048];
  if (mdadm_write(0, sizeof(buf2), buf2) != -1) {
    printf("failed: write should fail on larger than 1024-byte I/O sizes but it did not.\n");
    goto out;
  }

  if (mdadm_write(0, SIZE, NULL) != -1) {
    printf("failed: write should fail when passed a NULL pointer and non-zero length but it did not.\n");
    goto out;
  }

  if (mdadm_write(0, 0, NULL) != 0) {
    printf("failed: 0-length write should succeed with a NULL pointer but it did not.\n");
    goto out;
  }
  success = true;

out:
  mdadm_unmount();
  if (!success)
    return 0;

  printf("passed\n");
  return 1;
  
}

/*
 * This test writes 16 bytes starting at the linear address 256, which
 * corresponds to the first 16 bytes of the 1st block of the 0th disk.
 */
int test_write_within_block() {
  printf("running %s: ", __func__);

  mdadm_mount();

  /* Set the contents of JBOD drives to a specific pattern. */
  jbod_initialize_drives_contents();

  bool success = false;
  const uint8_t expected[JBOD_BLOCK_SIZE] = {
    0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa,
    0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb,
    0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb,
    0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb,
    0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb,
    0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb,
    0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb,
    0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb,
    0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb,
    0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb,
    0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb,
    0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb,
    0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb,
    0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb,
    0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb,
    0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb,

  };

  // Making a copy of expected output to be written
  uint8_t to_write[JBOD_BLOCK_SIZE];
  memcpy(to_write, expected, JBOD_BLOCK_SIZE);
  
  /* Write only the first SIZE bytes of the buffer |expected| at address 256. */
  if (mdadm_write(256, SIZE,to_write) != SIZE) {
    printf("failed: write failed\n");
    return 0;
  }

  uint8_t out[JBOD_BLOCK_SIZE] = {0};
  /* This call reads raw disk contents into out buffer according to the
   * requirements of this test. */
  jbod_fill_block_test_write_within_block(out);

  if (memcmp(out, expected, JBOD_BLOCK_SIZE) != 0) {
    char *out_s = stringify(out, JBOD_BLOCK_SIZE);
    char *expected_s = stringify(expected, JBOD_BLOCK_SIZE);

    printf("failed:\n  got:      %s\n  expected: %s\n", out_s, expected_s);

    free(out_s);
    free(expected_s);
    goto out;
  }
  success = true;

out:
  mdadm_unmount();
  if (!success)
    return 0;

  printf("passed\n");
  return 1;
}

/*
 * This test writes 16 bytes starting at the linear address 327928, which
 * corresponds to the last 8 bytes of block 0 of disk 5 and the first 8 bytes of
 * block 1 of disk 5.
 */
int test_write_across_blocks() {
  printf("running %s: ", __func__);

  mdadm_mount();

  bool success = false;
  const uint8_t expected[SIZE] = {
    0xaa, 0xaa, 0xaa, 0xaa,
    0xaa, 0xaa, 0xaa, 0xaa,
    0xbb, 0xbb, 0xbb, 0xbb,
    0xbb, 0xbb, 0xbb, 0xbb,
  };

  // Making a copy of expected output to be written
  uint8_t to_write[SIZE];
  memcpy(to_write, expected, SIZE);
  
  if (mdadm_write(327928, SIZE, to_write) != SIZE) {
    printf("failed: write failed\n");
    return 0;
  }

  uint8_t out[SIZE];
  /* This call reads raw disk contents into out buffer according to the
   * requirements of this test. */
  jbod_fill_block_test_write_across_blocks(out);

  if (memcmp(out, expected, SIZE) != 0) {
    char *out_s = stringify(out, SIZE);
    char *expected_s = stringify(expected, SIZE);

    printf("failed:\n  got:      %s\n  expected: %s\n", out_s, expected_s);

    free(out_s);
    free(expected_s);
    goto out;
  }
  success = true;

out:
  mdadm_unmount();
  if (!success)
    return 0;

  printf("passed\n");
  return 1;
}

/*
 * This test writes 258 bytes starting at the linear address 528383, which
 * corresponds to the last byte of 15th block of disk 8, all off the 16th block
 * of disk 8, and the first byte of the 17th block of disk 8.
 */
int test_write_three_blocks() {
  printf("running %s: ", __func__);

  mdadm_mount();

  bool success = false;
  const uint8_t expected[TEST3_SIZE] = {
    0xaa, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb,
    0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb,
    0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb,
    0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb,
    0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb,
    0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb,
    0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb,
    0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb,
    0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb,
    0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb,
    0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb,
    0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb,
    0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb,
    0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb,
    0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb,
    0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb,
    0xbb, 0xcc,
  };

	
  // Making a copy of expected output to be written
  uint8_t to_write[TEST3_SIZE];
  memcpy(to_write, expected, TEST3_SIZE);

  if (mdadm_write(528383, TEST3_SIZE, to_write) != TEST3_SIZE) {
    printf("failed: write failed\n");
    goto out;
  }

  uint8_t out[TEST3_SIZE];
  /* This call reads raw disk contents into out buffer according to the
   * requirements of this test. */
  jbod_fill_block_test_write_three_blocks(out);

  if (memcmp(out, expected, TEST3_SIZE) != 0) {
    char *out_s = stringify(out, TEST3_SIZE);
    char *expected_s = stringify(expected, TEST3_SIZE);

    printf("failed:\n  got:\n%s\n  expected:\n%s\n", out_s, expected_s);

    free(out_s);
    free(expected_s);
    goto out;
  }
  success = true;

out:
  mdadm_unmount();
  if (!success)
    return 0;

  printf("passed\n");
  return 1;
}

/*
 * This test writes 16 bytes starting at the linear address 917496, which
 * corresponds to the last 8 bytes of disk 13 and first 8 bytes on disk 14.
 */
int test_write_across_disks() {
  printf("running %s: ", __func__);

  mdadm_mount();

  bool success = false;
  const uint8_t expected[SIZE] = {
    0xee, 0xee, 0xee, 0xee,
    0xee, 0xee, 0xee, 0xee,
    0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff,
  };


  // Making a copy of expected output to be written
  uint8_t to_write[SIZE];
  memcpy(to_write, expected, SIZE);
 
  if (mdadm_write(917496, SIZE, to_write) != SIZE) {
    printf("failed: write failed\n");
    goto out;
  }

  uint8_t out[SIZE];
  /* This call reads raw disk contents into out buffer according to the
   * requirements of this test. */
  jbod_fill_block_test_write_across_disks(out);

  if (memcmp(out, expected, SIZE) != 0) {
    char *out_s = stringify(out, SIZE);
    char *expected_s = stringify(expected, SIZE);

    printf("failed:\n  got:\n%s\n  expected:\n%s\n", out_s, expected_s);

    free(out_s);
    free(expected_s);
    goto out;
  }
  success = true;

out:
  mdadm_unmount();
  if (!success)
    return 0;

  printf("passed\n");
  return 2;
}

int equals(const char *s1, const char *s2) {
  return strncmp(s1, s2, strlen(s2)) == 0;
}

int run_workload(char *workload) {
  char line[256], cmd[32];
  uint8_t buf[MAX_IO_SIZE];
  uint32_t addr, len, ch;
  int rc;

  memset(buf, 0, MAX_IO_SIZE);

  FILE *f = fopen(workload, "r");
  if (!f)
    err(1, "Cannot open workload file %s", workload);

  int line_num = 0;
  while (fgets(line, 256, f)) {
    ++line_num;
    line[strlen(line)-1] = '\0';
    if (equals(line, "MOUNT")) {
      rc = mdadm_mount();
    } else if (equals(line, "UNMOUNT")) {
      rc = mdadm_unmount();
    } else if (equals(line, "SIGNALL")) {
      for (int i = 0; i < JBOD_NUM_DISKS; ++i)
        for (int j = 0; j < JBOD_NUM_BLOCKS_PER_DISK; ++j)
          jbod_sign_block(i, j);
    } else {
      if (sscanf(line, "%7s %7u %4u %3u", cmd, &addr, &len, &ch) != 4)
        errx(1, "Failed to parse command: [%s\n], aborting.", line);
      if (equals(cmd, "READ")) {
        rc = mdadm_read(addr, len, buf);
      } else if (equals(cmd, "WRITE")) {
        memset(buf, ch, len);
        rc = mdadm_write(addr, len, buf);
      } else {
        errx(1, "Unknown command [%s] on line %d, aborting.", line, line_num);
      }
    }

    if (rc == -1)
      errx(1, "tester failed when processing command [%s] on line %d", line, line_num);
  }
  fclose(f);
  return 0;
}
