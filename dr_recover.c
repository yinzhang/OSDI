//
//  dr_recover.c
//
//      File restoration routines.
//
//  Created by Yin Zhang on 4/28/13.
//

#include <stdio.h>
#include <stdlib.h>
#include <minix/config.h>
#include <sys/types.h>
#include <sys/dirent.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <limits.h>
#include <pwd.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>

#include <minix/const.h>
#include <minix/type.h>
#include "mfs/const.h"
#include "mfs/type.h"
#include "mfs/mfsdir.h"
#include "mfs/inode.h"
#include <minix/fslib.h>

#include "drecover.h"

/* split_dir_file()
 *      split "path_name" into a directory name and a file name
 *      0 is returned on error conditions
 */
int split_dir_file(path_name, dir_name, file_name)
char *path_name;
char **dir_name;
char **file_name;
{
    char *p;
    static char directory[MAX_STRING + 1];
    static char filename[MAX_STRING + 1];
    
    if((p = strrchr(path_name, '/')) == NULL) {
        strcpy(directory, ".");
        strcpy(filename, path_name);
    }
    else {
        *directory = '\0';
        strncat(directory, path_name, p - path_name);
        strcpy(filename, p + 1);
    }
    
    if(*directory == '\0')
        strcpy(directory, "/");
    
    if(*filename == '\0') {
        fprintf(stderr, "A file name must follow the directory name!\n");
        return(0);
    }
    
    *dir_name = directory;
    *file_name = filename;
    
    return(1);
}

/* file_device(file_name)
 *
 *      return the name of the file system device containing the file.
 *      we have only been given a file name and need to determine which
 *      file system device to open.
 *
 *      NULL is returned on error conditions.
 */
char *file_device(file_name)
char *file_name;
{
    struct stat fstat;
    struct stat dstat;
    int dev_d;
    struct direct entry;
    static char device_name[NAME_MAX + 1];
    
    if(access(file_name, R_OK) != 0) {
        fprintf(stderr, "Can not find %s\n", file_name);
        return(NULL);
    }
    
    if(stat(file_name, &fstat) == -1) {
        fprintf(stderr, "Can not stat %s\n", file_name);
        return(NULL);
    }
    
    /* open /dev for reading */
    if((dev_d = open(DEV, O_RDONLY)) == -1) {
        fprintf(stderr, "Can not read %s", DEV);
        return(NULL);
    }
    
    while(read(dev_d, (char *)&entry, sizeof(struct direct)) == sizeof(struct direct)) {
        if(entry.mfs_d_ino == 0)
            continue;
        
        strcpy(device_name, DEV);
        strcat(device_name, "/");
        strncat(device_name, entry.mfs_d_name, NAME_MAX);
        
        //printf("construct device_name: %s\n", device_name);
        
        if(stat(device_name, &dstat) == -1) {
            fprintf(stderr, "device name: %s cannot be stated!\n", device_name);
            continue;
        }
        
        if((dstat.st_mode & S_IFMT) != S_IFBLK) {
            continue;
        }
        
        if(fstat.st_dev == dstat.st_rdev) {
            close(dev_d);
            return(device_name);
        }
    }
    
    close(dev_d);
    
    //printf("device number that contain the file is: %d\n", fstat.st_dev);
    
    fprintf(stderr, "The device containing file %s is not in %s", file_name, DEV);
    return(NULL);
}

/* find_del_entry(st, path_name)
 *
 *      step 1. split "path_name" into a directory name and a file name
 *      step 2. search the directory for a entry that would match the file name
 *      note: deleted entries have a zero i-node number, but original i-node number
 *      is placed at the end od the file name
 *      return i-node number iff successfully find the entry
 *      else return 0
 */

