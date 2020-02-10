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
#include <vfs.h>
#include <kern/fcntl.h>

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

/* Allocate/free some kernel-space virtual pages */
vaddr_t
alloc_kpages(unsigned npages)
{
	// when no swap
	KASSERT(npages <= physicalmemory->EmptyPageNumber);

	// get lock
	tpvm_acquirelock();

	paddr_t pa = 0;
	for(uint32_t i = 0; (i + npages) <= physicalmemory->TotalPageNumber; ++i) {
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

	if(pa == 0) {
		KASSERT(pa != 0);
	}
	return PADDR_TO_KVADDR(pa);
}

void
free_kpages(vaddr_t addr)
{
	/* nothing - leak the memory. */
	tpvm_acquirelock();
	uint32_t index = ((addr - MIPS_KSEG0) - physicalmemory->StartPointer) / PAGE_SIZE;
	for(; index != (uint32_t)physicalmemory->TotalPageNumber; index ++) {
		bool last = physicalmemory->IsMemoryUsed[index] == (struct page*)-1;
		physicalmemory->IsMemoryUsed[index] = NULL;
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

	/* dumbvm doesn't track page allocations. Return 0 so that khu works. */

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

	if(curproc == NULL) {
		return EFAULT;
	}

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
	return EFAULT;
}