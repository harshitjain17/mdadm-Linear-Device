#include "net.h"
#include "jbod.h"
#include <arpa/inet.h>
#include <err.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

// the client socket descriptor for the connection to the server
int cli_sd = -1;

// attempts to read n bytes from fd; returns true on success and false on failure
static bool nread(int fd, int len, uint8_t *buf) {

    // declare a variable to store the number of bytes read so far
    int n_read = 0;

    // loop to keep reading until the specified number of bytes have been read
    while (n_read < len) {

        // use read() system call to read the remaining bytes from fd
        int n = read(fd, &buf[n_read], len - n_read);

        // if read() returns a non-positive value, it indicates a failure, so return false
        if (n < 0) {
            return false;
        }
        n_read += n;
    }
    return true;
}

// attempts to write n bytes to fd; returns true on success and false on failure
static bool nwrite(int fd, int len, uint8_t *buf) {

    // declare a variable to store the number of bytes written so far
    int n_write = 0;

    // loop to keep writing until the specified number of bytes have been written
    while (n_write < len) {

        // use write() system call to write the remaining bytes to fd
        int n = write(fd, &buf[n_write], len - n_write);

        // if write() returns a non-positive value, it indicates a failure, so return false
        if (n < 0) {
            return false;
        }
        n_write += n;
    }
    return true;
}

// attempts to receive a packet from fd; returns true on success and false on failure
static bool recv_packet(int fd, uint32_t *op, uint16_t *ret, uint8_t *block) {

    // declare variables to store the length of the packet, the operation code, the return code, and the data block
    uint16_t len;
    uint8_t header[HEADER_LEN];

    // call nread function to read HEADER_LEN bytes from fd into header. If it fails, return false
    if (nread(fd, HEADER_LEN, header) == false) {
        return false;
    }

    // copy the next sizeof(len) bytes from header and store it in len
    memcpy(&len, header, sizeof(len));

    // copy the next sizeof(*op) bytes from header and store it in the memory location pointed to by op
    memcpy(op, header + 2, sizeof(*op));

    // copy the next sizeof(*ret) bytes from header and store it in the memory location pointed to by ret
    memcpy(ret, header + 6, sizeof(*ret));

    // convert the length of the packet from network byte order to host byte order
    len = ntohs(len);

    // convert the operation code from network byte order to host byte order
    *op = ntohl(*op);

    // convert the return code from network byte order to host byte order
    *ret = ntohs(*ret);

    // if len is equal to HEADER_LEN + JBOD_BLOCK_SIZE, call nread function to read 256 bytes from fd into block. If it fails, return false
    if (len == (HEADER_LEN + JBOD_BLOCK_SIZE)) {
        if (nread(fd, JBOD_BLOCK_SIZE, block) == false) {
            return false;
        }
    }
    return true;
}

// attempts to send a packet to sd; returns true on success and false on failure
static bool send_packet(int sd, uint32_t op, uint8_t *block) {

    // declare variables to store the length of the packet, the operation code, and the data block
    uint8_t header[HEADER_LEN];
    uint16_t len = HEADER_LEN;

    // extract the command from the op code
    uint32_t cmd = op >> 26;

    // if the command is JBOD_WRITE_BLOCK, then set the length of the packet to the sum of the header length and the JBOD block size.
    // Otherwise, set the length of the packet to the header length
    if (cmd == JBOD_WRITE_BLOCK) {
        len += JBOD_BLOCK_SIZE;
    }

    // convert the length of the packet to network byte order
    len = htons(len);

    // convert the op code to network byte order
    op = htonl(op);

    // copy the length of the packet into the header buffer
    memcpy(header, &len, sizeof(len));

    // copy the op code into the header buffer
    memcpy(header + 2, &op, sizeof(op));

    // attempt to write the header to the socket descriptor using the nwrite function
    if (nwrite(sd, HEADER_LEN, header) == false) {
        return false;
    }

    // if the command is JBOD_WRITE_BLOCK, attempt to write the block of data to the socket descriptor
    if (cmd == JBOD_WRITE_BLOCK) {
        if (nwrite(sd, JBOD_BLOCK_SIZE, block) == false) {
            return false;
        }
    }
    return true;
}

// declare a structure to store the address information for the server
struct sockaddr_in caddr;

// attempts to connect to server and set the global cli_sd variable to the socket; returns true if successful and false if not
bool jbod_connect(const char *ip, uint16_t port) {

    // create a socket
    cli_sd = socket(AF_INET, SOCK_STREAM, 0);

    // if the socket could not be created, return false
    if (cli_sd == -1) {
        return false;
    }

    // set the family field of the address structure to AF_INET, which indicates that the server uses IPv4
    caddr.sin_family = AF_INET;

    // set the port field of the address structure to the specified port number
    caddr.sin_port = htons(port);

    // convert the IP address from a string to a binary format
    if (inet_aton(ip, &caddr.sin_addr) == 0) {
        return false;
    }

    // connect to the server
    if (connect(cli_sd, (const struct sockaddr *)&caddr, sizeof(caddr)) == -1) {
        return false;
    }
    return true;
}

// disconnects from the server and resets cli_sd
void jbod_disconnect(void) {

    // close the socket
    close(cli_sd);

    // reset the global variable cli_sd to -1
    cli_sd = -1;
}

// sends the JBOD operation to the server and receives and processes the response
int jbod_client_operation(uint32_t op, uint8_t *block) {
    uint16_t ret;
    if (cli_sd == -1) {
        return -1;
    } else {
        if (send_packet(cli_sd, op, block) == true) {
            recv_packet(cli_sd, &op, &ret, block);
            return 0;
        } else {
            return -1;
        }
    }
}