ino_t find_del_entry(st, path_name)
dr_state *st;
char *path_name;
{
    char *dir_name;
    char *file_name;
    
    ino_t dir_ino;
    struct stat dir_stat;
    
    int dir_d;
    int count;
    struct direct entry;
    
    ino_t inode;                    /* inode for the demaged file */
    
    /* check if the file exist */
    if(access(path_name, F_OK) == 0) {
        printf("File has not been damaged!\n");
        //return 0;
    }
    
    /* split the path_name into a directory and a file name */
    if(!split_dir_file(path_name, &dir_name, &file_name)) {
        fprintf(stderr, "File path: %s error!\n", path_name);
        return 0;
    }
    
    printf("File path: %s build OK!\n", path_name);
    
    /* check to make sure that the directory can be accessed */
    if(access(dir_name, R_OK) != 0) {
        printf("Cannot access directory: %s\n", dir_name);
        return 0;
    }
    
    /* make sure "dir_name" is really a directory */
    if(stat(dir_name, &dir_stat) == -1 || (dir_stat.st_mode & S_IFMT) != S_IFDIR) {
        printf("Cannot find directory: %s\n", dir_name);
        return 0;
    }
    
    /* make sure the directory is on the current file system device */
    if((dir_ino = find_inode(st, dir_name)) == 0) {
        printf("Directory: %s cannot be found on the device!\n", dir_name);
        return 0;
    }
    
    printf("Path: %s check finished! OK!\n", path_name);
    printf("Corresponding inode to the directory %s is %ld\n", dir_name, dir_ino);
    
    /* open the directory and search for the lost file name */
    if((dir_d = open(dir_name, O_RDONLY)) == -1) {
        printf("Cannot read directory %s\n", dir_name);
        return 0;
    }
    
    while((count = read(dir_d, (char *)&entry, sizeof(struct direct))) == sizeof(struct direct)) {
        if( entry.mfs_d_ino == 0 && strncmp(file_name, entry.mfs_d_name, NAME_MAX - sizeof(ino_t)) == 0) {
            printf("Deleted file name: %s\n", entry.mfs_d_name);
            inode = *((ino_t *)&entry.mfs_d_name[MFS_DIRSIZ - sizeof(ino_t)]);
            //inode = entry.mfs_d_ino;
            close(dir_d);
            
            if(inode < 1 || inode > st->inodes) {
                printf("Illegal i-node number: %ld\n", inode);
                return 0;
            }
            
            return inode;
        }
    }
    
    close(dir_d);
    
    if(count == 0)
        printf("Cannot find a damaged entry for %s\n", file_name);
    else
        printf("Error reading directory %s\n", dir_name);
    return 0;
}

/* find_inode(st, filename)
 *
 *      find the i-node for the given file name
 */
ino_t find_inode(st, filename)
dr_state *st;
char *filename;
{
    struct stat device_stat;
    struct stat file_stat;
    ino_t inode;
    
    if(fstat(st->device_d, &device_stat) == -1) {
        fprintf(stderr, "Can not fstat(2) file system device\n" );
        exit(1);
    }
    
#ifdef S_IFLNK
    if(lstat(filename, &file_stat) == -1)
#else
    if(stat(filename, &file_stat) == -1)
#endif
    {
        printf("Can not find file %s\n", filename);
        return 0;
    }
    
    if(device_stat.st_rdev != file_stat.st_dev) {
        printf("File is not on device %s\n", st->device_name);
        return 0;
    }
    
    inode = file_stat.st_ino;
    
    if(inode < 1  || inode > st->inodes) {
        printf("Illegal i-node number!\n");
        return 0;
    }
    
    return inode;
}


/*	Recover_Blocks( state )
 *
 *		Try to recover all the blocks for the i-node
 *		currently pointed to by "s->address". The
 *		i-node and all of the blocks must be marked
 *		as FREE in the bit maps. The owner of the
 *		i-node must match the current real user name.
 *
 *		"Holes" in the original file are maintained.
 *		This allows moving sparse files from one device
 *		to another.
 *
 *		On any error -1L is returned, otherwise the
 *		size of the recovered file is returned.								
 */
off_t recover_blocks(st)
dr_state *st;
{
    //struct inode core_inode;
    //d1_inode *dip1;
    //d2_inode *dip2;
    //struct inode *inode = &core_inode;
    struct inode *inode;
    bit_t node = (st->address - (st->first_data - st->inode_blocks) * K) / st->inode_size + 1;
    
    //printf("node = %d\n", node);
    
    //printf("size of d1_inode: %d\n", sizeof(d1_inode));
    //printf("size of d2_inode: %d\n", sizeof(d2_inode));
    
    //printf("buffer size is: %d\n", sizeof(st->buffer));
    
    //printf("buffer1 address is: %d\n",(st->offset & ~ 0x1f));
    //printf("buffer2 address is: %d\n",(st->offset & ~ 0x1f & ~ (V2_INODE_SIZE-1)));
    
    //dip1 = (d1_inode *)&st->buffer[st->offset & ~ 0x1f];
    //dip2 = (d2_inode *)&st->buffer[st->offset & ~ 0x1f & ~ (V2_INODE_SIZE-1)];
    
    //printf("d1_size = %d\n", dip1->d1_size);
    //printf("d2_size = %d\n", dip2->d2_size);
    
    //conv_inode(inode, dip1, dip2, READING, st->magic);

    inode = (struct inode *)&st->buffer[st->offset];
    
    if(st->block < st->first_data - st->inode_blocks || st->block >= st->first_data) {
        printf("Not in an inode block");
        return(-1L);
    }

    /*  Is this a valid, but free i-node?  */
    if(node > st->inodes) {
        printf("Not an inode");
        return(-1L);
    }
    
    if(in_use(node, st, 1)) {
        printf("i-node is in use\n");
        return(-1L);
    }
    
    printf("Recovering start...\n");

    off_t file_size = inode->i_size;
    int i;

    printf("i_size = %ld\n", file_size);
        
    /*  Up to st->ndzones pointers are stored in the i-node.  */
    for(i = 0; i < st->ndzones; ++i) {
        if(file_size == 0)
            return(inode->i_size);
            
        if (!data_block(st, inode->i_zone[i], &file_size))
            return(-1L);
    }
        
    if(file_size == 0)
        return(inode->i_size);
        
    /*  An indirect block can contain up to inode->i_indirects more blk ptrs.  */
    if(!indirect(st, inode->i_zone[st->ndzones], &file_size, 0))
        return(-1L);
        
    if(file_size == 0)
        return(inode->i_size);
        
    /*  A double indirect block can contain up to inode->i_indirects blk ptrs. */
    if(!indirect(st, inode->i_zone[st->ndzones+1], &file_size, 1))
        return(-1L);
        
    if(file_size == 0)
        return(inode->i_size);
        
    fprintf(stderr, "Internal fault (file_size != 0)\n");
    
    /* NOTREACHED */
    return(-1L);
}

