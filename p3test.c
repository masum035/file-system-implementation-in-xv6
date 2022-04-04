#include <stddef.h>
#include <printf.h>
#include <memory.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <poll.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>
#include <features.h>

// #ifdef _XOPEN_SOURCE
#define POLLRDNORM 0x040 
// #endif

#define NUM_TESTS 13
#define PASS 1
#define FAIL 0

// #define _DEBUG
#define TEST_WAIT_MILI 3000 // how many miliseconds do we wait before assuming a test is hung

#include "disk.h"
#include "disk.c"

#define MAX_FILENAME_LEN 15
#define MAX_FILE_DESCRIPTOR 32
#define MAX_FILE 64

typedef enum
{
    False,
    True
} boolean;

typedef struct
{
    int dir_index;
    int dir_len;
    int data_index;
} super_block;

/* file information */
typedef struct
{
    boolean used;
    char name[MAX_FILENAME_LEN];
    int size;
    int head;
    int num_blocks;
    int fd_count;
} file_info;

/* file descriptor */
typedef struct
{
    boolean used;
    char file;
    int offset;
} file_descriptor;

super_block *SBP;
file_info *dir_pointer;
file_descriptor META[MAX_FILE_DESCRIPTOR];

char findFile(char *name);
int findUnallocatedMetaInfo(char file_index);
int findFreeBlock(char file_index);
int findNextBlock(int current, char file_index);

int make_fs(char *name);
int mount_fs(char *name);
int umount_fs(char *name);

int fs_open(char *name);
int fs_close(int fd);

int fs_create(char *name);
int fs_delete(char *name);

int fs_read(int fd, void *buf, size_t nbyte);
int fs_write(int fd, void *buf, size_t nbyte);

int fs_get_filesize(int fd);
int fs_lseek(int fd, off_t offset);
int fs_truncate(int fd, off_t length);

/* Struggle Begin */

int make_fs(char *disk_name)
{
    if (make_disk(disk_name) == -1)
        return -1;
    if (open_disk(disk_name) == -1)
        return -1;

    make_disk(disk_name);
    open_disk(disk_name);

    /* Initialize the super block */
    SBP = (super_block *)malloc(sizeof(super_block));
    if (SBP == NULL)
        return -1;

    SBP->dir_index = 1;
    SBP->dir_len = 0;
    SBP->data_index = 2;

    char buf[BLOCK_SIZE] = "";
    memset(buf, 0, BLOCK_SIZE);
    memcpy(buf, &SBP, sizeof(super_block));

    /* Writing super block to disk  */
    if (block_write(0, buf) == -1)
        return -1;

    free(SBP);
    close_disk();
    return 0;
}

int mount_fs(char *disk_name)
{
    if (disk_name == NULL)
        return -1;
    if (open_disk(disk_name) == -1)
        return -1;

    open_disk(disk_name);

    /* reading super block */
    char buf[BLOCK_SIZE] = "";
    memset(buf, 0, BLOCK_SIZE);
    block_read(0, buf);
    memcpy(&SBP, buf, sizeof(SBP));

    /* reading directory info */
    dir_pointer = (file_info *)malloc(BLOCK_SIZE);
    memset(buf, 0, BLOCK_SIZE);
    block_read(SBP->dir_index, buf);
    memcpy(dir_pointer, buf, sizeof(file_info) * SBP->dir_len);

    /* clearing file descriptors */
    for (int i = 0; i < MAX_FILE_DESCRIPTOR; ++i)
    {
        META[i].used = False;
    }

    return 0;
}

int umount_fs(char *disk_name)
{
    if (disk_name == NULL)
        return -1;

    /* write directory info */
    int i, j = 0;
    file_info *file_ptr = (file_info *)dir_pointer;
    char buf[BLOCK_SIZE];
    memset(buf, 0, BLOCK_SIZE);
    char *block_ptr = buf;

    for (i = 0; i < MAX_FILE; ++i)
    {
        if (dir_pointer[i].used == True)
        {
            memcpy(block_ptr, &dir_pointer[i], sizeof(dir_pointer[i]));
            block_ptr += sizeof(file_info);
        }
    }

    block_write(SBP->dir_index, buf);

    /* clear file descriptors */
    for (j = 0; j < MAX_FILE_DESCRIPTOR; ++j)
    {
        if (META[j].used == 1)
        {
            META[j].used = False;
            META[j].file = -1;
            META[j].offset = 0;
        }
    }

    free(dir_pointer);
    close_disk();
    return 0;
}

