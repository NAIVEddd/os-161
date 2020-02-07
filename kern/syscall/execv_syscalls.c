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
#include <addrspace.h>
#include <lib.h>
#include <file.h>
#include <proc.h>
#include <current.h>
#include <kern/errno.h>
#include <kern/fcntl.h>
#include <thread.h>
#include <threadlist.h>
#include <cpu.h>
#include <array.h>
#include <synch.h>
#include <vfs.h>

struct execv_arg {
	int argc;
	char* argvbuf;
	char* pathname;
};

int execv_helper(struct execv_arg* args);

int
execv_helper(struct execv_arg* args)
{
	struct addrspace * as;
	struct vnode *v;
	vaddr_t entrypoint, stackptr;
	int result;

	/* Open the file. */
	result = vfs_open(args->pathname, O_RDONLY, 0, &v);
	if (result) {
		panic("execv: vfs_open failed.\n");
	}

    as = proc_setas(NULL);
	if(as != NULL) {
		as_destroy(as);
	}
	/* Create a new address space. */
	as = as_create();
	if (as == NULL) {
		vfs_close(v);
		panic("execv: as_create err[ENOMEM]\n");
	}

	/* Switch to it and activate it. */
	proc_setas(as);
	as_activate();

	/* Load the executable. */
	result = load_elf(v, &entrypoint);
	if (result) {
		/* p_addrspace will go away when curproc is destroyed */
		vfs_close(v);
		panic("execv: load_elf err.\n");
	}

	/* Done with the file now. */
	vfs_close(v);

	/* Define the user stack in the address space */
	result = as_define_stack(as, &stackptr);
	if (result) {
		/* p_addrspace will go away when curproc is destroyed */
		panic("execv: as_define_stack err.\n");
	}

	{	// make argv in userspace
		// userptr_t userargvp = (userptr_t)as->as_vbase2;
		// userptr_t userargvcontent = (userptr_t)(as->as_vbase2+ sizeof(void*) * args->argc);
		// void* argvptr[args->argc];
		// unsigned offset = sizeof(void*) * args->argc;
		// unsigned count = 0;
		// unsigned len = 0;
		// for(int i = 0; i != args->argc; i++) {
		// 	copyoutstr(args->argvbuf + count, (userptr_t)(((char*)userargvcontent) + count), 200 - count, &len);
		// 	argvptr[i] = (void*)(userargvcontent + count);
		// 	count += len;
		// }
		// copyout((const void*)argvptr, userargvp, offset);

		// copyinstr((const_userptr_t)args->pathname, args->argvbuf, 200, &len);
		// kfree(curproc->p_name);
		// curproc->p_name = kstrdup(args->argvbuf);
		// KASSERT(curproc->p_name != NULL);
		// kfree(args->argvbuf);
		// kfree(args);

		/* Warp to user mode. */
		enter_new_process(args->argc /*argc*/, NULL, // userargvp /*userspace addr of argv*/,
				NULL /*userspace addr of environment*/,
				stackptr, entrypoint);
	}
	return 1;
}

/*
 * execv system call:
 */

int
sys_execv(const char * path, char * const argv[])
{
	int err = 0;
	(void)path;
    unsigned argc = 0;
	unsigned count = 0;

    for(; argv[argc] != NULL; argc++) ;

	char * buf = kmalloc(200);
	if(buf == NULL) {
		return -1;
	}
	for(unsigned i = 0; i != argc; i++) {
		unsigned len = 0;
		err = copyinstr((const_userptr_t)argv[i], buf + count, 200 - count, &len);
		if(err) {
			return -err;
		}
		count += len;
	}

	struct execv_arg * args = kmalloc(sizeof(struct execv_arg));
	args->argc = argc;
	args->argvbuf = buf;
	args->pathname = (char*)path;

	err = execv_helper(args);
	return -err;
}