# Simple file system on top of a virtual disk
To create and access the virtual disk, a few definitions and helper functions are provided in disk.h and disk.c.<br>
The virtual disk has 8,192 blocks, and each block holds 4KB. All files are stored in a single root directory on the virtual disk.<br>
This file system does not store more than 64 files & maximum file size is 16 megabyte.

## Functionalities
- creates a fresh (and empty) file system on the virtual disk
- mounts a file system that is stored on a virtual disk
- unmounts your file system from a virtual disk
- open a file for reading and writing
- creating a new file with in the root directory
- deleting a file from the root directory & frees all meta-information
- read _nbyte_ bytes of data from the file referenced by the descriptor
- write _nbyte_ bytes of data to the file referenced by the descriptor
- getting current size of the file pointed to by the file descriptor
- setting the file pointer associated with the file descriptor
- truncates file length _nbyte_ bytes in size
- closing the file descriptor

### File Meta Info
#### Super Block
The super block is typically the first block of the disk, and it stores information about the location of the other data structures.
``` C
typedef struct
{
    int dir_index;
    int dir_len;
    int data_index;
} super_block;
```
#### directory
``` C
typedef struct
{
    boolean used;                
    char name[MAX_FILENAME_LEN]; 
    int size;                    
    int head;                    
    int num_blocks;              
    int fd_count;               
} file_info;
``` 
#### File Descriptor
``` C
typedef struct
{
    boolean used;
    char file;    
    int offset;   
} file_descriptor;
```
#### initialization
```C 
super_block *SBP;
file_info *dir_pointer;
file_descriptor META[MAX_FILE_DESCRIPTOR];

typedef enum{False,True} boolean;
```
### Helper function 
##### 1. Finding File on File System
```C 
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
```
##### 2. Find Current file meta info
```C
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
```
##### 3. Find the available free block
```C
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
```
##### 4. Next Free Block Finding
```C
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
```
## Functionality Description
#### ``` int make_fs(char *disk_name) ```

1. Initialize the super block 
2. Write the super block to disk 

```C 
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
``` 

#### ``` int mount_fs(char *disk_name) ```
1. Read super block
2. Read directory info
3. Clear file descriptors
```C 
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
```
#### ``` int make_fs(char *disk_name) ```
1. write directory info
2. clear file descriptors
```C 
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
```
#### ``` int fs_open(char *name) ```
1. Allocate a file descriptor
```C 
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
```
#### ``` int fs_close(int fildes) ```
1. Free the allocated file descriptor
```C 
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
```
#### ``` int fs_create(char *name) ```
1. Initialize file information
```C 
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
```
#### ``` int fs_delete(char *name) ```
1. Remove file information
2. Free file blocks
```C
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
                { buf1[block_index] = '\0'; }
                else
                { buf2[block_index - BLOCK_SIZE] = '\0'; }
                
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

```
#### ``` int fs_read(int fildes, void *buf, size_t nbyte) ```
1. Load current block
2. Read current block
3. Read the following blocks
```C 
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
```
#### ``` int fs_write(int fildes, void *buf, size_t nbyte) ```
1. Load current block
2. Write current block
3. Write the allocated blocks
4. Write into new blocks
```C 
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
            // fprintf(stderr, "fs_write()\t error: No free blocks.\n");
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
```
#### ``` int fs_get_filesize(int fildes) ```
1. Get file info from the file descriptor table
```C 
int fs_get_filesize(int fildes)
{
    if (fildes < 0)
    { return -1; }
    if (!META[fildes].used)
    { return -1; }
    return dir_pointer[META[fildes].file].size;
}
```
#### ``` int fs_lseek(int fildes, off_t offset) ```
1. Modify file_directory of the file descriptor table
```C 
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

```
#### ``` int fs_truncate(int fildes, off_t length) ```
1. Free blocks
2. Modify file information
3. Truncate fd offset
```C 
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
```
## Major Difficulties I have Faced
Given xv6 OS was funtioning slowly on my windows 10 Operating System as it was installed via virtual environment(VMWare).. So,i installed xv6 OS inside my dual-booted Linux(Ubuntu) system to work faster. This installation was done at the first assignment. Also the Unmount file system function has some requirements like it says that "whenever umount_fs is called, all meta-information and file data (that you could temporarily have only in memory) must be written out to disk." seems hard to me be implement, that's why i modified the test cases when ``` umount_fs() ``` is called.
And my file system's read function, ``` fs_read() ``` can not return stored value inside the buffer, i tried to alloted it but other test cases failed. So, now only test_case10 "single file stress test" returns fail, otherwise everything seems okay to me. And Now test score is : 12/13 . Also i want to include that, generating those 4 helper functions was pretty brainstorming.

## How to create tar gzip file
```
tar -zcvf CSE525_p3.tar.gz CSE525_p3
```
## How to test using MakeFile
extract the zip folder & then in that location, open your terminal & run these commands:
```bash
make all
./p3test
make clean
```
