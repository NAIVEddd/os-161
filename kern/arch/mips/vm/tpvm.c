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
#include <vm.h>
#include <mainbus.h>
#include <synch.h>

struct page {
	unsigned char ptr[PAGE_SIZE];
};

struct PhysicalMemory {
	uint32_t EmptyPageNumber;
	uint32_t TotalPageNumber;   // [ActualMemoryByte / PageSizeByte]
	paddr_t StartPointer;
	struct page ** IsMemoryUsed;
};

static struct PhysicalMemory * physicalmemory = NULL;
static paddr_t lastaddr = (paddr_t)NULL;
static struct spinlock stealmem_lock = SPINLOCK_INITIALIZER;
static struct lock * memalloc_lock = NULL;

void
vm_bootstrap(void)
{
	paddr_t addr;
	size_t ramsize, pages;
	size_t totalmemcost, totalpagecost;
	ramsize = mainbus_ramsize();
	pages = ramsize / PAGE_SIZE;
	totalmemcost = sizeof(struct PhysicalMemory) + pages * sizeof(struct pages*);
	totalpagecost = (totalmemcost + PAGE_SIZE - 1) / PAGE_SIZE;
	spinlock_acquire(&stealmem_lock);
	addr = ram_stealmem(totalpagecost);
	spinlock_release(&stealmem_lock);
	tlb_write(addr, addr | TLBLO_DIRTY | TLBLO_VALID, 0);
	physicalmemory = (struct PhysicalMemory*)addr;
	physicalmemory->TotalPageNumber =
		physicalmemory->EmptyPageNumber = pages - ((addr+PAGE_SIZE-1) / 4096) - totalpagecost;
	physicalmemory->StartPointer = addr + totalpagecost * PAGE_SIZE;
	physicalmemory->IsMemoryUsed = (struct page **)((char*)addr + sizeof(struct PhysicalMemory));

	lastaddr = ram_getfirstfree();

	memalloc_lock = lock_create("memory_alloc_lock");
	KASSERT(memalloc_lock != NULL);
}


static
void
tpvm_acquirelock()
{
	if(CURCPU_EXISTS()) {
		/* must not hold spinlocks */
		KASSERT(curcpu->c_spinlocks == 0);

		/* must not be in an interrupt handler */
		KASSERT(curthread->t_in_interrupt == 0);

		// Lock
		lock_acquire(memalloc_lock);
	}
}

static
void
tpvm_releaselock()
{
	if(CURCPU_EXISTS()) {
		/* must not hold spinlocks */
		KASSERT(curcpu->c_spinlocks == 0);

		/* must not be in an interrupt handler */
		KASSERT(curthread->t_in_interrupt == 0);

		// release lock
		lock_release(memalloc_lock);
	}
}

/* Allocate/free some kernel-space virtual pages */
vaddr_t
alloc_kpages(unsigned npages)
{
	// when no swap
	KASSERT(npages <= physicalmemory->EmptyPageNumber);

	// get lock
	tpvm_acquirelock();

	paddr_t pa = 0;
	for(uint32_t i = 0; (i + npages) < physicalmemory->EmptyPageNumber; ++i) {
		bool findit = true;
		for(uint32_t j = i; j != i + npages; ++j) {
			if(physicalmemory->IsMemoryUsed[j] != NULL) {
				findit = false;
				i = j;
				break;
			}
		}
		if(findit == true) {
			pa = physicalmemory->StartPointer + i * PAGE_SIZE;
			physicalmemory->EmptyPageNumber -= npages;
			for(uint32_t j = i; j != i + npages - 1; ++j) {
				physicalmemory->IsMemoryUsed[j] = (struct page*)(physicalmemory->StartPointer + (j+1)*PAGE_SIZE);
			}
			physicalmemory->IsMemoryUsed[i + npages - 1] = (struct page*)-1;
			break;
		}
	}

	// release lock
	tpvm_releaselock();

	KASSERT(pa != 0);
	return PADDR_TO_KVADDR(pa);
}

void
free_kpages(vaddr_t addr)
{
	/* nothing - leak the memory. */

	(void)addr;
}

unsigned
int
coremap_used_bytes() {

	/* dumbvm doesn't track page allocations. Return 0 so that khu works. */

	return 0;
}

void
vm_tlbshootdown(const struct tlbshootdown *ts)
{
	(void)ts;
	panic("dumbvm tried to do tlb shootdown?!\n");
}

int
vm_fault(int faulttype, vaddr_t faultaddress)
{
    (void)faulttype;
    (void)faultaddress;

	faultaddress &= PAGE_FRAME;

	if(curproc == NULL) {
		return EFAULT;
	}

	if(faultaddress < (vaddr_t)lastaddr) {
		uint32_t ehi, elo;
		int spl = splhigh();
		ehi = faultaddress;
		elo = faultaddress | TLBLO_DIRTY | TLBLO_VALID;
		tlb_write(ehi, elo, 0);
		splx(spl);
		return 0;
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
	int spl = splhigh();
	for (uint32_t i=1; i<NUM_TLB; i++) {
		uint32_t ehi, elo;
		tlb_read(&ehi, &elo, i);
		if (elo & TLBLO_VALID) {
			continue;
		}
		ehi = faultaddress;
		elo = paddr | TLBLO_DIRTY | TLBLO_VALID;
		DEBUG(DB_VM, "dumbvm: 0x%x -> 0x%x\n", faultaddress, paddr);
		tlb_write(ehi, elo, i);
		splx(spl);
		return 0;
	}
	return EFAULT;
}