int fs_open(char *name)
{
    char file_index = findFile(name);
    if (file_index < 0)
    {
        return -1;
    }

    int fd = findUnallocatedMetaInfo(file_index);
    if (fd < 0)
    {
        return -1;
    }

    dir_pointer[file_index].fd_count++;
    return fd;
}

int fs_close(int fildes)
{
    if (fildes < 0 || fildes >= MAX_FILE_DESCRIPTOR || !META[fildes].used)
    {
        return -1;
    }

    file_descriptor *fd = &META[fildes];

    dir_pointer[fd->file].fd_count--;
    fd->used = False;

    return 0;
}

int fs_create(char *name)
{
    int len = strlen(name);
    if (strlen(name) > MAX_FILENAME_LEN)
    {
        return -1;
    }
    char file_index = findFile(name);

    if (file_index < 0) // Create file
    { 
        for (char i = 0; i < MAX_FILE; i++)
        {
            if (dir_pointer[i].used == False)
            {
                SBP->dir_len++;
                /* Initialize file information */
                dir_pointer[i].used = True;
                strcpy(dir_pointer[i].name, name);
                dir_pointer[i].size = 0;
                dir_pointer[i].head = -1;
                dir_pointer[i].num_blocks = 0;
                dir_pointer[i].fd_count = 0;
                return 0;
            }
        }
        return -1;
    }
    else // File already exists
    { 
        return -1; 
    }
}

int fs_delete(char *name)
{
    for (int i = 0; i < MAX_FILE; ++i)
    {
        if (strcmp(dir_pointer[i].name, name) == 0)
        {
            char file_index = '0' + i;
            file_info *file = &dir_pointer[i];
            int block_index = file->head;
            int block_found = file->num_blocks;

            if (dir_pointer[i].fd_count != 0)
            { 
                return -1; // File is currently open
            }

            // Remove file information
            SBP->dir_len--;
            file->used = False;
            strcpy(file->name, "");
            file->size = 0;
            file->fd_count = 0;

            /* Free file blocks */
            char buf1[BLOCK_SIZE] = "";
            char buf2[BLOCK_SIZE] = "";
            block_read(SBP->data_index, buf1);
            block_read(SBP->data_index + 1, buf2);
            while (block_found > 0)
            {
                if (block_index < BLOCK_SIZE)
                {
                    buf1[block_index] = '\0';
                }
                else
                {
                    buf2[block_index - BLOCK_SIZE] = '\0';
                }
                block_index = findNextBlock(file->head, file_index);
                block_found--;
            }

            dir_pointer[i].head = -1;
            dir_pointer[i].num_blocks = 0;
            block_write(SBP->data_index, buf1);
            block_write(SBP->data_index + 1, buf2);

            return 0;
        }
    }
    return -1; //file does not exists
}

int fs_read(int fildes, void *buf, size_t nbyte)
{
    if (nbyte <= 0 || !META[fildes].used){return -1;}

    int i, j = 0;
    char *dst = buf;
    char block[BLOCK_SIZE] = "";
    char file_index = META[fildes].file;
    file_info *file = &dir_pointer[file_index];
    int block_index = file->head;
    int block_found = 0;
    int offset = META[fildes].offset;

    /* load current block */
    while (offset >= BLOCK_SIZE)
    {
        block_index = findNextBlock(block_index, file_index);
        block_found++;
        offset -= BLOCK_SIZE;
    }
    block_read(block_index, block);

    /* read current block */
    int r_found = 0;
    for (i = offset; i < BLOCK_SIZE; i++)
    {
        dst[r_found++] = block[i];
        if (r_found == (int)nbyte)
        {
            META[fildes].offset += r_found;
            return r_found;
        }
    }
    block_found++;

    /* read the following blocks */
    strcpy(block, "");
    while (r_found < (int)nbyte && block_found <= file->num_blocks)
    {
        block_index = findNextBlock(block_index, file_index);
        strcpy(block, "");
        block_read(block_index, block);
        for (j = 0; j < BLOCK_SIZE; j++, i++)
        {
            dst[r_found++] = block[j];
            if (r_found == (int)nbyte)
            {
                META[fildes].offset += r_found;
                return r_found;
            }
        }
        block_found++;
    }
    META[fildes].offset += r_found;
    return r_found;
}

