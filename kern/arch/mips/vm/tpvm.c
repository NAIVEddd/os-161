#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <spl.h>
#include <cpu.h>
#include <spinlock.h>
#include <proc.h>
#include <current.h>
#include <mips/tlb.h>
#include <addrspace.h>
#include <current.h>
#include <vm.h>
#include <mainbus.h>
#include <synch.h>
#include <vfs.h>
#include <kern/fcntl.h>
#include <uio.h>
#include <vnode.h>

void SwapOut(struct PTE * pte);
void SwapIn(struct PTE * pte);

struct memunit{
	struct thread * mu_thread;
	uint16_t used : 1;
	uint16_t next : 1;
	uint16_t swapable : 1;
	uint16_t reserve : 13;
};

struct PhysicalMemory {
	uint32_t SwapableNumber;
	uint32_t EmptyPageNumber;
	uint32_t TotalPageNumber;   // [ActualMemoryByte / PageSizeByte]
	paddr_t StartPointer;
	struct memunit * IsMemoryUsed;
};

static struct PhysicalMemory * physicalmemory = NULL;
static paddr_t lastaddr = (paddr_t)NULL;
static struct spinlock stealmem_lock = SPINLOCK_INITIALIZER;
static struct lock * memalloc_lock = NULL;

static
int
WriteToTlb(uint32_t ehi, uint32_t elo)
{
	int spl = splhigh();
	for (uint32_t i=1; i<NUM_TLB; i++) {
		uint32_t ehir, elor;
		tlb_read(&ehir, &elor, i);
		if (elor & TLBLO_VALID) {
			continue;
		}
		//DEBUG(DB_VM, "dumbvm: 0x%x -> 0x%x\n", faultaddress, paddr);
		DEBUG(DB_VM, "dumbvm: 0x%x -> 0x%x\n", ehi, elo & PAGE_FRAME);
		tlb_write(ehi, elo, i);
		splx(spl);
		return 0;
	}
	splx(spl);
	return -1;
}

struct swapinfo {
	uint8_t * IsUsed;
	size_t Empties;
	size_t Pages;
};
static struct swapinfo swap;

void
vm_bootstrap(void)
{
	paddr_t addr;
	size_t ramsize, pages, swappages;
	size_t totalmemcost, totalpagecost;
	ramsize = mainbus_ramsize();
	pages = ramsize / PAGE_SIZE;
	swappages = (pages + PAGE_SIZE - 1) / PAGE_SIZE;
	size_t memoryusedcost = pages * sizeof(struct memunit);
	totalmemcost = sizeof(struct PhysicalMemory) + memoryusedcost;
	totalpagecost = (totalmemcost + PAGE_SIZE - 1) / PAGE_SIZE;
	spinlock_acquire(&stealmem_lock);
	addr = ram_stealmem(totalpagecost);
	spinlock_release(&stealmem_lock);
	tlb_write(addr, addr | TLBLO_DIRTY | TLBLO_VALID, 0);
	physicalmemory = (struct PhysicalMemory*)addr;
	physicalmemory->TotalPageNumber =
		physicalmemory->EmptyPageNumber = pages - ((addr+PAGE_SIZE-1) / 4096) - totalpagecost;
	physicalmemory->StartPointer = addr + (totalpagecost + swappages) * PAGE_SIZE;
	physicalmemory->IsMemoryUsed = (struct memunit*)((char*)addr + sizeof(struct PhysicalMemory));
	physicalmemory->SwapableNumber = 0;

	// Init swap struct
	swap.IsUsed = (uint8_t *)ram_stealmem(swappages);
	swap.Empties = pages;
	swap.Pages = pages;

	lastaddr = ram_getfirstfree();
	memset(&physicalmemory->IsMemoryUsed[0], 0, memoryusedcost);
	memset(&swap.IsUsed[0], 0, swap.Pages);

	memalloc_lock = lock_create("memory_alloc_lock");
	KASSERT(memalloc_lock != NULL);
}

static struct lock * swaplock = NULL;
static struct vnode * swapinode;
void
vm_swapbootstrap()
{
	if(vfs_open((char*)"LHD0.img", O_RDWR, 0, &swapinode) != 0) {
		panic("Swap boot failed!!!\n");
	}

	swaplock = lock_create("SwapLock");
	KASSERT(swaplock != NULL);
}

