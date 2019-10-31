/*
 * Copyright (c) 2001, 2002, 2009
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

/*
 * Driver code is in kern/tests/synchprobs.c We will
 * replace that file. This file is yours to modify as you see fit.
 *
 * You should implement your solution to the whalemating problem below.
 */

#include <types.h>
#include <lib.h>
#include <thread.h>
#include <test.h>
#include <synch.h>


#define NMATING 10
static struct semaphore * male_sem[NMATING];
static struct semaphore * female_sem[NMATING];
static struct semaphore * paired_sem[NMATING];


/*
 * Called by the driver during initialization.
 */

void whalemating_init() {
	for(int i = 0; i != NMATING; i++)
	{
		male_sem[i] = sem_create("whale_male", 0);
		if(male_sem[i] == NULL){
			panic("whale_male sem create failed.\n");
		}
		female_sem[i] = sem_create("whale_famale", 0);
		if(female_sem[i] == NULL){
			panic("whale_famale sem create failed.\n");
		}
		paired_sem[i] = sem_create("whale_paired", 0);
		if(paired_sem[i] == NULL){
			panic("whale_paired sem create failed.\n");
		}
	}
	return;
}

/*
 * Called by the driver during teardown.
 */

void
whalemating_cleanup() {
	for(int i = 0; i != NMATING; i++)
	{
		sem_destroy(male_sem[i]);
		sem_destroy(female_sem[i]);
		sem_destroy(paired_sem[i]);
	}
	return;
}

void
male(uint32_t index)
{
	(void)index;
	/*
	 * Implement this function by calling male_start and male_end when
	 * appropriate.
	 */
	male_start(index);
	V(male_sem[index]);
	P(paired_sem[index]);
	male_end(index);
	return;
}

void
female(uint32_t index)
{
	(void)index;
	/*
	 * Implement this function by calling female_start and female_end when
	 * appropriate.
	 */
	int i = index % NMATING;
	female_start(index);
	V(female_sem[i]);
	P(paired_sem[i]);
	female_end(index);
	return;
}

void
matchmaker(uint32_t index)
{
	(void)index;
	/*
	 * Implement this function by calling matchmaker_start and matchmaker_end
	 * when appropriate.
	 */
	int i = index % NMATING;
	matchmaker_start(index);
	P(male_sem[i]);
	P(female_sem[i]);
	V(paired_sem[i]);
	V(paired_sem[i]);
	matchmaker_end(index);
	return;
}