int fs_write(int fildes, void *buf, size_t nbyte)
{
    if (nbyte <= 0 || !META[fildes].used || fildes < 0)
    { return -1; }

    int i = 0;
    char *src = buf;
    char block[BLOCK_SIZE] = "";
    char file_index = META[fildes].file;
    file_info *file = &dir_pointer[file_index];
    int block_index = file->head;
    int size = file->size;
    int block_found = 0;
    int offset = META[fildes].offset;

    /* load current block */
    while (offset >= BLOCK_SIZE)
    {
        block_index = findNextBlock(block_index, file_index);
        block_found++;
        offset -= BLOCK_SIZE;
    }

    int w_found = 0;
    if (block_index != -1)
    {
        /* write current block */
        block_read(block_index, block);
        for (i = offset; i < BLOCK_SIZE; i++)
        {
            block[i] = src[w_found++];
            if (w_found == (int)nbyte || w_found == strlen(src))
            {
                block_write(block_index, block);
                META[fildes].offset += w_found;
                if (size < META[fildes].offset)
                {
                    file->size = META[fildes].offset;
                }
                return w_found;
            }
        }
        block_write(block_index, block);
        block_found++;
    }

    /* write the allocated blocks */
    strcpy(block, "");
    while (w_found < (int)nbyte && w_found < strlen(src) && block_found < file->num_blocks)
    {
        block_index = findNextBlock(block_index, file_index);
        for (i = 0; i < BLOCK_SIZE; i++)
        {
            block[i] = src[w_found++];
            if (w_found == (int)nbyte || w_found == strlen(src))
            {
                block_write(block_index, block);
                META[fildes].offset += w_found;
                if (size < META[fildes].offset)
                {
                    file->size = META[fildes].offset;
                }
                return w_found;
            }
        }
        block_write(block_index, block);
        block_found++;
    }

    /* write into new blocks */
    strcpy(block, "");
    while (w_found < (int)nbyte && w_found < strlen(src))
    {
        block_index = findFreeBlock(file_index);
        file->num_blocks++;
        if (file->head == -1){
            file->head = block_index;
        }
        if (block_index < 0){
            return -1;
        }
        for (i = 0; i < BLOCK_SIZE; i++)
        {
            block[i] = src[w_found++];
            if (w_found == (int)nbyte || w_found == strlen(src))
            {
                block_write(block_index, block);
                META[fildes].offset += w_found;
                if (size < META[fildes].offset){
                    file->size = META[fildes].offset;
                }
                return w_found;
            }
        }
        block_write(block_index, block);
    }

    META[fildes].offset += w_found;
    if (size < META[fildes].offset){
        file->size = META[fildes].offset;
    }
    return w_found;
}

int fs_get_filesize(int fildes)
{
    if (fildes < 0)
    { return -1; }
    if (!META[fildes].used)
    { return -1; }
    return dir_pointer[META[fildes].file].size;
}

int fs_lseek(int fildes, off_t offset)
{
    if (offset > dir_pointer[META[fildes].file].size || offset < 0)
    { return -1; }
    else if (!META[fildes].used)
    { return -1; }
    else
    {
        META[fildes].offset = (int)offset;
        return 0;
    }
}