static
void
tpvm_acquirelock()
{
	if(CURCPU_EXISTS()) {
		// Lock
		lock_acquire(memalloc_lock);
	}
}

static
void
tpvm_releaselock()
{
	if(CURCPU_EXISTS()) {
		// release lock
		lock_release(memalloc_lock);
	}
}

static
void
Find_EmptyPages(unsigned npages, size_t * idx, paddr_t * addr)
{
	*idx = (size_t) -1;
	*addr = 0;
	// get lock
	tpvm_acquirelock();
	if(physicalmemory->EmptyPageNumber < npages) {
		tpvm_releaselock();
		return;
	}
	for(uint32_t i = 0; (i + npages) <= physicalmemory->TotalPageNumber; ++i) {
		bool findit = true;
		for(uint32_t j = i; j != i + npages; ++j) {
			if((physicalmemory->IsMemoryUsed[j]).used == true) {
				findit = false;
				i = j;
				break;
			}
		}
		if(findit == true) {
			*idx = i;
			*addr = physicalmemory->StartPointer + i * PAGE_SIZE;
			physicalmemory->EmptyPageNumber -= npages;
			for(uint32_t j = i; j != i + npages; ++j) {
				(physicalmemory->IsMemoryUsed[j]).mu_thread = curthread;
				(physicalmemory->IsMemoryUsed[j]).next = true;
				(physicalmemory->IsMemoryUsed[j]).used = true;
			}
			(physicalmemory->IsMemoryUsed[i + npages - 1]).next = false;
			break;
		}
	}

	// release lock
	tpvm_releaselock();
}

static
void
SwapSegment(struct Segment * seg)
{
	for(struct PteList * h = seg->ptes; h != NULL; h = h->next) {
		SwapOut(&h->pte);
	}
}

static
void
SwapSomePage()
{
	KASSERT(physicalmemory->SwapableNumber > 0);
	for(uint32_t i = 0; i != physicalmemory->TotalPageNumber; ++i) {
		tpvm_acquirelock();
		bool swapable = physicalmemory->IsMemoryUsed[i].swapable == true;
		struct proc * proc = physicalmemory->IsMemoryUsed[i].mu_thread->t_proc;
		tpvm_releaselock();
		if(swapable) {
			SwapSegment(&proc->p_addrspace->code);
			SwapSegment(&proc->p_addrspace->data);
			SwapSegment(&proc->p_addrspace->heap);
			SwapSegment(&proc->p_addrspace->stack);
			break;
		}
	}
}

static
void
SwapInSegment(struct Segment * seg)
{
	for(struct PteList * h = seg->ptes; h != NULL; h = h->next) {
		SwapIn(&h->pte);
	}
}

static
void
SwapInPages()
{
	if(curthread == NULL) {
		return;
	}
	struct proc * proc = curthread->t_proc;
	SwapInSegment(&proc->p_addrspace->code);
	SwapInSegment(&proc->p_addrspace->data);
	SwapInSegment(&proc->p_addrspace->heap);
	SwapInSegment(&proc->p_addrspace->stack);
}

vaddr_t
alloc_kpages_swapable(unsigned npages)
{
	paddr_t pa;
	size_t idx;
	Find_EmptyPages(npages, &idx, &pa);
	if(pa == 0) {
		SwapSomePage();
		Find_EmptyPages(npages, &idx, &pa);
		KASSERT(pa != 0);
	}
	KASSERT(idx != (size_t)-1);
	for(size_t i = idx; i != idx + npages; i++) {
		physicalmemory->IsMemoryUsed[i].swapable = true;
	}
	tpvm_acquirelock();
	physicalmemory->SwapableNumber += npages;
	tpvm_releaselock();

	return PADDR_TO_KVADDR(pa);
}
/* Allocate/free some kernel-space virtual pages */
vaddr_t
alloc_kpages(unsigned npages)
{
	paddr_t pa;
	size_t idx;
	Find_EmptyPages(npages, &idx, &pa);
	if(pa == 0) {
		SwapSomePage();
		Find_EmptyPages(npages, &idx, &pa);
		KASSERT(pa != 0);
	}
	KASSERT(idx != (size_t)-1);
	for(size_t i = idx; i != idx + npages; i++) {
		physicalmemory->IsMemoryUsed[i].swapable = false;
	}

	return PADDR_TO_KVADDR(pa);
}

