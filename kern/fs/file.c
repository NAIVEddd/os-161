#include <types.h>
#include <lib.h>
#include <file.h>
#include <synch.h>
#include <proc.h>
#include <thread.h>
#include <uio.h>
#include <vfs.h>
#include <fs.h>
#include <vnode.h>
#include <copyinout.h>
#include <kern/seek.h>
#include <kern/stat.h>
#include <kern/errno.h>

#define FILES_STRUCT_DEFAULT_CAPACITY 3
void files_struct_ensurespace(struct files_struct * files, uint32_t sz, bool has_lock);

struct files_struct * files_struct_create(char* name)
{
    struct files_struct * files;

    files = kmalloc(sizeof(*files));
    if(files == NULL) {
        return NULL;
    }

    files->procname = kstrdup(name);
    if(files->procname == NULL) {
        kfree(files);
        return NULL;
    }

    files->lock = lock_create(name);
    if(files->lock == NULL) {
        kfree(files->procname);
        kfree(files);
        return NULL;
    }
    
    files->fds = kmalloc(FILES_STRUCT_DEFAULT_CAPACITY * sizeof(struct file*));
    if(files->fds == NULL) {
        lock_destroy(files->lock);
        kfree(files->procname);
        kfree(files);
        return NULL;
    }
    for(int i = 0; i != FILES_STRUCT_DEFAULT_CAPACITY; i++) {
        files->fds[i] = NULL;
    }

    files->count = 0;
    files->refc = 1;
    files->capacity = FILES_STRUCT_DEFAULT_CAPACITY;
    return files;
}

uint32_t files_struct_incref(struct files_struct * files)
{
    KASSERT(files != NULL);
    uint32_t res;

    lock_acquire(files->lock);
    files->refc ++;
    res = files->refc;
    lock_release(files->lock);
    return res;
}

uint32_t files_struct_decref(struct files_struct * files)
{
    KASSERT(files != NULL);
    uint32_t res;

    lock_acquire(files->lock);
    files->refc --;
    res = files->refc;
    lock_release(files->lock);
    return res;
}

void files_struct_destroy(struct files_struct * files)
{
    KASSERT(files != NULL);
    if(files_struct_decref(files) != 0) {
        return;
    }
    
    lock_acquire(files->lock);
    struct file* fp;
    for (size_t i = 0; i < files->capacity; i++)
    {
        fp = files->fds[i];
        if(fp) {
            file_destroy(fp);
        }
    }
    files->fds = NULL;
    lock_release(files->lock);
    
    lock_destroy(files->lock);
    kfree(files->procname);
    kfree(files->fds);
    files->lock = NULL;
    files->procname = NULL;
    kfree(files);
}

void files_struct_ensurespace(struct files_struct * files, uint32_t sz, bool has_lock)
{
    if(!has_lock) {
        lock_acquire(files->lock);
    }
    if(sz > files->capacity) {
        size_t oldcap = files->capacity;
        // no enough space, require more space
        while(sz > files->capacity) {
            files->capacity *= 2;
        }
        struct file** p = kmalloc(files->capacity * sizeof(struct file*));
        struct file** tmp = files->fds;
        for (size_t i = 0; i < oldcap; i++)
        {
            p[i] = tmp[i];
        }
        for (size_t i = oldcap; i < files->capacity; i++)
        {
            p[i] = NULL;
        }
        
        files->fds = p;
        kfree(tmp);
    }
    if(!has_lock) {
        lock_release(files->lock);
    }
}

ssize_t files_struct_append(struct files_struct * files, struct file * file)
{
    KASSERT(files != NULL);
    KASSERT(file != NULL);

    ssize_t idx = -1;
    lock_acquire(files->lock);

    files_struct_ensurespace(files, files->count + 1, true);
    
    // find first NULL pointer to place fp
    for (size_t i = 0; i < files->capacity; i++)
    {
        if(files->fds[i] == NULL) {
            idx = i;
            break;
        }
    }
    files->fds[idx] = file;
    files->count ++;
    lock_release(files->lock);

    return idx;
}

struct file* files_struct_remove(struct files_struct * files, size_t fd)
{
    KASSERT(files != NULL);

    struct file* fp = NULL;
    lock_acquire(files->lock);
    KASSERT(fd < files->capacity);

    fp = files->fds[fd];
    files->fds[fd] = NULL;
    if (fp != NULL) {
        files->count --;
    }
    lock_release(files->lock);
    return fp;
}

//inline 
bool files_struct_check_idx(struct files_struct * files, size_t fd, bool has_lock)
{
    bool retval = false;
    if(!has_lock){
        lock_acquire(files->lock);
    }
    retval = files->capacity > fd;
    if(!has_lock){
        lock_release(files->lock);
    }
    return retval;
}

//inline 
struct file* files_struct_get(struct files_struct * files, size_t fd)
{
    struct file* fp = NULL;
    bool valid = false;
    lock_acquire(files->lock);
    valid = files_struct_check_idx(files, fd, true);
    if(!valid) {
        lock_release(files->lock);
        return NULL;
    }
    fp = files->fds[fd];
    lock_release(files->lock);
    return fp;
}