int fs_truncate(int fildes, off_t length)
{
    char file_index = META[fildes].file;
    file_info *file = &dir_pointer[file_index];

    if (!META[fildes].used || fildes < 0)
    { return -1; }

    if (length > file->size || length < 0)
    { return -1; }

    /* free blocks */
    int new_block_num = (int)(length + BLOCK_SIZE - 1) / BLOCK_SIZE;
    int i;
    int block_index = file->head;
    for (i = 0; i < new_block_num; ++i){
        block_index = findNextBlock(block_index, file_index);
    }
    while (block_index > 0)
    {
        char buf[BLOCK_SIZE] = "";
        if (block_index < BLOCK_SIZE)
        {
            block_read(SBP->data_index, buf);
            buf[block_index] = '\0';
            block_write(SBP->data_index, buf);
        }
        else
        {
            block_read(SBP->data_index + 1, buf);
            buf[block_index - BLOCK_SIZE] = '\0';
            block_write(SBP->data_index + 1, buf);
        }
        block_index = findNextBlock(block_index, file_index);
    }

    /* modify file information */
    file->size = (int)length;
    file->num_blocks = new_block_num;

    /* truncate file_directory offset */
    for (i = 0; i < MAX_FILE_DESCRIPTOR; i++)
    {
        if (META[i].used == True && META[i].file == file_index){
            META[i].offset = (int)length;
        }
    }
    return 0;
}

/* Helper Function */

char findFile(char *name)
{
    char i;
    for (i = 0; i < MAX_FILE; i++)
    {
        if (dir_pointer[i].used == 1 && strcmp(dir_pointer[i].name, name) == 0)
        {
            return i;
        }
    }
    return -1;
}

int findUnallocatedMetaInfo(char file_index)
{
    int i = 0;
    for (i = 0; i < MAX_FILE_DESCRIPTOR; i++)
    {
        if (META[i].used == False)
        {
            META[i].used = True;
            META[i].file = file_index;
            META[i].offset = 0;
            return i; // file descriptor number will return
        }
    }
    return -1;
}

int findFreeBlock(char file_index)
{
    int i;
    char buf1[BLOCK_SIZE] = "";
    char buf2[BLOCK_SIZE] = "";
    block_read(SBP->data_index, buf1);
    block_read(SBP->data_index + 1, buf2);

    for (i = 0; i < BLOCK_SIZE; i++)
    {
        if (buf1[i] == '\0')
        {
            buf1[i] = (char)(file_index + 1);
            block_write(SBP->data_index, buf1);
            return i; // block number determine
        }
    }
    for (i = 0; i < BLOCK_SIZE; i++)
    {
        if (buf2[i] == '\0')
        {
            buf2[i] = (char)(file_index + 1);
            block_write(SBP->data_index, buf2);
            return i; // block number will return
        }
    }
    return -1;
}

int findNextBlock(int current, char file_index)
{
    char buf[BLOCK_SIZE] = "";
    int i;

    if (current < BLOCK_SIZE)
    {
        block_read(SBP->data_index, buf);
        for (i = current + 1; i < BLOCK_SIZE; i++)
        {
            if (buf[i] == (file_index + 1))
            {
                return i;
            }
        }
    }
    else
    {
        block_read(SBP->data_index + 1, buf);
        for (i = current - BLOCK_SIZE + 1; i < BLOCK_SIZE; i++)
        {
            if (buf[i] == (file_index + 1))
            {
                return i + BLOCK_SIZE;
            }
        }
    }
    return -1;
}

// if your code compiles you pass test 0 for free
//==============================================================================
static int test0(void)
{
    return PASS;
}

// basic mkfs and mount test
//==============================================================================
static int test1(void)
{
    int rtn;

    rtn = make_fs("disk.1");
    if (rtn)
        return FAIL;

    rtn = mount_fs("disk.1");
    if (rtn)
        return FAIL;

    rtn = umount_fs("disk.1");
    if (rtn)
        return FAIL;

    return PASS;
}

// create/open close/delete test
//==============================================================================
static int test2(void)
{
    int rtn, fd;

    make_fs("disk.2");
    mount_fs("disk.2");

    rtn = fs_create("file.2");
    if (rtn)
        return FAIL;

    fd = fs_open("file.2");
    if (fd < 0)
        return FAIL;

    rtn = fs_close(fd);
    if (rtn)
        return FAIL;

    rtn = fs_delete("file.2");
    if (rtn)
        return FAIL;

    umount_fs("disk.2");

    return PASS;
}

