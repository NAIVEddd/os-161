Assignment 3 Design
===================

## Manage physical memory
    struct PhysicalMemory {
        uint32_t emptyPageNumber;
        uint32_t totalPageNumber;   // [ActualMemoryByte / PageSizeByte]
        struct page ** IsMemoryUsed;
    }

    struct Page {
        uchar ptr[PAGE_SIZE];
    }

    struct TLE {
        uint32_t swapable : 1;
        uint32_t valid : 1;
        uint32_t readable : 1;
        uint32_t writeable : 1;
        uint32_t executable : 1;
        uint32_t isInMemory : 1;
        uint32_t reserved : 6;
        uint32_t location : 20;     // physical page number
        paddr_t physical;         // recalcuated
        uint32_t useCount;
        pid_t pid;
    }

    struct PteList {
        struct PTE pte;
        struct PteList * prev;
        struct PteList * next;
    }

## VM Init functions : In "tpvm.c"
    //
    vm_bootstrap()
    alloc_kpages(unsigned npages);
    free_kpages(vaddr_t addr);
    alloc_upages()      // alloc memory for user program
    free_upages()
    coremp_used_bytes()
    vm_tlbshootdown()
    vm_fault()

    // VM helper function
    vm_allocpages(u)

## AddrSpace helper function: In "include/addrspace.h"
    as_create()
    as_destroy()
    as_activate()
    as_deactivate()
    as_define_region()
    as_zero_region()
    as_prepare_load()
    as_complete_load()
    as_define_stack()
    as_copy()

## Process address space
    struct Segment {
        struct PteList * pte;
        vaddr start;
        size_t bound;
    }

    struct AddressSpace {
        struct Segment code, data, heap, stack;
    }

#### Check:
    Start < V.A < Start + bound

#### Translate:
    P.A = V.A - Start + Base9

## When vm_fault happend:
need a new struct:
    struct pagetable {

    };
this struct store 63 tlb entries.
    put_into_tlb    // used in as_activate
    remove_from_tlb // used in as_deactivate

## Malloc related System call
    void sbrk(...)
