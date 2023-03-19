#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "jbod.h"
#include "mdadm.h"

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
        int rc = jbod_operation(op, NULL);

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
        int rc = jbod_operation(op, NULL);

        if (rc == 0) {
            mount = 0;
            return 1;
        }
        return -1;
    };
}

// this function seeks to the specified disk and block number using jbod_operation
void seek(int disk_number, int block_number) {
    jbod_operation(encode_op(JBOD_SEEK_TO_DISK, disk_number, 0, 0), NULL);   // seek to disk_num
    jbod_operation(encode_op(JBOD_SEEK_TO_BLOCK, 0, 0, block_number), NULL); // seek to block_num 
};

int mdadm_read(uint32_t addr, uint32_t len, uint8_t *buf) {

    uint32_t end_of_the_linear_address_space = 1048570;

    // checks for failures from read_invalid_parameters()
    if ((len > 1024) || (buf == NULL && len > 0) || ((addr + len) > end_of_the_linear_address_space) || (mount == 0)) {
        return -1;
    }

    int count = 0;
    int remaining_read = 0;     // bytes that yet to be read in the disk
    int current_address = addr; // keeping track of current address

    seek((current_address / JBOD_DISK_SIZE), ((current_address % JBOD_DISK_SIZE) / JBOD_BLOCK_SIZE));   // seek to the appropriate disk and block

    // loop through each disk, partial or impartial till the remaining_read
    for (int address = 0; address < len; address += remaining_read) {
        int read_length = 0;
        count += 1;

        int offset = current_address % JBOD_BLOCK_SIZE;                          // set offset
        uint8_t buffer[JBOD_BLOCK_SIZE];                                         // set temporary buffer to the size of the disk
        int disk_number = (int)current_address / (JBOD_DISK_SIZE);               // set the disk number
        jbod_operation(encode_op(JBOD_SEEK_TO_DISK, disk_number, 0, 0), NULL);   // seek to the disk
        int block_number = current_address / JBOD_NUM_BLOCKS_PER_DISK;           // set the block number
        jbod_operation(encode_op(JBOD_SEEK_TO_BLOCK, 0, 0, block_number), NULL); // seek to the block number

        // full disk read => offset = 0
        // the amount of the bytes remaining = the amount in the disk
        if (offset == 0) {
            read_length = read_length + len;
            if (read_length > JBOD_BLOCK_SIZE) {
                read_length = read_length - (read_length % JBOD_BLOCK_SIZE);
            }
        } else {
            read_length = JBOD_BLOCK_SIZE - offset; // otherwise, compute what's yet to be read
        }

        jbod_operation(encode_op(JBOD_READ_BLOCK, 0, 0, 0), buffer);

        if (address < len) {
            memcpy((buf + address), buffer, read_length); // memcpy the contents of the buffer
            address = address + read_length;
            current_address = current_address + read_length; // current_address is the address after process
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

        offset = (current_address % JBOD_DISK_SIZE) % JBOD_BLOCK_SIZE;       // set the offset number for the current address
        uint8_t buffer[JBOD_BLOCK_SIZE];                                     // set temporary buffer to hold data to be written to the storage system
        
        seek((current_address / JBOD_DISK_SIZE), ((current_address % JBOD_DISK_SIZE) / JBOD_BLOCK_SIZE));   
        jbod_operation(encode_op(JBOD_READ_BLOCK, 0, 0, 0), buffer);                                        // reads the block into the temporary buffer
        seek((current_address / JBOD_DISK_SIZE), ((current_address % JBOD_DISK_SIZE) / JBOD_BLOCK_SIZE));   // seek to the appropriate disk and block

        // writes the first block
        if (count == 0) {
            if (len + offset <= JBOD_BLOCK_SIZE) {    
                memcpy(buffer + offset, buf, len);                            // copies the data from the user-supplied buffer to the appropriate location in the temporary buffer
                jbod_operation(encode_op(JBOD_WRITE_BLOCK, 0, 0, 0), buffer); // writes buffer back to the block
                written_bytes += len;                                         // update the number of bytes written
                copy_of_length -= len;                                        // update the remaining length of data to be written
                current_address += len;                                       // update the current address
            
            } else {
                write_length = JBOD_BLOCK_SIZE - offset;
                memcpy(buffer + offset, buf, write_length);                   // copies the data from the user-supplied buffer to the appropriate location in the temporary buffer
                written_bytes += write_length;
                jbod_operation(encode_op(JBOD_WRITE_BLOCK, 0, 0, 0), buffer); // writes buffer back to the block
                copy_of_length -= write_length;
                current_address += write_length;
            }
            count = 1;
        }

        // write extends beyond the first block
        else if (copy_of_length >= 256) {
            memcpy(buffer, buf + written_bytes, JBOD_BLOCK_SIZE);             // copies data from buf to buffer
            jbod_operation(encode_op(JBOD_WRITE_BLOCK, 0, 0, 0), buffer);     // write buffer to the block
            copy_of_length -= 256;
            write_length = 256;
            written_bytes += 256;
            current_address += 256;
        }

        // last block
        else {
            memcpy(buffer + offset, buf + written_bytes, copy_of_length);
            jbod_operation(encode_op(JBOD_WRITE_BLOCK, 0, 0, 0), buffer);
            current_address += copy_of_length;
        }
    }

    return len;
}
