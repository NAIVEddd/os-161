/*
 * All the contents of this file are overwritten during automated
 * testing. Please consider this before changing anything in this file.
 */

#include <types.h>
#include <lib.h>
#include <clock.h>
#include <thread.h>
#include <synch.h>
#include <test.h>
#include <kern/test161.h>
#include <spinlock.h>

/*
 * Use these stubs to test your reader-writer locks.
 */

#define CREATELOOPS 8
#define NTHREADS 8

static struct rwlock* testlock = NULL;
static struct semaphore* exitsem;

struct spinlock status_lock;
static bool test_status = TEST161_FAIL;

static volatile unsigned int test_writed_number = 0;


int rwtest(int nargs, char **args) {
	(void)nargs;
	(void)args;

	int i;
	kprintf_n("rwt1 \n");
	for(i = 0; i < CREATELOOPS; i++)
	{
		kprintf_t(".");
		testlock = rwlock_create("testrwlock");
		if(testlock == NULL) {
			panic("rwt1: rwlock_create failed\n");
		}
		if(i != CREATELOOPS - 1) {
			rwlock_destroy(testlock);
		}
	}
	spinlock_init(&status_lock);
	spinlock_acquire(&status_lock);
	test_status = TEST161_SUCCESS;
	spinlock_release(&status_lock);

	kprintf_n("If this hangs, it's broken: ");

	rwlock_acquire_read(testlock);
	rwlock_acquire_read(testlock);
	rwlock_acquire_read(testlock);
	rwlock_release_read(testlock);
	rwlock_release_read(testlock);
	rwlock_release_read(testlock);
	rwlock_acquire_write(testlock);
	rwlock_release_write(testlock);

	rwlock_destroy(testlock);
	spinlock_cleanup(&status_lock);
	testlock = NULL;

	kprintf_t("\n");
	success(test_status, SECRET, "rwt1");

	return 0;
}

#define NLOOPS 40
static
void
rwlocktestreadthread(void* junk, unsigned long num)
{
	(void)junk;
	(void)num;

	int i;
	for(i = 0; i != NLOOPS; i++)
	{
		kprintf_t(".");
		random_yielder(4);
		rwlock_acquire_read(testlock);
		random_yielder(4);
		rwlock_release_read(testlock);
	}
	kprintf_n("rwt2: read loop exiting...\n");
	V(exitsem);
}

static
void
rwlocktestwritethread(void* junk, unsigned long num)
{
	(void)junk;
	(void)num;

	int i;
	unsigned int local_num = 0;
	for(i = 0; i != NLOOPS; i++)
	{
		kprintf_t(".");
		for(local_num = 20; local_num != 40; ++local_num)
		{
			random_yielder(4);
			rwlock_acquire_write(testlock);
			random_yielder(4);
			test_writed_number = local_num;
			random_yielder(4);
			rwlock_release_write(testlock);
		}
	}
	kprintf_n("rwt2: write loop exiting...\n");
	V(exitsem);
}

int rwtest2(int nargs, char **args) {
	(void)nargs;
	(void)args;

	unsigned i;
	int result;
	kprintf_n("rwt2 \n");

	for(i=0; i < CREATELOOPS; i++)
	{
		kprintf_t(".");
		testlock = rwlock_create("rwlocktest2");
		if(testlock == NULL){
			panic("rwlocktest2: rwlock_create failed.\n");
		}
		exitsem = sem_create("rwtest2 exitsem", 0);
		if(exitsem == NULL){
			panic("exitsem: sem_create failed.\n");
		}
		if(i != CREATELOOPS - 1) {
			rwlock_destroy(testlock);
			sem_destroy(exitsem);
		}
	}
	test_status = TEST161_SUCCESS;

	for(i=0; i<NTHREADS;i++)
	{
		kprintf_t(".");
		result = thread_fork("synchtest_rwlockread", NULL, rwlocktestreadthread, NULL, i);
		if(result) {
			panic("rwlocktest2: thread_fork failed.\n");
		}
		result = thread_fork("synchtest_rwlockwrite", NULL, rwlocktestwritethread, NULL, i);
		if(result) {
			panic("rwlocktest2: thread_fork failed.\n");
		}
	}
	for(i=0;i<NTHREADS;i++)
	{
		kprintf_t(".");
		P(exitsem);
		P(exitsem);
	}

	rwlock_destroy(testlock);
	sem_destroy(exitsem);
	testlock = NULL;
	exitsem = NULL;

	kprintf_t("\n");
	success(test_status, SECRET, "rwt2");

	return 0;
}

static
void
rwlocktest3thread(void* junk, unsigned long num)
{
	(void)junk;
	(void)num;

	int i;
	unsigned int local_num = 0;
	for(i = 0; i != NLOOPS; i++)
	{
		kprintf_t(".");
		for(local_num = 20; local_num != 40; ++local_num)
		{
			random_yielder(4);
			rwlock_acquire_write(testlock);
			random_yielder(4);
			test_writed_number = local_num;
			random_yielder(4);
			rwlock_release_write(testlock);
			random_yielder(4);
			rwlock_acquire_read(testlock);
			random_yielder(4);
			if(test_writed_number != local_num)
			{
				panic("rwlocktest3: write wrong.\n");
			}
			random_yielder(4);
			rwlock_release_read(testlock);
			kprintf_n("rwt3: loop...\n");
		}
	}
	kprintf_n("rwt3: loop exiting...\n");
	V(exitsem);
}

int rwtest3(int nargs, char **args) {
	(void)nargs;
	(void)args;

	kprintf_n("rwt3 \n");
	int i, result;
	for(i=0; i < CREATELOOPS; i++)
	{
		kprintf_t(".");
		testlock = rwlock_create("rwlocktest3");
		if(testlock == NULL){
			panic("rwlocktest3: rwlock_create failed.\n");
		}
		exitsem = sem_create("rwtest3 exitsem", 0);
		if(exitsem == NULL){
			panic("exitsem: sem_create failed.\n");
		}
		if(i != CREATELOOPS - 1) {
			rwlock_destroy(testlock);
			sem_destroy(exitsem);
		}
	}
	test_status = TEST161_SUCCESS;

	for(i=0; i<1;i++)
	{
		kprintf_t(".");
		result = thread_fork("synchtest_rwlockread3", NULL, rwlocktest3thread, NULL, i);
		if(result) {
			panic("rwlocktest3: thread_fork failed.\n");
		}
	}
	for(i=0;i<1;i++)
	{
		kprintf_t(".");
		P(exitsem);
	}

	success(test_status, SECRET, "rwt3");

	return 0;
}

int rwtest4(int nargs, char **args) {
	(void)nargs;
	(void)args;

	kprintf_n("rwt4 unimplemented\n");
	success(TEST161_FAIL, SECRET, "rwt4");

	return 0;
}

int rwtest5(int nargs, char **args) {
	(void)nargs;
	(void)args;

	kprintf_n("rwt5 unimplemented\n");
	success(TEST161_FAIL, SECRET, "rwt5");

	return 0;
}
