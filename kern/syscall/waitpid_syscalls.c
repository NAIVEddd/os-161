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
#include <thread.h>
#include <threadlist.h>
#include <cpu.h>
#include <array.h>
#include <synch.h>

/*
 * waitpid system call:
 */

pid_t
sys_waitpid(pid_t pid, int *status_ptr, int options)
{
    KASSERT(pid != 0);
    KASSERT(pid == -1 || pid > 0);
    (void)options;

    pid_t pidval = 0;
    struct proc * proc = curthread->t_proc;
    do {
        for(unsigned i = 0; i != 20; i++) {
            if(pid == -1) {
                if(proc->p_eventarray[i].event == PE_childexit) {
                    *status_ptr = proc->p_eventarray[i].proc->p_xcode;
                    pidval = proc->p_eventarray[i].proc->p_pid;
                    proc->p_eventarray[i].event = PE_none;
                    proc->p_eventarray[i].proc = NULL;
                    goto done;
                }
            } else {
                if(proc->p_eventarray[i].event != PE_none && proc->p_eventarray[i].proc->p_pid == pid) {
                    pidval = pid;
                    *status_ptr = proc->p_eventarray[i].proc->p_xcode;
                    proc->p_eventarray[i].event = PE_none;
                    proc->p_eventarray[i].proc = NULL;
                    goto done;
                }
            }
        }
        lock_acquire(proc->p_locksubpwait);
        cv_wait(proc->p_cvsubpwait, proc->p_locksubpwait);
        lock_release(proc->p_locksubpwait);
    } while(true);
done:
    return pidval;
}