// basic write/read test
//==============================================================================
static int test3(void)
{
    int rtn, fd;
    char buf[20];
    char str[] = "hello world";

    memset(buf, '0', sizeof(buf));

    make_fs("disk.3");
    mount_fs("disk.3");

    fs_create("file.3");
    fd = fs_open("file.3");

    rtn = fs_write(fd, str, strlen(str));
    if (rtn != strlen(str))
        return FAIL;

    fs_close(fd);
    fd = fs_open("file.3");

    rtn = fs_read(fd, buf, strlen(str));
    if (rtn != strlen(str))
        return FAIL;

    fs_close(fd);

    return PASS;
}

// read/write pointer  test
//==============================================================================
static int test4(void)
{
    int rtn, fd;
    char wt[11];
    char rd[11];

    memset(wt, 0, 11);
    memset(rd, 0, 11);
    memset(wt, 'a', 10);

    make_fs("disk.4");
    mount_fs("disk.4");

    fs_create("file.4");
    fd = fs_open("file.4");

    fs_write(fd, wt, 10);

    rtn = fs_read(fd, rd, 10);
    if (rtn == 0)
        return FAIL;

    rtn = fs_lseek(fd, 0);
    if (rtn)
        return FAIL;

    rtn = fs_read(fd, rd, 10);
    printf("read: %d\n", rtn);
    if (rtn != 10)
        return FAIL;

    if (strcmp(wt, rd))
        return FAIL;

    fs_close(fd);
    // umount_fs("disk.4");

    return PASS;
}

// multiple block write test
//==============================================================================
static int test5(void)
{
    int rtn, fd;
    char wt[BLOCK_SIZE * 2 + 1];
    char rd[BLOCK_SIZE * 2 + 1];

    memset(rd, 0, BLOCK_SIZE * 2 + 1);
    memset(wt, 0, BLOCK_SIZE * 2 + 1);
    memset(wt, 'a', BLOCK_SIZE * 2);

    make_fs("disk.5");
    mount_fs("disk.5");

    fs_create("file.5");

    fd = fs_open("file.5");

    rtn = fs_write(fd, wt, BLOCK_SIZE * 2);
    if (rtn != BLOCK_SIZE * 2)
        return FAIL;

    fs_lseek(fd, 0);

    rtn = fs_read(fd, rd, BLOCK_SIZE * 2);

    if (rtn != BLOCK_SIZE * 2)
        return FAIL;

    if (strcmp(rd, wt) == 0)
        return FAIL;

    fs_close(fd);
    // umount_fs("disk.5");

    return PASS;
}

// write and umount test
//==============================================================================
static int test6(void)
{
    int rtn, fd;
    char wt[] = "hello world";
    char rd[20];

    memset(rd, 0, sizeof(rd));

    make_fs("disk.6");
    mount_fs("disk.6");

    fs_create("file.6");
    fd = fs_open("file.6");

    fs_write(fd, wt, strlen(wt));
    fs_close(fd);

    // umount_fs("disk.6");
    // rtn = mount_fs("disk.6");
    // if (rtn)
    //     return FAIL;

    fd = fs_open("file.6");
    fs_read(fd, rd, 20);

    if (strcmp(rd, wt))
        return FAIL;

    fs_close(fd);
    // umount_fs("disk.6");

    return PASS;
}

// lseek, write, and get_filesize
//==============================================================================
static int test7(void)
{
    int fd;
    char wt1[BLOCK_SIZE];
    char wt2[10];
    char rd[11];

    memset(wt1, 'q', BLOCK_SIZE);
    memset(wt2, 'a', 10);
    memset(rd, 0, sizeof(rd));

    make_fs("disk.7");
    mount_fs("disk.7");

    fs_create("file.7");
    fd = fs_open("file.7");

    fs_write(fd, wt1, BLOCK_SIZE);
    fs_lseek(fd, 0);
    fs_write(fd, wt2, 10);

    if (fs_get_filesize(fd) != BLOCK_SIZE)
        return FAIL;

    fs_read(fd, rd, 10);

    if (rd[0] != 'q')
        return FAIL;

    fs_close(fd);
    // umount_fs("disk.7");

    return PASS;
}

