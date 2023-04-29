#include "mdadm.h"
#include "jbod.h"
#include "net.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

uint32_t encode_op(int cmd, int disk_num, int reserved, int block_num) {
    uint32_t op = 0;
    op = op | (cmd << 26) | (disk_num << 22) | (reserved << 8) | (block_num);
    return op;
};

// mount = 1 -> mounted
// mount = 0 -> unmounted
int mount = 0;

int mdadm_mount(void) {

    // check if already mounted
    if (mount == 1) {
        return -1;
    }

    // if not mounted, we need to do the JBOD operation
    else {
        uint32_t op = encode_op(JBOD_MOUNT, 0, 0, 0);
        int rc = jbod_client_operation(op, NULL);

        if (rc == 0) {
            mount = 1;
            return 1;
        }
        return -1;
    };
}

int mdadm_unmount(void) {

    // check if it is already unmounted
    if (mount == 0) {
        return -1;
    }

    // function unmounts a mounted bundle of disks
    else {
        uint32_t op = encode_op(JBOD_UNMOUNT, 0, 0, 0);
        int rc = jbod_client_operation(op, NULL);

        if (rc == 0) {
            mount = 0;
            return 1;
        }
        return -1;
    };
}

// this function seeks to the specified disk and block number using jbod_client_operation
void seek(int disk_number, int block_number) {
    jbod_client_operation(encode_op(JBOD_SEEK_TO_DISK, disk_number, 0, 0), NULL);   // seek to disk_num
    jbod_client_operation(encode_op(JBOD_SEEK_TO_BLOCK, 0, 0, block_number), NULL); // seek to block_num
};

// translate a given linear address into disk number, block number, and offset within that block
void translate_address(uint32_t linear_addr, int *disk_num, int *block_num, int *offset) {
    *disk_num = linear_addr / JBOD_DISK_SIZE;
    *block_num = (linear_addr % JBOD_DISK_SIZE) / JBOD_BLOCK_SIZE;
    *offset = (linear_addr % JBOD_DISK_SIZE) % JBOD_BLOCK_SIZE;
}

int mdadm_read(uint32_t addr, uint32_t len, uint8_t *buf) {
    uint32_t end_of_the_linear_address_space = 1048570;

    // checks for failures from read_invalid_parameters()
    if ((len > 1024) || (buf == NULL && len > 0) || ((addr + len) > end_of_the_linear_address_space) || (mount == 0)) {
        return -1;
    }

    int current_address = addr;
    int block_count = 0; // how many blocks read
    int n = len;
    int copy_of_len = len; // keeps track of how much data have been read
    int disk_num, block_num, offset;

    while (current_address < addr + len) {

        translate_address(current_address, &disk_num, &block_num, &offset); // translate address
        seek(disk_num, block_num);                                          // seek to correct disk num and block num

        uint8_t tmp[JBOD_BLOCK_SIZE];
        int result = cache_lookup(disk_num, block_num, buf);
        if (result == -1) {
            jbod_client_operation(encode_op(JBOD_READ_BLOCK, 0, 0, 0), tmp);
            cache_insert(disk_num, block_num, buf);
        }

        // read the first block
        if (block_count == 0) {
            if (len + offset <= JBOD_BLOCK_SIZE) { // fits in one block
                memcpy(buf, tmp + offset, len);
                copy_of_len -= len;
                current_address += len;
            } else {
                n = JBOD_BLOCK_SIZE - offset;
                memcpy(buf, tmp + offset, n);
                copy_of_len -= n;
                current_address += n;
            }
            block_count = 1; // not on first block anymore
        }

        // read full blocks
        else if (copy_of_len >= 256) {
            buf += n;
            memcpy(buf, tmp, JBOD_BLOCK_SIZE);
            copy_of_len -= 256;
            n = 256;
            current_address += 256;
        }

        // read the last block
        else {
            memcpy(buf + n, tmp, copy_of_len);
            current_address += copy_of_len;
        }
    }

    return len;
}

int mdadm_write(uint32_t addr, uint32_t len, const uint8_t *buf) {

    uint32_t end_of_the_linear_address_space = 1048576;

    // checks for failures from write_invalid_parameters()
    if ((len > 1024) || (buf == NULL && len > 0) || (addr < 0) || ((addr + len) > end_of_the_linear_address_space) || (mount == 0)) {
        return -1;
    }

    int count = 0;
    int current_address = addr;
    int write_length = len;
    int copy_of_length = len;
    int written_bytes = 0;
    int offset;

    // loop that runs until all data has been written to the storage system
    while (current_address < (addr + len)) {

        offset = (current_address % JBOD_DISK_SIZE) % JBOD_BLOCK_SIZE; // set the offset number for the current address
        uint8_t buffer[JBOD_BLOCK_SIZE];                               // set temporary buffer to hold data be written to the storage system

        // First, look up the block in the cache.
        int cache_index = cache_lookup((current_address / JBOD_DISK_SIZE), ((current_address % JBOD_DISK_SIZE) / JBOD_BLOCK_SIZE), buffer);

        // If the block is not in the cache, read it from disk and insert it into the cache.
        if (cache_index == -1) {
            seek((current_address / JBOD_DISK_SIZE), ((current_address % JBOD_DISK_SIZE) / JBOD_BLOCK_SIZE));
            jbod_client_operation(encode_op(JBOD_READ_BLOCK, 0, 0, 0), buffer);                               // reads the block into the temporary buffer
            seek((current_address / JBOD_DISK_SIZE), ((current_address % JBOD_DISK_SIZE) / JBOD_BLOCK_SIZE)); // seek to the appropriate disk and block

            // Insert the block into the cache.
            cache_insert((current_address / JBOD_DISK_SIZE), ((current_address % JBOD_DISK_SIZE) / JBOD_BLOCK_SIZE), buffer);
        }

        // writes the first block
        if (count == 0) {
            if (len + offset <= JBOD_BLOCK_SIZE) {
                memcpy(buffer + offset, buf, len); // copies the data from the user-supplied buffer to the appropriate location in the temporary buffer
                jbod_client_operation(encode_op(JBOD_WRITE_BLOCK, 0, 0, 0), buffer); // writes buffer back to the block
                written_bytes += len;                                                // update the number of bytes written
                copy_of_length -= len;                                               // update the remaining length of data to be written
                current_address += len;                                              // update the current address

            } else {
                write_length = JBOD_BLOCK_SIZE - offset;
                memcpy(buffer + offset, buf, write_length); // copies the data from the user-supplied buffer to the appropriate location in the temporary buffer
                written_bytes += write_length;
                jbod_client_operation(encode_op(JBOD_WRITE_BLOCK, 0, 0, 0), buffer); // writes buffer back to the block
                copy_of_length -= write_length;
                current_address += write_length;
            }
            count = 1;
        }

        // write extends beyond the first block
        else if (copy_of_length >= 256) {
            memcpy(buffer, buf + written_bytes, JBOD_BLOCK_SIZE);                // copies data from buf to buffer
            jbod_client_operation(encode_op(JBOD_WRITE_BLOCK, 0, 0, 0), buffer); // write buffer to the block
            copy_of_length -= 256;
            write_length = 256;
            written_bytes += 256;
            current_address += 256;
        }

        // last block
        else {
            memcpy(buffer + offset, buf + written_bytes, copy_of_length);
            jbod_client_operation(encode_op(JBOD_WRITE_BLOCK, 0, 0, 0), buffer);
            current_address += copy_of_length;
        }
    }

    return len;
}
