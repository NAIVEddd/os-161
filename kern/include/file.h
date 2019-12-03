#ifndef _FILE_H
#define _FILE_H

#include <types.h>
#include <synch.h>
#include <lib.h>

struct vnode;

struct file {
    char * path;        // file name
    struct rwlock* lock; // protect race and offset
    struct lock* reflock;   // protect update refc
    uint32_t flags;     // open for read/write
    uint32_t mode;      // 0644 or something else
    // f_op
    volatile uint32_t refc;      // reference count
    uint64_t offset;    // r/w base pos
    uint64_t length;    // file content length      // Not used???
    struct vnode* inode;    // actual content
};

struct files_struct {
    char * procname;    // process' name
    struct lock* lock;  // protect self
    volatile uint32_t count;     // opened files num
    volatile uint32_t capacity;  // length of files array
    volatile uint32_t refc;      // reference count
    struct file** fds;
};

// open stdin/stdout/stderr file by default
struct files_struct* files_struct_create(char* name);
uint32_t files_struct_incref(struct files_struct * files);
uint32_t files_struct_decref(struct files_struct * files);
void files_struct_destroy(struct files_struct * files);

ssize_t files_struct_append(struct files_struct * files, struct file* f);

struct file* files_struct_remove(struct files_struct * files, size_t fd);

//inline 
bool files_struct_check_idx(struct files_struct * files, size_t fd, bool has_lock);

//inline 
struct file* files_struct_get(struct files_struct * files, size_t fd);

//inline 
void files_struct_set(struct files_struct * files, size_t fd, struct file * file);

struct file* file_create(char* path, int flags, int mode);
void file_destroy(struct file* fp);

uint32_t file_addref(struct file* fp, bool has_lock);
uint32_t file_decref(struct file* fp, bool has_lock);

struct file* file_dup(struct file* fp);
int file_close(struct file* fp);
ssize_t file_read(struct file* fp, void* buf, size_t N);
ssize_t file_write(struct file* fp, const void* buf, size_t N, uint64_t* pos);
bool file_isseekable(struct file* fp);
off_t file_lseek(struct file* fp, off_t offset, int pos);


#endif