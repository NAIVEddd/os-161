Assignment 2 design
===================

## How to implement file-related system calls:

### Test:
- console work: `p /testbin/consoletest`
- open, close work: `p /testbin/opentest` and `p /testbin/closetest`
- read, write work: `p /testbin/readwritetest` and `p /testbin/fileonlytest`
- lseek work: `p /testbin/fileonlytest` and `p /testbin/sparsefile`
- dup2 work: `p /testbin/redirect`
- chdir work:
- __getcwd work:

### Details:
First, must be implemented is `file table` and `file object`.

There are 2 struct are needed: `struct files_struct` and `struct file`.

__To support file table, `struct proc` need add `struct files_struct* files;` filed.__

### struct infomation:

    // this struct is used as file table. Include all 
    // file struct opened in a process.

    struct files_struct {
        char * procname;
        struct lock* lock;
        uint32_t count;     // opened files num
        uint32_t capacity;  // length of fds array
        struct file** fds;
    };

    // this struct is do the actual work to read/write 
    // data from filesystem

    struct file {
        char * pathname;
        struct rwlock* lock;
        struct lock* reflock;
        uint32_t flags;      // read/write/append
        uint32_t mode;       // 0644 etc.
        // f_op
        uint32_t refc;       // how many fd reference this file
        uint64_t offset;     // r/w base address
        uint64_t length;     // file content length
        struct vnode* inode; // do actual work
    };

### Helper functions
Function to support `struct files_struct`
- `struct files_struct* files_struct_create(char* name)`
- `void files_struct_destroy(struct files_struct * files)`
- `uint32_t files_struct_append(struct files_struct * files, struct file * fp)`
- `void files_struct_remove(struct files_struct * files, uint32_t fd)`
- `struct file* files_struct_get(struct files_struct* files, size_t fd)`
- `void files_struct_set(struct files_struct * files, size_t fd, struct file * fp)`

Function to support `struct file`
- `struct file* file_create(char* filename)`
- `void file_destroy(struct file* fp)`

        free memory if `refc == 0` else `refc -= 1`
        
- `size_t file_incref(struct file* fp)`
- `size_t file_decref(struct file* fp)`

### How these syscall work? open, close, read, write, lseek, dup2

- Open: just create a `file`, and attach to current process
- Close: decref, if `refc == 0`, destroy it
- Read, Write: `struct vnode` will do the work
- lseek: if `isseekable`, change offset
- dup2: copy the pointer and incref


### Conclusion
So the key point is through `fd` to find out the `vnode` inside `struct file`, then vop will do actual work.

## How to implement process-related system calls:

### Test:
- fork work: `p /testbin/forktest`
- execv work: `p /testbin/argtest`, `p /testbin/add`, `p /testbin/factorial` and `p /testbin/bigexec`
- waitpid/_exit work: `p /testbin/forktest`
- getpid work: all of the tests described above use getpid in some way.

### Struct imformation:

    // struct proc needed field
    struct proc {
        pid_t pid;      // process's id
        pid_t p_ppid;   // parent pid
        struct files_struct * p_files;
        enum p_states{
            PRS_NEW = 0,    // new created
            PRS_NORMAL,     // threads can be run
            PRS_ZOMBIE
        } p_state;
        struct proclist* p_children;
        struct proc * p_parent;

        struct lock* p_lk;  // lock for this sruct
        struct lock* p_statlk;  // lock for the states
        struct lock* p_subexit; // lock for subprocess _exit/wait
        struct thread* p_trapth;    // trap thread
        struct threadlist* p_threads;   // multiple sub thread
        list of struct procevent* p_procevent;  // event such as sub process exited/killed
        uint32_t p_xcode;   // exit code
        uint32_t p_xsig;    // stop/kill sig

        struct cv* p_cvsubpwait;  // wait cv for sub process exit
        // typedef void(*fatexit)(void);
        //list of fatexit p_fatexit;  // functions exec at exit
    };

    struct procevent {
        enum {
            exit,
            crash,
            killed,
            event,
        } type;
        struct proc * proc;     // the event happened proc
    }


### Pseudocode:
    // waitpid/_exit
    waitpid(pid_t pid, int* status_ptr, int options)
    {
        find pointer of that sub process
        if not find, return immediately
        if pid is greater than 0, lock that process' p_exit, and cv_wait(p_cvwait, subproc p_exit)
        if pid is equal to zero, lock current process' p_subexit, and cv_wait(p_cvsubwait, p_subexit);
    }

    _exit()
    {
        Calls all functions registered with the atexit() function
        Flushes all buffers(threads), and closes all open files
        All files opened with tmpfile() are deleted
        Save status and wakeup all suspended thread wait for this proc exit
        Returns control to the host environment from program
    }
    
    pid_t fork()
    {

    }

    int execv(char * pathname, void** argv)
    {
        copy userspace argv to kernel
        load new program into new addrspace
        make stack
        copy kernel argv to userspace
        enter new process
    }

## Stability

### Test:
- `p /testbin/badcall`
- `p /testbin/crash`
- `p /testbin/forktest`. run multiple iterations to check for race conditions
- `p /testbin/execvtest` test execv() function.