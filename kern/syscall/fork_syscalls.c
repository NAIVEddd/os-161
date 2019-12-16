/*
 * Copyright (c) 2000, 2001, 2002, 2003, 2004, 2005, 2008, 2009
 *	The President and Fellows of Harvard College.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE UNIVERSITY AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE UNIVERSITY OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <types.h>
#include <clock.h>
#include <copyinout.h>
#include <syscall.h>
#include <lib.h>
#include <file.h>
#include <proc.h>
#include <current.h>
#include <kern/errno.h>
#include <addrspace.h>
#include <vm.h>
#include <mips/trapframe.h>

static
void
forkthread(void *ptr, unsigned long nargs)
{
    (void)nargs;

    struct trapframe tf;
    struct addrspace * as;
    void * tfp = ((void**)ptr)[0];
    as = (struct addrspace*)(((void**)ptr)[1]);
    memcpy(&tf, tfp, sizeof(tf));
    proc_setas(as);
    as_activate();
    kfree(tfp);
    kfree(ptr);

    enter_forked_process(&tf);
}

/*
 * open system call:
 */
pid_t
sys_fork(struct trapframe * tf)
{
    // alloc pid
    // create new proc, copy addrspace
    // copy fds
    // set up stack
    void** args = kmalloc(2 * sizeof(void*));
    if(args == NULL) {
        return -1;
    }
    
    struct addrspace *as;
    //vaddr_t entrypoint, stackptr;
    struct proc* proc;
    int result;

    result = as_copy(curthread->t_proc->p_addrspace, &as);
    if(result) {
        return -1;
    }

    proc = proc_create_runprogram(curthread->t_proc->p_name);
    if(proc == NULL) {
        as_destroy(as);
        return -1;
    }
    files_struct_destroy(proc->p_fds);

    // copy old files table
    files_struct_incref(curproc->p_fds);
    proc->p_fds = curproc->p_fds;

    struct trapframe * tfp = kmalloc(sizeof(struct trapframe));
    memcpy(tfp, tf, sizeof(struct trapframe));
    args[0] = tfp;
    args[1] = as;

    result = thread_fork("forked_thread",
            proc,
            forkthread,
            args, 2);
    if (result) {
        as_destroy(as);
        proc_destroy(proc);
        return -1;
    }

    return proc->p_pid;
}