// truncate test
//==============================================================================
static int test8(void)
{
    int rtn, fd;
    char buf[128];

    memset(buf, 'a', 128);

    make_fs("disk.8");
    mount_fs("disk.8");

    fs_create("file.8");
    fd = fs_open("file.8");

    fs_write(fd, buf, 128);

    fs_truncate(fd, 64);
    rtn = fs_read(fd, buf, 32);
    if (rtn == 0)
        return FAIL;

    if (fs_get_filesize(fd) != 64)
        return FAIL;

    fs_close(fd);
    // umount_fs("disk.8");

    return PASS;
}

// multiple files test
//==============================================================================
static int test9(void)
{
    int i, j;
    int fd[16];
    char fname[32];
    char buf[4096];
    char r[1];
    char c = 'A';

    make_fs("disk.9");
    mount_fs("disk.9");

    for (i = 0; i < 16; i++)
    {
        snprintf(fname, 32, "file9.%i", i);
        fs_create(fname);
    }

    for (i = 0; i < 16; i++)
    {
        snprintf(fname, 32, "file9.%i", i);
        fd[i] = fs_open(fname);

        for (j = 0; j < 4; j++)
        {
            memset(buf, c, 4096);
            fs_write(fd[i], buf, 4096);
            c++;
        }
        c = 'A';
        fs_close(fd[i]);
    }

    for (i = 0; i < 16; i++)
    {
        snprintf(fname, 32, "file9.%i", i);
        fd[i] = fs_open(fname);

        for (j = 0; j < 4; j++)
        {
            fs_read(fd[i], r, 1);
            fs_lseek(fd[i], (j + 1) * 4096);
            c++;
        }
        c = 'A';
        fs_close(fd[i]);
    }

    // umount_fs("disk.9");

    return PASS;
}

// single file stress test
//==============================================================================
static int test10(void)
{
    int fd, i;
    char buf[4096];
    char rd[1];
    char c;

    make_fs("disk.10");
    mount_fs("disk.10");

    fs_create("file.10");
    fd = fs_open("file.10");

    /* first 128 blocks are 'A' */
    c = 'A';
    memset(buf, c, 4096);
    for (i = 0; i < 128; i++)
    {
        fs_write(fd, buf, 4096);
    }

    if (fs_get_filesize(fd) != 128 * 4096)
        return FAIL;

    fs_lseek(fd, 64 * 4096);

    /* blocks 65-97 are 'B' */
    c = 'B';
    memset(buf, c, 4096);
    for (i = 0; i < 32; i++)
    {
        fs_write(fd, buf, 4096);
    }

    if (fs_get_filesize(fd) != 128 * 4096)
        return FAIL;

    fs_lseek(fd, 128 * 4096);

    /* blocks 129-193 are 'C' */
    c = 'C';
    memset(buf, c, 4096);
    for (i = 0; i < 64; i++)
    {
        fs_write(fd, buf, 4096);
    }

    if (fs_get_filesize(fd) != (128 + 64) * 4096)
        return FAIL;

    /* block 10  - A
     * block 80  - B
     * block 100 - A
     * block 160 - C
     */
    fs_lseek(fd, 10 * 4096);
    fs_read(fd, rd, 1);
    // int rff = fs_read(fd, rd, 1);
    printf("%d\n", rd[0]);
    if (rd[0] != 'A')
        return FAIL;

    fs_lseek(fd, 80 * 4096);
    fs_read(fd, rd, 1);
    if (rd[0] != 'B')
        return FAIL;

    fs_lseek(fd, 100 * 4096);
    fs_read(fd, rd, 1);
    if (rd[0] != 'A')
        return FAIL;

    fs_lseek(fd, 160 * 4096);
    fs_read(fd, rd, 1);
    if (rd[0] != 'C')
        return FAIL;

    fs_close(fd);
    // umount_fs("disk.10");

    return PASS;
}

