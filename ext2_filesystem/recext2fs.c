#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include "ext2fs.h"

#define BLOCK_SIZE 1024
#define RESERVED_INODES 10

struct ext2_super_block *read_super_block(int fd) {
    struct ext2_super_block *sb = malloc(sizeof(struct ext2_super_block));
    lseek(fd, EXT2_SUPER_BLOCK_POSITION, SEEK_SET);
    read(fd, sb, sizeof(struct ext2_super_block));
    if (sb->magic != EXT2_SUPER_MAGIC) {
        fprintf(stderr, "Not a valid ext2 filesystem\n");
        exit(1);
    }
    return sb;
}

struct ext2_block_group_descriptor *read_bgdt(int fd, int block_groups) {
    struct ext2_block_group_descriptor *bgdt = malloc(block_groups * sizeof(struct ext2_block_group_descriptor));
    lseek(fd, EXT2_BOOT_BLOCK_SIZE + EXT2_SUPER_BLOCK_SIZE, SEEK_SET);
    read(fd, bgdt, block_groups * sizeof(struct ext2_block_group_descriptor));
    return bgdt;
}

void read_block(int fd, void *buf, uint32_t block_num, uint32_t block_size) {
    lseek(fd, block_num * block_size, SEEK_SET);
    read(fd, buf, block_size);
}

void write_block(int fd, void *buf, uint32_t block_num, uint32_t block_size) {
    lseek(fd, block_num * block_size, SEEK_SET);
    write(fd, buf, block_size);
}

void mark_reserved_inodes_used(uint8_t *inode_bitmap) {
    for (uint32_t i = 0; i < RESERVED_INODES; i++) {
        uint32_t byte_idx = i / 8;
        uint32_t bit_idx = i % 8;
        inode_bitmap[byte_idx] |= (1 << bit_idx);
    }
}

