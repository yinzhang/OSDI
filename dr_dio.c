//
//  dr_dio.c
//  
//      Reading and writing to a file system device.
//
//  Created by Yin Zhang on 4/28/13.
//
//

#include <stdio.h>
#include <stdlib.h>
#include <minix/config.h>
#include <sys/types.h>
#include <unistd.h>
#include <limits.h>
#include <string.h>
#include <dirent.h>

#include <minix/const.h>
#include <minix/type.h>
#include "mfs/const.h"
#include "mfs/type.h"
#include "mfs/super.h"
#include "mfs/inode.h"
#include <minix/fslib.h>

#include "drecover.h"

/* read_disk(state, block_addr, buffer)
 *      read a 4K block at "block_addr" into buffer.
 */
void read_disk(st, block_addr, buffer)
dr_state *st;
off_t block_addr;
char *buffer;
{
    //printf("RD: block_addr = %lu\nst->device_d = %d\nst->block_size = %d\n", block_addr, st->device_d, st->block_size);
    
    if(lseek(st->device_d, block_addr, SEEK_SET) == -1) {
        printf("Error seeking %s\n", st->device_name);
        exit(1);
    }
    if(read(st->device_d, buffer, st->block_size) != st->block_size) {
        printf("Error reading %s\n", st->device_name);
        exit(1);
    }
}

/* read_block(state, buffer)
 *      read a 4K block from st->address into buffer
 *      checks address and updates blocks and offset
 */
void read_block(st, buffer)
dr_state *st;
char *buffer;
{
    //printf("st->device_size = %d\n", st->device_size);
    //printf("st->block_size = %d\n", st->block_size);
    //printf("st->address = %ld\n", st->address);

    long long end_addr;
    off_t block_addr;
    end_addr = (long long)st->device_size * st->block_size - 1;
    /* end_addr = 6891593728; */

    //printf("end_addr = %lld\n", end_addr);
    
    if(st->address < 0)
        st->address = 0L;
    
    if(st->address > end_addr)
        st->address = end_addr;

    //printf("st->address = %ld\n", st->address);
    
    /* adjust address */
    st->address &= ~1L;
    printf("Adjusted address is: %ld\n", st->address);
    
    block_addr = st->address & K_MASK;
    printf("Block address is %ld\n", block_addr);
    
    st->block = (zone_t)(block_addr >> K_SHIFT);
    st->offset = (unsigned)(st->address - block_addr);
    
    printf("current block: %u\n", st->block);
    printf("offset is: %u\n", st->offset);

    //printf("block_addr = %ld\n", block_addr);
    read_disk(st, block_addr, buffer);
}

/* read_super_block(state, buffer)
 *      read and check super block
 */