/* in_use(bit, map)
 *      
 *      is the bit set in map?
 */
int in_use(bit, st, mode)
bit_t bit;
dr_state *st;
int mode;                   /* mode == 1 --> in_use for inode; mode == 0 --> in_use for zone */
{
    if(mode == 1) {
        if(bit < st->isearch)
            return 1;
    }
    else {
        if(bit < st->zsearch)
            return 1;
    }
    
    return 0;
    //return(map[(int)(bit / (CHAR_BIT * sizeof(bitchunk_t)))] & (1 << ((unsigned)bit % (CHAR_BIT * sizeof (bitchunk_t)))));
}

/* data_block(st, block, &file_size)
 *
 *      If "block" is free then write  Min(file_size, k)
 *      bytes from it onto the current output file.
 *      If "block" is zero, this means that a 1k "hole"
 *      is in the file. The recovered file maintains
 *      the reduced size by not allocating the block.
 *      The file size is decremented accordingly.
 */
int data_block(st, block, file_size)
dr_state *st;
zone_t block;
off_t *file_size;
{
    char buffer[K];
    off_t block_size = *file_size > K ? K : *file_size;
    
    /*  Check for a "hole".  */
    if (block == NO_ZONE) {
        if(block_size < K) {
            printf("File has a hole at the end\n");
            return(0);
        }
        
        if(fseek(st->file_f, block_size, SEEK_CUR) == -1) {
            printf("Problem seeking %s\n", st->file_name);
            return(0);
        }
        
        *file_size -= block_size;
        return(1);
    }

    /*  Block is not a "hole". Copy it to output file, if not in use.  */
    printf("Block is not a hole!\n");
    if(!free_block(st, block))
        return(0);
    
    read_disk(st, (long)block << K_SHIFT, buffer);
    
    if(fwrite(buffer, 1, (size_t)block_size, st->file_f) != (size_t)block_size) {
        printf("Problem writing %s\n", st->file_name);
        return(0);
    }
    
    *file_size -= block_size;
    return(1);
}

/* free_block(st, block)
 *
 *      Make sure "block" is a valid data block number, and it
 *      has not been allocated to another file.
 */
int free_block(st, block)
dr_state *st;
zone_t block;
{
    if (block < st->first_data || block >= st->zones) {
        printf("Illegal block number\n");
        return(0);
    }

    if(in_use((bit_t)(block - (st->first_data - 1)), st, 0)) {
        printf("Encountered an \"in use\" data block\n");
        return(0);
    }
    
    return(1);
}

/* indirect(st, block, &file_size, double)
 *
 *      Recover all the blocks pointed to by the indirect block
 *      "block",  up to "file_size" bytes. If "double" is true,
 *      then "block" is a double-indirect block pointing to
 *      V*_INDIRECTS indirect blocks.
 *
 *      If a "hole" is encountered, then just seek ahead in the
 *      output file.
 */
int indirect(st, block, file_size, dblind)
dr_state *st;
zone_t block;
off_t *file_size;
int dblind;
{
    union {
        zone1_t ind1[V1_INDIRECTS];
        zone_t ind2[V2_INDIRECTS(_MAX_BLOCK_SIZE)];
    }indir;
    
    int i;
    zone_t zone;
    
    /* Check for a "hole". */
    if(block == NO_ZONE) {
        off_t skip = (off_t)st->nr_indirects * K;
        
        if(*file_size < skip || dblind) {
            printf("File has a hole at the end\n");
            return(0);
        }
        
        if(fseek( st->file_f, skip, SEEK_CUR) == -1 ) {
            printf( "Problem seeking %s\n", st->file_name );
            return(0);
        }
        
        *file_size -= skip;
        return( 1 );
    }

    /* Not a "hole". Recover indirect block, if not in use. */
    if(!free_block(st, block))
        return(0);
    
    read_disk(st, (long)block << K_SHIFT, (char *)&indir);
    
    for(i = 0; i < st->nr_indirects; ++i) {
        if(*file_size == 0)
            return(1);
        
        zone = (st->v1 ? indir.ind1[i] : indir.ind2[i]);
        if (dblind) {
            if (!indirect(st, zone, file_size, 0))
                return(0);
        }
        else {
            if (!data_block(st, zone, file_size))
                return(0);
        }
    }
    
    return(1);
}