void check_and_fix_bitmaps(int fd, struct ext2_super_block *sb, struct ext2_block_group_descriptor *bgdt, uint32_t bgdt_size ) {
    uint32_t block_size = EXT2_UNLOG(sb->log_block_size);
    uint32_t blocks_per_group = sb->blocks_per_group;
    uint32_t inodes_per_group = sb->inodes_per_group;
    uint32_t block_groups = (sb->block_count + blocks_per_group - 1) / blocks_per_group;
    uint8_t *block_bitmap = malloc(block_size);
    uint8_t *inode_bitmap = malloc(block_size);
    uint8_t *buffer = malloc(block_size);
    uint32_t bgdt_blocks = (bgdt_size + block_size - 1) / block_size; // Round up to the nearest block
    uint32_t calculated_free_blocks = 0;
    uint32_t calculated_free_inodes = 0;
    uint32_t inode_table_blocks = (sb->inodes_per_group * EXT2_INODE_SIZE + block_size-1) / block_size;
    //printf("reservedd1: %u", bgdt->reserved[0]);
    //printf("reservedd1: %u", bgdt->reserved[1]);
    //printf("reservedd1: %u", bgdt->reserved[2]);
    //printf("reservedd2: %u", sb->reserved_block_count);
    //printf("block size: %u", block_size);

    for (uint32_t i = 0; i < block_groups; i++) {
        read_block(fd, block_bitmap, bgdt[i].block_bitmap, block_size);
        read_block(fd, inode_bitmap, bgdt[i].inode_bitmap, block_size);

        // Mark the first 10 inodes as used
        mark_reserved_inodes_used(inode_bitmap);

        //printf("Checking block group %u\n", i);
        for (uint32_t j = 0; j < blocks_per_group; j++) {
            uint32_t block = i * blocks_per_group + j+1;
            if(block_size>1024) block-=1;
            if (block >= sb->block_count+1) break;

            int byte_idx = j / 8;
            int bit_idx = j % 8;
            int is_non_data_block = 0;

            // Superblock (always at block 1 if block size > 1KB)
            /*if (block == 1) {
                is_non_data_block = 1;
                printf("mr1");
            }

            // Block Group Descriptor Table (BGD) - check its location
            uint32_t bgdt_start_block = sb->first_data_block + 1;
            if (block >= bgdt_start_block && block < bgdt_start_block + bgdt_blocks) {
                is_non_data_block = 1;
                printf("mr2");
            }
            // Reserved GDT blocks - specific range
            //if (j >= 2 && j < 2 + sb->reserved_block_count) {
            //    is_non_data_block = 1;
            //}

            // Bitmaps and Inode Table
            if (block == bgdt[i].block_bitmap || block == bgdt[i].inode_bitmap ) {
                is_non_data_block = 1;
                printf("mr3");
            }*/
            //if(block >= bgdt[i].inode_table){
                    //printf("mr4");
            if(block < bgdt[i].inode_table + inode_table_blocks){
                is_non_data_block = 1;
                //printf("mr5");
            }
                
            //}
            /*if ((i == 0 || i == 1 || i == block_groups - 1) && block < bgdt_start_block + bgdt_blocks) {
                is_non_data_block = 1;
                printf("mr6");
            }*/

            // Update the block bitmap for non-data blocks
            if (is_non_data_block) {
                if (!(block_bitmap[byte_idx] & (1 << bit_idx))) {
                    // Incorrectly marked as free
                    block_bitmap[byte_idx] |= (1 << bit_idx);
                    //printf("Non-data block %u of group %u is set as used\n", block, i);
                }
                continue; // Skip further checks for non-data blocks
            }


            lseek(fd, block * block_size, SEEK_SET);
            int result = read(fd, buffer, block_size);
            if (result <= 0) break; // End of file reached

            int is_free = 1;
            for (uint32_t k = 0; k < block_size; k++) {
                if (buffer[k] != 0) {
                    is_free = 0;
                    break;
                }
            }

            if (is_free) {
                calculated_free_blocks++;
                if (block_bitmap[byte_idx] & (1 << bit_idx) ) {
                    // Incorrectly marked as used
                    block_bitmap[byte_idx] &= ~(1 << bit_idx);
                    //if(j<11) printf("block %u of group %u is set as free\n", j , i);
                    //sb->free_block_count++;
                    //bgdt[i].free_block_count++;
                }

            } else {
                if (!(block_bitmap[byte_idx] & (1 << bit_idx))) {
                    // Incorrectly marked as free
                    block_bitmap[byte_idx] |= (1 << bit_idx);
                    //printf("block %u of group %u is set as used\n", j , i);
                    //sb->free_block_count--;
                    //bgdt[i].free_block_count--;
                }
            }
        }
        // Check inode bitmap
        for (uint32_t j = 0; j < inodes_per_group; j++) {
            uint32_t inode = i * inodes_per_group + j + 1;
            if (inode > sb->inode_count) break;
            if (inode <= RESERVED_INODES) continue; // Skip reserved inodes

            lseek(fd, bgdt[i].inode_table * block_size + j * sb->inode_size, SEEK_SET);
            int result = read(fd, buffer, sb->inode_size);
            if (result <= 0) break; // End of file reached

            struct ext2_inode *inode_struct = (struct ext2_inode *)buffer;
            int byte_idx = j / 8;
            int bit_idx = j % 8;
            //printf("Calculatedd free inodes: %u, Superblockk free inodes: %u\n", calculated_free_inodes, sb->free_inode_count);

            if (inode_struct->mode == 0) {
                calculated_free_inodes++;
                if (inode_bitmap[byte_idx] & (1 << bit_idx)) {
                    // Incorrectly marked as used
                    inode_bitmap[byte_idx] &= ~(1 << bit_idx);

                }
            } else {
                if (!(inode_bitmap[byte_idx] & (1 << bit_idx))) {
                    // Incorrectly marked as free
                    inode_bitmap[byte_idx] |= (1 << bit_idx);

                }
            }

        }

        write_block(fd, block_bitmap, bgdt[i].block_bitmap, block_size);
        write_block(fd, inode_bitmap, bgdt[i].inode_bitmap, block_size);
    }

    //printf("Calculated free blocks: %u, Superblock free blocks: %u\n", calculated_free_blocks, sb->free_block_count);
    //printf("Calculated free inodes: %u, Superblock free inodes: %u\n", calculated_free_inodes, sb->free_inode_count);

    free(block_bitmap);
    free(inode_bitmap);
    free(buffer);
}

int main(int argc, char *argv[]) {
    /*if (argc != 2) {
        fprintf(stderr, "Usage: %s <filesystem image>\n", argv[0]);
        return 1;
    }*/

    int fd = open(argv[1], O_RDWR);
    if (fd < 0) {
        perror("Failed to open filesystem image");
        return 1;
    }

    struct ext2_super_block *sb = read_super_block(fd);
    uint32_t block_groups = (sb->block_count + sb->blocks_per_group - 1) / sb->blocks_per_group;
    struct ext2_block_group_descriptor *bgdt = read_bgdt(fd, block_groups);

    check_and_fix_bitmaps(fd, sb, bgdt, block_groups * sizeof(struct ext2_block_group_descriptor));

    // Write back the super block
    lseek(fd, EXT2_SUPER_BLOCK_POSITION, SEEK_SET);
    write(fd, sb, sizeof(struct ext2_super_block));

    // Write back the block group descriptor table
    lseek(fd, EXT2_BOOT_BLOCK_SIZE + EXT2_SUPER_BLOCK_SIZE, SEEK_SET);
    write(fd, bgdt, block_groups * sizeof(struct ext2_block_group_descriptor));

    free(sb);
    free(bgdt);
    close(fd);

    return 0;
}
