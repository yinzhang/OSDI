/****************************************************************
 *                                                              *
 *  drecover.h                                                  *
 *                                                              *
 *          Definitions of data structure and constants         *
 *--------------------------------------------------------------*
 *                     04-27-2013           Yin Zhang           *
 *--------------------------------------------------------------*
 ****************************************************************/

#include <stdio.h>
#include <dirent.h>

/* constants for general use */
#define     MAX_STRING        128       /* max length of input string line */

/* constants for block */
#define     K               4096        /* Block size for the file system */
#define     K_MASK          (~(K - 1))
#define     K_SHIFT         12

/* files */
#define     TMP         "/tmp"	     /* for output */
#define     DEV         "/dev"

/* return values */
#define     OK              0
#define     ERROR           -1

#ifndef I_MAP_SLOTS
#define I_MAP_SLOTS         128
#define Z_MAP_SLOTS         128
#endif	

typedef struct dr_state {
    /* information from super block */
	unsigned inodes;                /* number of inodes */
	zone_t zones;                   /* total number of blocks */
    unsigned inode_maps;            /* number of inode bitmap blocks */
    unsigned zone_maps;             /* number of zone bitmap blocks */
    unsigned inode_blocks;          /* inode blocks */
    unsigned first_data;            /* total non-data blocks */
    int magic;                      /* Magic number */
    
    bit_t isearch;                  /* inodes below this bit number are in use */
    bit_t zsearch;                  /* all zones below this bit number are in use */
    
    /* information derived from the magic number */
    unsigned char is_fs;            /* none zero for good fs */
    unsigned char v1;               /* none zero for v1 fs */
    unsigned inode_size;            /* size of disk inode */
    unsigned nr_indirects;          /* number of indirect blocks */
    unsigned zone_num_size;         /* size of disk zone num */
    int block_size;                 /* file system block size */
    
    /* other derived numbers */
    bit_t inodes_in_map;            /* bits in inode map */
    bit_t zones_in_map;             /* bits in zone map */
    int ndzones;                    /* number of direct zones in an inode */
    
    /* information from map blocks */
    bitchunk_t inode_map[ I_MAP_SLOTS * K / sizeof (bitchunk_t) ];
    bitchunk_t zone_map[ Z_MAP_SLOTS * K / sizeof (bitchunk_t) ];
    
    /* information for current block */
    off_t address;                  /* current address */
    off_t last_addr;                /* for erasing ptrs */
    zone_t block;                   /* current block */
    unsigned offset;                /* offset within block */
    
    char sbuf[_MIN_BLOCK_SIZE];     /* buffer for super block */
    char buffer[_MAX_BLOCK_SIZE];   /* general buffer */
    
    /* search information */
    char search_string[MAX_STRING + 1];
    
    /* file information */
    char *device_name;
    int device_d;
    int device_mode;
    zone_t device_size;             /* number of blocks */
    
    char file_name[MAX_STRING + 1];
    FILE *file_f;
} dr_state;

/* function referenes */
/* drecover.c */
_PROTOTYPE(int main, (int argc, char *argv[]));
_PROTOTYPE(void do_recover, (char *str));
_PROTOTYPE(void do_test, (char *fstr));

/* dr_recover.c */
_PROTOTYPE(int split_dir_file, (char *path_name, char **dir_name, char **file_name));
_PROTOTYPE(char *file_device, (char *file_name));
_PROTOTYPE(ino_t find_del_entry, (dr_state *st, char *path_name));
_PROTOTYPE(ino_t find_inode, (dr_state *st, char *filename));
_PROTOTYPE(off_t recover_blocks, (dr_state *st));
_PROTOTYPE(int in_use, (bit_t bit, dr_state *st, int mode));
_PROTOTYPE(int data_block, (dr_state *st, zone_t block, off_t *file_size));
_PROTOTYPE(int free_block, (dr_state *st, zone_t block));
_PROTOTYPE(int indirect, (dr_state *st, zone_t block, off_t *file_size, int dblind));

/* dr_dio.c */
_PROTOTYPE(void read_disk, (dr_state *st, off_t block_addr, char *buffer));
_PROTOTYPE(void read_block, (dr_state *st, char *buffer));
_PROTOTYPE(void read_super_block, (dr_state *st));
_PROTOTYPE(void read_bit_map, (dr_state *st));