void
free_kpages(vaddr_t addr)
{
	/* nothing - leak the memory. */
	tpvm_acquirelock();
	uint32_t index = ((addr - MIPS_KSEG0) - physicalmemory->StartPointer) / PAGE_SIZE;
	if(index > physicalmemory->TotalPageNumber) {
		KASSERT(index < physicalmemory->TotalPageNumber);
	}
	for(; index < (uint32_t)physicalmemory->TotalPageNumber; index ++) {
		bool last = physicalmemory->IsMemoryUsed[index].next == false;
		bool swapable = physicalmemory->IsMemoryUsed[index].swapable == true;
		bzero(&physicalmemory->IsMemoryUsed[index], sizeof(typeof(*physicalmemory->IsMemoryUsed)));
		if(swapable) {
			physicalmemory->SwapableNumber -= 1;
		}
		physicalmemory->EmptyPageNumber += 1;
		if(last) {
			break;
		}
	}
	tpvm_releaselock();
}

unsigned
int
coremap_used_bytes() {
	return (physicalmemory->TotalPageNumber - physicalmemory->EmptyPageNumber) * PAGE_SIZE;
}

void
vm_tlbshootdown(const struct tlbshootdown *ts)
{
	(void)ts;
	panic("vm tried to do tlb shootdown?!\n");
}

int
vm_fault(int faulttype, vaddr_t faultaddress)
{
    (void)faulttype;
    (void)faultaddress;

	faultaddress &= PAGE_FRAME;

	if(faultaddress < (vaddr_t)lastaddr) {
		uint32_t ehi, elo;
		ehi = faultaddress;
		elo = faultaddress | TLBLO_DIRTY | TLBLO_VALID;
		if(WriteToTlb(ehi, elo) == 0) {
			return 0;
		}
		return EFAULT;
	}

	struct addrspace * as = proc_getas();
	if(as == NULL) {
		return EFAULT;
	}
	
	struct PTE * pte = as_pagefault(as, faultaddress);
	if(pte == NULL) {
		return EFAULT;
	}
	paddr_t paddr = pte->physical;
	if(WriteToTlb((uint32_t)faultaddress, (uint32_t)(paddr | TLBLO_DIRTY | TLBLO_VALID)) == 0) {
		return 0;
	}

	if(curproc == NULL) {
		return EFAULT;
	}
	return EFAULT;
}

void
SwapIn(struct PTE * pte)
{
	if(pte->isInMemory == true) {
		return;
	}
	size_t pos = pte->virtual;
	size_t idx = pos / PAGE_SIZE;
	pte->virtual = alloc_kpages(1);

	struct iovec iov;
	struct uio ku;
	int err = 0;
	lock_acquire(swaplock);
	uio_kinit(&iov, &ku, (void*)pte->virtual, PAGE_SIZE, pos, UIO_READ);
	err = VOP_READ(swapinode, &ku);

	swap.IsUsed[idx] = 0;
	pte->isInMemory = true;
	lock_release(swaplock);

	if(err) {
		KASSERT(err != 0);
	}
}

void
SwapOut(struct PTE * pte)
{
	struct iovec iov;
	struct uio ku;
	uint64_t pos = (uint64_t)-1;
	int err;
	lock_acquire(swaplock);
	for(size_t i = 0; i != swap.Pages; i++) {
		if(swap.IsUsed[i] == 0) {
			pos = i * PAGE_SIZE;
			swap.IsUsed[i] = 1;
			break;
		}
	}
	KASSERT(pos != (uint64_t)-1);
	uio_kinit(&iov, &ku, (void*)pte->virtual, PAGE_SIZE, pos, UIO_WRITE);
	err = VOP_WRITE(swapinode, &ku);

	pte->isInMemory = false;
	pte->virtual = pos;
	lock_release(swaplock);
	free_kpages(pte->virtual);
	if(err) {
		KASSERT(err != 0);
	}
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

	// SwapIn while as_complete_load() has ran.
	if(false) {
		SwapInPages();
	}
}