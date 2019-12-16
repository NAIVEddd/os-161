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
 * exit system call:
 */

void
sys_exit(int status)
{
    struct proc * this = curthread->t_proc;
    struct proc * parent = this->p_parent;

    thread_collect(this, &curcpu->c_zombies);

    spinlock_acquire(&parent->p_lock);
    for (int i = 0; i != 20; i++) {
        if(parent->p_eventarray[i].proc == NULL) {
            parent->p_eventarray[i].proc = this;
            parent->p_eventarray[i].event = PE_childexit;
            break;
        }
    }
    spinlock_release(&parent->p_lock);
    
    this->p_xcode = status;
    lock_acquire(parent->p_locksubpwait);
    cv_signal(parent->p_cvsubpwait, parent->p_locksubpwait);
    lock_release(parent->p_locksubpwait);

    schedule();
    thread_exit();

    for(;;)
        cpu_idle();
}