void read_super_block(st)
dr_state *st;
{
    struct super_block *super = (struct super_block *)st->sbuf;
    unsigned inodes_per_block;
    block_t offset;
    //off_t size;
    
    st->block_size = K;
    read_disk(st, (long) SUPER_BLOCK_BYTES, st->sbuf);
    
    st->magic = super->s_magic;
    if(st->magic == SUPER_MAGIC) {
        st->is_fs = TRUE;
        st->v1 = TRUE;
        st->inode_size = V1_INODE_SIZE;
        inodes_per_block = V1_INODES_PER_BLOCK;
        st->nr_indirects = V1_INDIRECTS;
        st->zone_num_size = V1_ZONE_NUM_SIZE;
        st->zones = super->s_nzones;
        st->ndzones = V1_NR_DZONES;
        st->block_size = _STATIC_BLOCK_SIZE;
    }
    else if(st->magic == SUPER_V2 || st->magic == SUPER_V3) {
        if(st->magic == SUPER_V3)
            st->block_size = super->s_block_size;
        else
            st->block_size = _STATIC_BLOCK_SIZE;
        st->is_fs = TRUE;
        st->v1 = FALSE;
        st->inode_size = V2_INODE_SIZE;
        inodes_per_block = V2_INODES_PER_BLOCK(st->block_size);
        st->nr_indirects = V2_INDIRECTS(st->block_size);
        st->zone_num_size = V2_ZONE_NUM_SIZE;
        st->zones = super->s_zones;
        st->ndzones = V2_NR_DZONES;
    }
    else {
        if(super->s_magic == SUPER_REV)
            printf("V1-bytes-swapped file system (?)\n");
        else if (super->s_magic == SUPER_V2_REV)
            printf("V2-bytes-swapped file system (?)\n");
        else
            printf("Not a Minix file system");
        printf("The file system features will not be available\n");
        st->zones = 100000L;
        return;
    }
    
    st->inodes = super->s_ninodes;
    st->inode_maps = bitmapsize((bit_t)st->inodes + 1, st->block_size);
    
    printf("s_block_size = %d\n", super->s_block_size);
    printf("inodes_per_block = %d\n", inodes_per_block);
    printf("st->inodes = %d\n", st->inodes);
    printf("st->inode_maps = %d\n", st->inode_maps);
    
    if(st->inode_maps != super->s_imap_blocks) {
        if (st->inode_maps > super->s_imap_blocks) {
            fprintf(stderr, "Corrupted inode map count or inode count in super block\n");
            exit(1);
        }
        else
            printf("Count of inode map blocks in super block suspiciously high\n");
        st->inode_maps = super->s_imap_blocks;
    }
    
    st->zone_maps = bitmapsize((bit_t)st->zones, st->block_size);
    
    printf("st->zone_maps = %d\n", st->zone_maps);
    
    if(st->zone_maps != super->s_zmap_blocks) {
        if(st->zone_maps > super->s_zmap_blocks) {
            fprintf(stderr, "Corrupted zone map count or zone count in super block\n");
            exit(1);
        }
        else
            printf("Count of zone map blocks in super block suspiciously high\n");
        st->zone_maps = super->s_zmap_blocks;
    }
    
    st->inode_blocks = (st->inodes + inodes_per_block - 1) / inodes_per_block;
    st->first_data = 2 + st->inode_maps + st->zone_maps + st->inode_blocks;
    
    printf("st->inode_blocks = %d\n", st->inode_blocks);
    printf("st->first_data = %d\n", st->first_data);
    printf("data zones = %u\n", ((super->s_zones - super->s_firstdatazone) << super->s_log_zone_size));
    //printf("super->s_firstdatazone_old = %d\n", super->s_firstdatazone_old);
    
    /* For even larger disks, a similar problem occurs with s_firstdatazone.
     * If the on-disk field contains zero, we assume that the value was too
     * large to fit, and compute it on the fly.
     */
    if (super->s_firstdatazone_old == 0) {
        offset = START_BLOCK + super->s_imap_blocks + super->s_zmap_blocks;
        offset += (super->s_ninodes + super->s_inodes_per_block - 1) / super->s_inodes_per_block;
        
        super->s_firstdatazone = (offset + (1 << super->s_log_zone_size) - 1) >> super->s_log_zone_size;
    }
    else {
        super->s_firstdatazone = (zone_t)super->s_firstdatazone_old;
    }
    
    if(st->first_data != super->s_firstdatazone) {
        if(st->first_data > super->s_firstdatazone) {
            fprintf(stderr, "Corrupted first data zone offset or inode count in super block\n");
            exit(1);
        }
        else
            printf("First data zone in super block suspiciously high\n");
        st->first_data = super->s_firstdatazone;
    }
    
    //printf("super->s_firstdatazone = %d\n", super->s_firstdatazone);
    
    st->inodes_in_map = st->inodes + 1;
    st->zones_in_map  = st->zones + 1 - st->first_data;
    
    /*
     if ( st->zones != st->device_size )
     Warning( "Zone count does not equal device size" );
     */
    
    st->device_size = st->zones;
    
    st->isearch = super->s_isearch;
    st->zsearch = super->s_zsearch;
    
    if(super->s_log_zone_size != 0) {
        fprintf(stderr, "Can not handle multiple blocks per zone\n");
        exit(1);
    }
}

/* read_bit_map(st)
 *
 *      read in the inode and zone bit maps form the
 *      specified file system device.
 */
void read_bit_map(st)
dr_state *st;
{
    int i;
    
    if(st->inode_maps > I_MAP_SLOTS || st->zone_maps > Z_MAP_SLOTS) {
        printf("Super block specifies too many bit map blocks!\n");
        return;
    }
    
    for(i = 0; i < st->inode_maps; ++ i) {
        read_disk(st, (long)(2 + i) * K, (char *)&st->inode_map[i * K / sizeof (bitchunk_t)]);
    }
    
    for(i = 0; i < st->zone_maps; ++ i) {
        read_disk(st, (long)(2 + st->inode_maps + i) * K, (char *)&st->zone_map[i * K / sizeof (bitchunk_t)]);
    }
}