//inline 
void files_struct_set(struct files_struct * files, size_t fd, struct file * file)
{
    KASSERT(files != NULL);
    KASSERT(file != NULL);

    struct file * fp = files_struct_remove(files, fd);
    if(fp != NULL) {
        file_destroy(fp);
        fp = NULL;
    }

    KASSERT(fd > 0);
    lock_acquire(files->lock);
    files_struct_ensurespace(files, fd, true);
    KASSERT(fd < files->capacity);
    files->fds[fd] = file;
    files->count ++;
    lock_release(files->lock);
}

struct file* file_create(char* path, int flags, int mode)
{
    char buf[256];
    struct file * fp = NULL;

    fp = kmalloc(sizeof(*fp));
    if(fp == NULL) {
        return NULL;
    }

    fp->lock = rwlock_create(path);
    if(fp->lock == NULL) {
        kfree(fp);
        return NULL;
    }

    fp->reflock = lock_create(path);
    if(fp->reflock == NULL) {
        rwlock_destroy(fp->lock);
        kfree(fp);
        return NULL;
    }

    strcpy(buf, path);
    int err = vfs_open(buf, flags, mode, &fp->inode);
    if (err) {
        lock_destroy(fp->reflock);
        rwlock_destroy(fp->lock);
        kfree(fp);
        return NULL;
    }

    fp->flags = flags;
    fp->mode = mode;
    fp->refc = 1;
    fp->offset = 0;
    fp->length = 0; //??

    return fp;
}

void file_destroy(struct file* fp)
{
    KASSERT(fp != NULL);
    if(file_decref(fp, false) != 0) {
        return;
    }
    rwlock_destroy(fp->lock);
    lock_destroy(fp->reflock);
    vfs_close(fp->inode);
    kfree(fp);
}

uint32_t file_addref(struct file* fp, bool has_lock)
{
    uint32_t ref;
    if(!has_lock) {
        lock_acquire(fp->reflock);
    }
    fp->refc ++;
    ref = fp->refc;
    if(!has_lock) {
        lock_release(fp->reflock);
    }
    return ref;
}

uint32_t file_decref(struct file* fp, bool has_lock)
{
    uint32_t ref;
    if(!has_lock) {
        lock_acquire(fp->reflock);
    }
    KASSERT(fp->refc > 0);
    fp->refc --;
    ref = fp->refc;
    if(!has_lock) {
        lock_release(fp->reflock);
    }
    return ref;
}

struct file* file_dup(struct file* fp)
{
    KASSERT(fp != NULL);
    file_addref(fp, false);
    return fp;
}

int file_close(struct file* fp)
{
    file_destroy(fp);
    return 0;
}

ssize_t file_read(struct file* fp, void* buf, size_t N)
{
    KASSERT(fp != NULL);

    struct iovec iov;
    struct uio ku;
    size_t len = 0;
    ssize_t err = 0;
    char* kbuf = kmalloc(N);
    
    rwlock_acquire_write(fp->lock);
    uio_kinit(&iov, &ku, kbuf, N, fp->offset, UIO_READ);
    err = VOP_READ(fp->inode, &ku);
    if(err) {
        kfree(kbuf);
        rwlock_release_write(fp->lock);
        return -1;
    }
    len = ku.uio_offset - fp->offset;
    fp->offset = ku.uio_offset;
    rwlock_release_write(fp->lock);

    KASSERT(len <= N);
    err = copyout(kbuf, buf, len);
    kfree(kbuf);
    if(err) {
        return -1;
    }
    return len;
}

ssize_t file_write(struct file* fp, const void* buf, size_t N, uint64_t * pos)
{
    KASSERT(fp != NULL);

    struct iovec iov;
	struct uio ku;
	size_t len = 0;
	ssize_t err = 0;
	char* kbuf = kmalloc(N);
    rwlock_acquire_write(fp->lock);

    // copy content from user land to system space
	err = copyin(buf, kbuf, N);
	if(err) {
		kfree(kbuf);
        rwlock_release_write(fp->lock);
		return -err;
	}

	uio_kinit(&iov, &ku, kbuf, N, *pos, UIO_WRITE);
    err = VOP_WRITE(fp->inode, &ku);
	if(err) {
		kprintf("Write error:[%s]\n", strerror(err));
        rwlock_release_write(fp->lock);
		return -err;
	}
	len = ku.uio_offset - fp->offset;
    fp->offset = ku.uio_offset;
    *pos = fp->offset;
    rwlock_release_write(fp->lock);
    return len;
}

bool file_isseekable(struct file* fp)
{
    return VOP_ISSEEKABLE(fp->inode);
}

off_t file_lseek(struct file* fp, off_t offset, int pos)
{
    KASSERT(fp != NULL);
    
    if (!file_isseekable(fp)) {
        return -ESPIPE;
    }

    struct stat stat;
    switch (pos)
    {
    case SEEK_SET:
        if (offset < 0) {
            return -EINVAL;
        }
        rwlock_acquire_write(fp->lock);
        fp->offset = offset;
        rwlock_release_write(fp->lock);
        break;
    case SEEK_CUR:
        rwlock_acquire_write(fp->lock);
        fp->offset += offset;
        offset = fp->offset;
        rwlock_release_write(fp->lock);
        break;
    case SEEK_END:
        VOP_STAT(fp->inode, &stat);
        rwlock_acquire_write(fp->lock);
        fp->offset = stat.st_size + offset;
        offset = fp->offset;
        rwlock_release_write(fp->lock);
        break;
    default:
        return -EINVAL;
    }
    
    return offset;
}