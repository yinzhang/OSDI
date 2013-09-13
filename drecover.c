/****************************************************************
 *                                                              *
 *  drecover.c                                                  *
 *                                                              *
 *      Main function and misc for FS tools drecover            *
 *--------------------------------------------------------------*
 *                  04-27-2013                  Yin Zhang       *
 *--------------------------------------------------------------*
 ****************************************************************/

#include <sys/types.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <minix/config.h>
#include <minix/const.h>
#include <minix/type.h>
#include <minix/u64.h>
#include "mfs/const.h"
#include "mfs/inode.h"
#include "mfs/type.h"
#include "mfs/mfsdir.h"
#include <minix/fslib.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/dirent.h>
#include <a.out.h>
#include <tools.h>
#include <dirent.h>

#include "mfs/super.h"
#include "drecover.h"

/* function reference */
_PROTOTYPE(void do_recover, (char *str));
_PROTOTYPE(void do_test, (char *fstr));

/* main function */
int main(int argc, char *argv[])
{
    char *command = argv[0];
    
    /* parse command */
    if(argc == 3 && strcmp(argv[1], "-r") == 0) {
        -- argc;
        ++ argv;
        do_recover(argv[1]);
    }
    else if(argc == 3 && strcmp(argv[1], "-t") == 0) {
        -- argc;
        ++ argv;
        do_test(argv[1]);
    }
    else {
        fprintf(stderr, "Usage: %s -[p] /path_name\n", command);
        exit(1);
    }
    return 0;
}

/* do_recover()
 *
 */
void do_recover(str)
char *str;
{
    static dr_state st;         /* static since it is safer not to putit on the stack and for special initialization */
    st.device_mode = O_RDONLY;
    
    char *dir_name;
    char *file_name;
    
    struct stat device_stat;
    struct stat tmp_stat;
    
    off_t size;
    ino_t inode;                 /* inode number of file which need to be recovered */
    
    /* data structure construction */
    /* split the path name into a directory and a file name */
    if(strlen(str) > MAX_STRING) {
        fprintf(stderr, "Path name too long!\n");
        exit(1);
    }
    
    if(!split_dir_file(str, &dir_name, &file_name)) {
        fprintf(stderr, "Path name error!\n");
        exit(1);
    }
    
    printf("dir_name: %s\n", dir_name);
    printf("file_name: %s\n", file_name);
    
    /* find the device holding the directory */
    if((st.device_name = file_device(dir_name)) == NULL) {
        fprintf(stderr, "Recover aborted!\n");
        exit(1);
    }
    printf("device_name: %s\n", st.device_name);
    
    /* the output file will be in /tmp with the same file name */
    strcpy(st.file_name, TMP);
    strcat(st.file_name, "/");
    strcat(st.file_name, file_name);
    
    if(stat(st.device_name, &device_stat) == -1) {
        fprintf(stderr, "Can not stat(2) device %s\n", st.device_name);
        exit(1);
    }
    
    if(stat(TMP, &tmp_stat) == -1) {
        fprintf(stderr, "Can not stat(2) directory %s\n", TMP);
        exit(1);
    }
    
    if(access(st.file_name, F_OK) == 0) {
        fprintf(stderr, "Will not overwrite file %s\n", st.file_name);
        exit(1);
    }
    
    /* open the output file */
    if((st.file_f = fopen(st.file_name, "w")) == NULL) {
        fprintf(stderr, "Can not open file %s\n", st.file_name);
        exit(1);
    }
     
    /* open the device file */
    if(stat(st.device_name, &device_stat) == -1) {
        fprintf(stderr, "Can not find file %s\n", st.device_name);
        exit(1);
    }
    
    /*
    printf("# of device = %u\n# of inode is %llu\nsize of file = %lld\namount of blocks is %lld\n", device_stat.st_dev,
           device_stat.st_ino, device_stat.st_size, device_stat.st_blocks);
     */
    
    /*if((device_stat.st_mode & S_IFMT) != S_IFBLK &&
       (device_stat.st_mode & S_IFMT) != S_IFREG) {
        fprintf(stderr, "Can only edit block special or regular files.\n");
        exit(1);
    }*/
    
    if((st.device_d = open(st.device_name, st.device_mode)) == -1) {
        fprintf(stderr, "Can not open %s\n", st.device_name);
        exit(1);
    }
    
    printf("device %s has been opened, st.device_d = %d\n", st.device_name, st.device_d);
    
    if((size = lseek(st.device_d, 0L, SEEK_END)) == -1) {
        fprintf(stderr, "Error seeking %s\n", st.device_name);
        exit(1);
    }
    
    //printf("size = %ld\n", size);
    
    if(size % K != 0) {
        printf("Device size is not a multiple of 4096\n");
        printf("The (partial) last block will not be accessible\n");
    }
    
    /* initialize the rest of state record */
    sync();
    
    printf("Read super block...\n");
    read_super_block(&st);
    read_bit_map(&st);
    st.address = 0L;
    
    /* recover percedure */
    inode = find_del_entry(&st, str);
    printf("The inode number for the file to be recovered is %ld\n", inode);
    
    if(inode == 0) {
        unlink(st.file_name);
        fprintf(stderr, "Recover aborted: inode error!");
        exit(1);
    }
    
    st.address = ((long)st.first_data - st.inode_blocks) * K + (long)(inode - 1) * st.inode_size;
    printf("The address of i-node %ld is: %ld\n", inode, st.address);
    
    /* read inode block */
    read_block(&st, st.buffer);
    
    printf("i-node of the file has been read...\n");
    
    
    /* have found the lost i-node, now extract the block */
    if((size = recover_blocks(&st)) == -1L) {
        unlink(st.file_name);
        fprintf(stderr, "Recover aborted: recover block error!\n");
        exit(1);
    }
    
    printf("Recovered %ld bytes, written to file %s\n", size, st.file_name);
}

/* do_test()
 *
 */
void do_test(fstr)
char *fstr;
{
    printf("This is test for free inode\n");
    struct stat fstat;
    stat(fstr, &fstat);
    //free_inode(fstat.st_dev, fstat.st_ino);
    printf("i-node: %llu for file: %s has been demaged!\n", fstat.st_ino, fstr);
}
