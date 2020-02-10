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
#include <kern/errno.h>
#include <lib.h>
#include <addrspace.h>
#include <vm.h>
#include <proc.h>
#include <current.h>
#include <spl.h>
#include <mips/tlb.h>


static
void
SegmentInit(struct Segment * seg)
{
	seg->ptes = NULL;
	seg->start = 0;
	seg->bound = 0;
}

static
void
SegmentDestroy(struct Segment * seg)
{
	for(struct PteList * h = seg->ptes; h != NULL;) {
		struct PteList * t = h;
		h = h->next;
		free_kpages(t->pte.virtual);
		kfree(t);
	}
}

static
void
SegmentMake(struct Segment * seg)
{
	for(struct PteList * h = seg->ptes; h != NULL; h = h->next) {
		vaddr_t addr = alloc_kpages(1);
		paddr_t paddr = addr - MIPS_KSEG0;
		h->pte.virtual = addr;
		h->pte.isInMemory = true;
		h->pte.swapable = true;
		h->pte.valid = true;
		h->pte.physical = paddr;
		h->pte.useCount = 1;

		bzero((void*)addr, PAGE_SIZE);
	}
}

static
void
SegmentMakeNoZero(struct Segment * seg)
{
	for(struct PteList * h = seg->ptes; h != NULL; h = h->next) {
		vaddr_t addr = alloc_kpages(1);
		paddr_t paddr = addr - MIPS_KSEG0;
		h->pte.virtual = addr;
		h->pte.isInMemory = true;
		h->pte.swapable = true;
		h->pte.valid = true;
		h->pte.physical = paddr;
		h->pte.useCount = 1;
	}
}


static
struct PteList *
PteList_InsertFront(struct PteList * list, struct PTE * pte)
{
	struct PteList * p = kmalloc(sizeof(struct PteList));
	KASSERT(p != NULL);
	if(list != NULL) {
		list->prev = p;
	}
	p->next = list;
	p->prev = NULL;
	memcpy(&p->pte, pte, sizeof(*pte));
	return p;
}

static
struct PteList *
PteList_Append(struct PteList * list, struct PTE * pte)
{
	struct PteList * tail = list;
	while(tail && tail->next != NULL) {
		tail = tail->next;
	}
	struct PteList * p = kmalloc(sizeof(struct PteList));
	p->prev = tail;
	p->next = NULL;
	if(tail != NULL) {
		tail->next = p;
	}
	memcpy(&p->pte, pte, sizeof(*pte));
	return list? list : p;
}

static
void
ExpandStack(struct addrspace * as, vaddr_t addr, size_t sz)
{
	KASSERT(sz == PAGE_SIZE);
	as->stack.start = addr;
	as->stack.bound += sz;
	struct PTE pte;
	pte.swapable = false;
	pte.shared = false;
	pte.isInMemory = false;
	pte.executable = false;
	pte.readable = true;
	pte.writeable = true;
	pte.valid = false;
	pte.location = 0;
	pte.physical = 0;
	pte.virtual = as->stack.start;
	pte.useCount = 0;
	pte.pid = curthread->t_proc->p_pid;
	as->stack.ptes = PteList_InsertFront(as->stack.ptes, &pte);

	SegmentMake(&as->stack);
}

static
struct PTE *
SegmentFindAddr(struct Segment * seg, vaddr_t addr)
{
	if(!(addr >= (seg->start) && addr < (seg->start + seg->bound))) {
		return NULL;
	}
	struct PTE * pte = NULL;
	vaddr_t start = seg->start;
	for(struct PteList * h = seg->ptes; h != NULL; h = h->next) {
		if(start == addr) {
			pte = &h->pte;
			break;
		} else {
			start += PAGE_SIZE;
		}
	}
	return pte;
}

static
void
SegmentPreCopy(struct Segment * dst, struct Segment * src)
{
	dst->start = src->start;
	dst->bound = src->bound;
	for(struct PteList * h = src->ptes; h != NULL; h = h->next) {
		struct PTE pte;
		memcpy(&pte, &h->pte, sizeof(struct PTE));
		dst->ptes = PteList_Append(dst->ptes, &pte);
	}
}

static
void
SegmentCopy(struct Segment * dst, struct Segment * src)
{
	for(struct PteList * h = src->ptes, * d = dst->ptes; h != NULL; h = h->next, d = d->next) {
		memmove((void *)PADDR_TO_KVADDR(d->pte.physical),
				(const void*)PADDR_TO_KVADDR(h->pte.physical), PAGE_SIZE);
	}
}

struct PTE *
as_pagefault(struct addrspace * as, vaddr_t addr)
{
	struct PTE * pte = NULL;

	if((pte = SegmentFindAddr(&as->code, addr)) != NULL) {
		;
	} else if((pte = SegmentFindAddr(&as->data, addr)) != NULL) {
		;
	} else if((pte = SegmentFindAddr(&as->heap, addr)) != NULL) {
		;
	} else if((pte = SegmentFindAddr(&as->stack, addr)) != NULL) {
		;
	} else {
		kprintf("Need Expand Stack...\n");
		KASSERT(addr < as->stack.start && addr >= (as->stack.start - PAGE_SIZE));
		ExpandStack(as, as->stack.start - PAGE_SIZE, PAGE_SIZE);
		pte = SegmentFindAddr(&as->stack, addr);
		
		KASSERT(pte != NULL);
	}
	return pte;
}