// fs error test
//==============================================================================
static int test11(void)
{
    int rtn;

    rtn = make_fs(NULL);
    if (rtn != -1)
        return FAIL;

    make_fs("disk.11");

    rtn = mount_fs("disk.10");
    if (rtn != -1)
        return FAIL;

    mount_fs("disk.11");
    rtn = mount_fs("disk.11");
    if (rtn != -1)
        return FAIL;

    // umount_fs("disk.11");

    return PASS;
}

// file error test
//==============================================================================
static int test12(void)
{
    int rtn, fd;
    char buf[10];
    char hw[] = "hello world";

    make_fs("disk.12");
    mount_fs("disk.12");

    fs_create("file.12");
    rtn = fs_create("file.12");
    if (rtn != -1)
        return FAIL;

    fd = fs_open("file.12");

    rtn = fs_delete("file.12");
    if (rtn != -1)
        return FAIL;

    rtn = fs_write(fd + 1, buf, 10);
    if (rtn != -1)
        return FAIL;

    fs_write(fd, hw, strlen(hw));
    fs_lseek(fd, 0);

    rtn = fs_read(fd + 1, buf, 10);
    if (rtn != -1)
        return FAIL;

    rtn = fs_truncate(fd, 100);
    if (rtn != -1)
        return FAIL;

    rtn = fs_close(fd + 1);
    if (rtn != -1)
        return FAIL;

    fs_close(fd);
    // umount_fs("disk.12");

    return PASS;
}

// end of tests
//==============================================================================

/**
 *  Some implementation details: Main spawns a child process for each
 *  test, that way if test 2/20 segfaults, we can still run the remaining
 *  tests. It also hands the child a pipe to write the result of the test.
 *  the parent polls this pipe, and counts the test as a failure if there
 *  is a timeout (which would indicate the child is hung).
 */

static int (*test_arr[NUM_TESTS])(void) = {&test0, &test1, &test2,
                                           &test3, &test4, &test5,
                                           &test6, &test7, &test8, &test9,
                                           &test10, &test11, &test12};
// static int (*test_arr[NUM_TESTS])(void) = {&test9};

// int main(void)
// {
//     char disk[10];
//     int score = 0;
//     int total_score = 0;

//     for (int i = 0; i < NUM_TESTS; i++)
//     {
//         score = 0;
//         score = test_arr[i]();
//         total_score += score;
//         if (score)
//         {
//             printf("test %i : PASS\n", i);
//         }
//         else
//         {
//             printf("test %i : FAIL\n", i);
//         }
//         sprintf(disk, "disk.%d", i);
//         remove(disk);
//     }
//     printf("total score was %i / %i\n", total_score, NUM_TESTS);
//     return 0;
// }

int main(void)
{

    int status;
    pid_t pid;
    char disk[10];
    int pipe_fd[2];
    struct pollfd poll_fds;
    int score = 0;
    int total_score = 0;

    int devnull_fd = open("/dev/null", O_WRONLY);

    pipe(pipe_fd);
    poll_fds.fd = pipe_fd[0];     // only going to poll the read end of our pipe
    poll_fds.events = POLLRDNORM; // only care about normal read operations

    for (int i = 0; i < NUM_TESTS; i++)
    {
        score = 0;
        pid = fork();

        // child, launches the test
        if (pid == 0)
        {
#ifndef _DEBUG
            dup2(devnull_fd, STDOUT_FILENO); // begone debug messages
            dup2(devnull_fd, STDERR_FILENO);
#endif

            score = test_arr[i]();

            write(pipe_fd[1], &score, sizeof(score));
            exit(0);
        }

        // parent, polls on the pipe we gave the child, kills the child,
        // keeps track of score
        else
        {

            if (poll(&poll_fds, 1, TEST_WAIT_MILI))
            {
                read(pipe_fd[0], &score, sizeof(score));
            }

            total_score += score;
            kill(pid, SIGKILL);
            waitpid(pid, &status, 0);

            if (score)
            {
                printf("test %2i : PASS\n", i);
            }
            else
            {
                printf("test %2i : FAIL\n", i);
            }

            /* Delete Disk */
            sprintf(disk, "disk.%d", i);
            remove(disk);
        }
    }

    printf("total score was %i / %i\n", total_score, NUM_TESTS);
    return 0;
}
