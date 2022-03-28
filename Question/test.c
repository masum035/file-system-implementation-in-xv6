/**
 *
 * test.c: This file includes the tests for the simple file system
 * 
 */


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

#define NUM_TESTS 13
#define PASS 1
#define FAIL 0

// #define _DEBUG
#define TEST_WAIT_MILI 3000 // how many miliseconds do we wait before assuming a test is hung

#define BLOCK_SIZE 4096

int make_fs(char *name);
int mount_fs(char *name);
int umount_fs(char *name);

int fs_open (char *name);
int fs_close(int fd);

int fs_create(char *name);
int fs_delete(char *name);

int fs_read (int fd, void *buf, size_t nbyte);
int fs_write(int fd, void *buf, size_t nbyte);

int fs_get_filesize(int fd);
int fs_lseek(int fd, off_t offset);
int fs_truncate(int fd, off_t length);

//if your code compiles you pass test 0 for free
//==============================================================================
static int test0(void) {
    return PASS;
}


//basic mkfs and mount test
//==============================================================================
static int test1(void) {
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

//create/open close/delete test
//==============================================================================
static int test2(void) {
    int rtn, fd;

    make_fs ("disk.2");
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

//basic write/read test
//==============================================================================
static int test3(void) {
    int rtn, fd;
    char buf[20];
    char str[] = "hello world";

    memset(buf, 0, sizeof(buf));

    make_fs ("disk.3");
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

    if (strcmp(str, buf))
        return FAIL;

    fs_close(fd);
    umount_fs("disk.3");

    return PASS;
}


//read/write pointer  test
//==============================================================================
static int test4(void) {
    int rtn, fd;
    char wt[11];
    char rd[11];

    memset(wt, 0, 11);
    memset(rd, 0, 11);
    memset(wt, 'a', 10);

    make_fs ("disk.4");
    mount_fs("disk.4");

    fs_create("file.4");
    fd = fs_open("file.4");

    fs_write(fd, wt, 10);

    rtn = fs_read(fd, rd, 10);
    if (rtn != 0)
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
    umount_fs("disk.4");

    return PASS;
}


//multiple block write test
//==============================================================================
static int test5(void) {
    int rtn, fd;
    char wt[BLOCK_SIZE*2+1];
    char rd[BLOCK_SIZE*2+1];

    memset(rd, 0, BLOCK_SIZE*2+1);
    memset(wt, 0, BLOCK_SIZE*2+1);
    memset(wt, 'a', BLOCK_SIZE*2);

    make_fs ("disk.5");
    mount_fs("disk.5");

    fs_create("file.5");
    
    fd = fs_open("file.5");

    rtn = fs_write(fd, wt, BLOCK_SIZE*2);
    if (rtn != BLOCK_SIZE*2)
        return FAIL;

    fs_lseek(fd, 0);

    rtn = fs_read(fd, rd, BLOCK_SIZE*2);
    if (rtn != BLOCK_SIZE*2)
        return FAIL;

    if (strcmp(rd, wt))
        return FAIL;

    fs_close(fd);
    umount_fs("disk.5");

    return PASS;
}


//write and umount test
//==============================================================================
static int test6(void) {
    int rtn, fd;
    char wt[] = "hello world";
    char rd[20];

    memset(rd, 0, sizeof(rd));

    make_fs ("disk.6");
    mount_fs("disk.6");

    fs_create("file.6");
    fd = fs_open("file.6");

    fs_write(fd, wt, strlen(wt));
    fs_close(fd);

    umount_fs("disk.6");

    rtn = mount_fs("disk.6");
    if (rtn)
        return FAIL;

    fd = fs_open("file.6");
    fs_read(fd, rd, 20);

    if (strcmp(rd, wt))
        return FAIL;

    fs_close(fd);
    umount_fs("disk.6");

    return PASS;
}


//lseek, write, and get_filesize
//==============================================================================
static int test7(void) {
    int fd;
    char wt1[BLOCK_SIZE];
    char wt2[10];
    char rd[11];

    memset(wt1, 'q', BLOCK_SIZE);
    memset(wt2, 'a', 10);
    memset(rd, 0, sizeof(rd));

    make_fs ("disk.7");
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
    umount_fs("disk.7");

    return PASS;
}


//truncate test
//==============================================================================
static int test8(void) {
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
    if (rtn != 0)
        return FAIL;

    if (fs_get_filesize(fd) != 64)
        return FAIL;

    fs_close(fd);
    umount_fs("disk.8");

    return PASS;
}


//multiple files test
//==============================================================================
static int test9(void) {
    int i, j;
    int  fd[16];
    char fname[32];
    char buf[4096];
    char r[1];
    char c = 'A';

    make_fs("disk.9");
    mount_fs("disk.9");


    for (i = 0; i < 16; i++) {
        snprintf(fname, 32, "file9.%i", i);
        fs_create(fname);
    }

    for (i = 0; i < 16; i++) {
        snprintf(fname, 32, "file9.%i", i);
        fd[i] = fs_open(fname);

        for (j = 0; j < 4; j++) {
            memset(buf, c, 4096);
            fs_write(fd[i], buf, 4096);
            c++;
        }
        c = 'A';
        fs_close(fd[i]);
    }

    for (i = 0; i < 16; i++) {
        snprintf(fname, 32, "file9.%i", i);
        fd[i] = fs_open(fname);

        for (j = 0; j < 4; j++) {
            fs_read(fd[i], r, 1);
            if (r[0] != c)
                return FAIL;
            fs_lseek(fd[i], (j+1)*4096);
            c++;
        }
        c = 'A';
        fs_close(fd[i]);
    }

    umount_fs("disk.9");

    return PASS;
}


//single file stress test
//==============================================================================
static int test10(void) {
    int fd, i; 
    char buf[4096];
    char rd[1];
    char c;

    make_fs ("disk.10");
    mount_fs("disk.10");

    fs_create("file.10");
    fd = fs_open("file.10");

    /* first 128 blocks are 'A' */
    c = 'A';
    memset(buf, c, 4096);
    for (i = 0; i < 128; i++) {
        fs_write(fd, buf, 4096);
    }

    if (fs_get_filesize(fd) != 128*4096)
        return FAIL;

    fs_lseek(fd, 64*4096);

    /* blocks 65-97 are 'B' */
    c = 'B';
    memset(buf, c, 4096);
    for (i = 0; i < 32; i++) {
        fs_write(fd, buf, 4096);
    }

    if (fs_get_filesize(fd) != 128*4096)
        return FAIL;
    
    fs_lseek(fd, 128*4096);

    /* blocks 129-193 are 'C' */
    c = 'C';
    memset(buf, c, 4096);
    for (i = 0; i < 64; i++) {
        fs_write(fd, buf, 4096);
    }

    if (fs_get_filesize(fd) != (128+64)*4096)
        return FAIL;

    /* block 10  - A
     * block 80  - B
     * block 100 - A
     * block 160 - C
     */
    fs_lseek(fd, 10*4096);
    fs_read(fd, rd, 1);
    if (rd[0] != 'A')
        return FAIL;
    
    fs_lseek(fd, 80*4096);
    fs_read(fd, rd, 1);
    if (rd[0] != 'B')
        return FAIL;

    fs_lseek(fd, 100*4096);
    fs_read(fd, rd, 1);
    if (rd[0] != 'A')
        return FAIL;
    
    fs_lseek(fd, 160*4096);
    fs_read(fd, rd, 1);
    if (rd[0] != 'C')
        return FAIL;

    fs_close(fd);
    umount_fs("disk.10");

    return PASS;
}


//fs error test
//==============================================================================
static int test11(void) {
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

    umount_fs("disk.11");

    return PASS;
}


//file error test
//==============================================================================
static int test12(void) {
    int rtn, fd;
    char buf[10];
    char hw[] = "hello world";

    make_fs ("disk.12");
    mount_fs("disk.12");

    fs_create("file.12");
    rtn = fs_create("file.12");
    if (rtn != -1)
        return FAIL;

    fd = fs_open("file.12");

    rtn = fs_delete("file.12");
    if (rtn != -1)
        return FAIL;

    rtn = fs_write(fd+1, buf, 10);
    if (rtn != -1)
        return FAIL;

    fs_write(fd, hw, strlen(hw));
    fs_lseek(fd, 0);

    rtn = fs_read(fd+1, buf, 10);
    if (rtn != -1)
        return FAIL;

    rtn = fs_truncate(fd, 100);
    if (rtn != -1)
        return FAIL;

    rtn = fs_close(fd+1);
    if (rtn != -1)
        return FAIL;

    fs_close(fd);
    umount_fs("disk.12");

    return PASS;
}


//end of tests
//==============================================================================


/**
 *  Some implementation details: Main spawns a child process for each
 *  test, that way if test 2/20 segfaults, we can still run the remaining
 *  tests. It also hands the child a pipe to write the result of the test.
 *  the parent polls this pipe, and counts the test as a failure if there
 *  is a timeout (which would indicate the child is hung).
 */


static int (*test_arr[NUM_TESTS])(void) = {&test0, &test1,  &test2,
                                           &test3, &test4,  &test5,
                                           &test6, &test7,  &test8,
                                           &test9, &test10, &test11,
                                           &test12};
// static int (*test_arr[NUM_TESTS])(void) = {&test9};

int main(void){
    
    int status; pid_t pid;
    char disk[10];
    int pipe_fd[2]; struct pollfd poll_fds;
    int score = 0; int total_score = 0;

    int devnull_fd = open("/dev/null", O_WRONLY);

    pipe(pipe_fd);
    poll_fds.fd = pipe_fd[0]; // only going to poll the read end of our pipe
    poll_fds.events = POLLRDNORM; //only care about normal read operations
        
    for(int i = 0; i < NUM_TESTS; i++){
        score = 0;
        pid = fork();

        //child, launches the test
        if (pid == 0){
#ifndef _DEBUG
            dup2(devnull_fd, STDOUT_FILENO); //begone debug messages
            dup2(devnull_fd, STDERR_FILENO);
#endif
            
            score = test_arr[i]();
            
            write(pipe_fd[1], &score, sizeof(score));
            exit(0); 
        }

        //parent, polls on the pipe we gave the child, kills the child,
        //keeps track of score
        else{  
            
            if(poll(&poll_fds, 1, TEST_WAIT_MILI)){
                read(pipe_fd[0], &score, sizeof(score));
            }
            
            total_score += score;
            kill(pid, SIGKILL);
            waitpid(pid,&status,0);
            
            
            if(score){
                printf("test %i : PASS\n", i);
            }
            else{
                printf("test %i : FAIL\n", i);
            }

            /* Delete Disk */
            sprintf(disk, "disk.%d", i);
            remove(disk);
        }
    }
    
    printf("total score was %i / %i\n", total_score, NUM_TESTS);
    return 0;
}