/*
 * Note! If OPT_DUMBVM is set, as is the case until you start the VM
 * assignment, this file is not compiled or linked or in any way
 * used. The cheesy hack versions in dumbvm.c are used instead.
 */

struct addrspace *
as_create(void)
{
	struct addrspace *as;

	as = kmalloc(sizeof(struct addrspace));
	if (as == NULL) {
		return NULL;
	}

	/*
	 * Initialize as needed.
	 */
	SegmentInit(&as->code);
	SegmentInit(&as->data);
	SegmentInit(&as->heap);
	SegmentInit(&as->stack);

	return as;
}

int
as_copy(struct addrspace *old, struct addrspace **ret)
{
	struct addrspace *newas;

	newas = as_create();
	if (newas==NULL) {
		return ENOMEM;
	}

	/*
	 * Write this.
	 */
	SegmentPreCopy(&newas->code, &old->code);
	SegmentPreCopy(&newas->data, &old->data);
	SegmentPreCopy(&newas->heap, &old->heap);
	SegmentPreCopy(&newas->stack, &old->stack);

	SegmentMakeNoZero(&newas->code);
	SegmentMakeNoZero(&newas->data);
	SegmentMakeNoZero(&newas->heap);
	SegmentMakeNoZero(&newas->stack);

	SegmentCopy(&newas->code, &old->code);
	SegmentCopy(&newas->data, &old->data);
	SegmentCopy(&newas->heap, &old->heap);
	SegmentCopy(&newas->stack, &old->stack);

	*ret = newas;
	return 0;
}

void
as_destroy(struct addrspace *as)
{
	/*
	 * Clean up as needed.
	 */
	SegmentDestroy(&as->code);
	SegmentDestroy(&as->data);
	SegmentDestroy(&as->heap);
	SegmentDestroy(&as->stack);

	kfree(as);
}

void
as_activate(void)
{
	struct addrspace *as;

	as = proc_getas();
	if (as == NULL) {
		/*
		 * Kernel thread without an address space; leave the
		 * prior address space in place.
		 */
		return;
	}

	/*
	 * Write this.
	 */
	int spl = splhigh();

	for (int i=1; i<NUM_TLB; i++) {
		tlb_write(TLBHI_INVALID(i), TLBLO_INVALID(), i);
	}

	splx(spl);
}

void
as_deactivate(void)
{
	/*
	 * Write this. For many designs it won't need to actually do
	 * anything. See proc.c for an explanation of why it (might)
	 * be needed.
	 */
}

/*
 * Set up a segment at virtual address VADDR of size MEMSIZE. The
 * segment in memory extends from VADDR up to (but not including)
 * VADDR+MEMSIZE.
 *
 * The READABLE, WRITEABLE, and EXECUTABLE flags are set if read,
 * write, or execute permission should be set on the segment. At the
 * moment, these are ignored. When you write the VM system, you may
 * want to implement them.
 */
int
as_define_region(struct addrspace *as, vaddr_t vaddr, size_t sz,
		 int readable, int writeable, int executable)
{
	/*
	 * Write this.
	 */
	size_t npages;

	/* Align the region. First, the base... */
	sz += vaddr & ~(vaddr_t)PAGE_FRAME;
	vaddr &= PAGE_FRAME;

	/* ...and now the length. */
	npages = (sz + PAGE_SIZE - 1) / PAGE_SIZE;

	if(as->code.ptes == NULL) {
		as->code.start = vaddr;
		as->code.bound = sz;
		for(uint32_t i = 0; i != npages; ++i) {
			struct PTE pte;
			pte.swapable = false;
			pte.shared = false;
			pte.isInMemory = false;
			pte.executable = (bool)executable;
			pte.readable = (bool)readable;
			pte.writeable = (bool)writeable;
			pte.valid = false;
			pte.location = 0;
			pte.physical = 0;
			pte.virtual = vaddr + i * PAGE_SIZE;
			pte.useCount = 0;
			pte.pid = curthread->t_proc->p_pid;
			as->code.ptes = PteList_Append(as->code.ptes, &pte);
		}
		return 0;
	}

	if(as->data.ptes == NULL) {
		as->data.start = vaddr;
		as->data.bound = sz;
		for(uint32_t i = 0; i != npages; ++i) {
			struct PTE pte;
			pte.swapable = false;
			pte.shared = false;
			pte.isInMemory = false;
			pte.executable = (bool)executable;
			pte.readable = (bool)readable;
			pte.writeable = (bool)writeable;
			pte.valid = false;
			pte.location = 0;
			pte.physical = 0;
			pte.virtual = vaddr + i * PAGE_SIZE;
			pte.useCount = 0;
			pte.pid = curthread->t_proc->p_pid;
			as->data.ptes = PteList_Append(as->data.ptes, &pte);
		}
		return 0;
	}

	/*
	 * Support for more than two regions is not available.
	 */
	kprintf("dumbvm: Warning: too many regions\n");
	return ENOSYS;
}

int
as_prepare_load(struct addrspace *as)
{
	/*
	 * Write this.
	 */
	SegmentMake(&as->code);
	SegmentMake(&as->data);

	return 0;
}

int
as_complete_load(struct addrspace *as)
{
	/*
	 * Write this.
	 */

	(void)as;
	return 0;
}

int
as_define_stack(struct addrspace *as, vaddr_t *stackptr)
{
	/* Initial user-level stack pointer */
	*stackptr = USERSTACK;

	vaddr_t addr = USERSTACK;
	ExpandStack(as, addr -= PAGE_SIZE, PAGE_SIZE);

	return 0;
}

