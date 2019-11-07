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
 * Driver code is in kern/tests/synchprobs.c We will replace that file. This
 * file is yours to modify as you see fit.
 *
 * You should implement your solution to the stoplight problem below. The
 * quadrant and direction mappings for reference: (although the problem is, of
 * course, stable under rotation)
 *
 *   |0 |
 * -     --
 *    01  1
 * 3  32
 * --    --
 *   | 2|
 *
 * As way to think about it, assuming cars drive on the right: a car entering
 * the intersection from direction X will enter intersection quadrant X first.
 * The semantics of the problem are that once a car enters any quadrant it has
 * to be somewhere in the intersection until it call leaveIntersection(),
 * which it should call while in the final quadrant.
 *
 * As an example, let's say a car approaches the intersection and needs to
 * pass through quadrants 0, 3 and 2. Once you call inQuadrant(0), the car is
 * considered in quadrant 0 until you call inQuadrant(3). After you call
 * inQuadrant(2), the car is considered in quadrant 2 until you call
 * leaveIntersection().
 *
 * You will probably want to write some helper functions to assist with the
 * mappings. Modular arithmetic can help, e.g. a car passing straight through
 * the intersection entering from direction X will leave to direction (X + 2)
 * % 4 and pass through quadrants X and (X + 3) % 4.  Boo-yah.
 *
 * Your solutions below should call the inQuadrant() and leaveIntersection()
 * functions in synchprobs.c to record their progress.
 */

#include <types.h>
#include <lib.h>
#include <thread.h>
#include <test.h>
#include <synch.h>

/*
 * Called by the driver during initialization.
 */

static struct lock* lk;
static struct cv* cv;
static volatile unsigned roadnum = 5;
static struct lock* roadlocks[4];

void
stoplight_init() {
	lk = lock_create("stoplight_lock");
	if(lk == NULL){
		panic("stoplight_init create lock failed.\n");
	}
	cv = cv_create("stoplight_cv");
	if(cv == NULL){
		panic("stoplight_init create cv failed.\n");
	}
	for(int i = 0; i != 4; i++)
	{
		roadlocks[i] = lock_create("road_lock");
		if(roadlocks[i] == NULL){
			panic("stoplight_init create lock failed.\n");
		}
	}
	return;
}

/*
 * Called by the driver during teardown.
 */

void stoplight_cleanup() {
	lock_destroy(lk);
	cv_destroy(cv);
	for(int i = 0; i != 4; i++){
		lock_destroy(roadlocks[i]);
	}
	return;
}

void
turnright(uint32_t direction, uint32_t index)
{
	(void)direction;
	(void)index;
	/*
	 * Implement this function.
	 */
	lock_acquire(lk);
	while(roadnum == 0){
		cv_wait(cv, lk);
	}
	roadnum -= 1;
	lock_release(lk);
	lock_acquire(roadlocks[direction]);
	inQuadrant(direction, index);
	leaveIntersection(index);
	lock_release(roadlocks[direction]);
	lock_acquire(lk);
	roadnum += 1;
	cv_broadcast(cv, lk);
	lock_release(lk);
	return;
}
void
gostraight(uint32_t direction, uint32_t index)
{
	(void)direction;
	(void)index;
	/*
	 * Implement this function.
	 */
	uint32_t step = (direction + 3) % 4;
	lock_acquire(lk);
	while(roadnum < 2){
		cv_wait(cv, lk);
	}
	roadnum -= 2;
	lock_release(lk);
	lock_acquire(roadlocks[direction]);
	lock_acquire(roadlocks[step]);
	inQuadrant(direction, index);
	inQuadrant(step, index);
	leaveIntersection(index);
	lock_release(roadlocks[step]);
	lock_release(roadlocks[direction]);
	lock_acquire(lk);
	roadnum += 2;
	cv_broadcast(cv, lk);
	lock_release(lk);
	return;
}
void
turnleft(uint32_t direction, uint32_t index)
{
	(void)direction;
	(void)index;
	/*
	 * Implement this function.
	 */
	uint32_t step1 = (direction + 3) % 4;
	uint32_t step2 = (step1 + 3) % 4;
	lock_acquire(lk);
	while(roadnum < 3){
		cv_wait(cv, lk);
	}
	roadnum -= 3;
	lock_release(lk);
	lock_acquire(roadlocks[direction]);
	lock_acquire(roadlocks[step1]);
	lock_acquire(roadlocks[step2]);
	inQuadrant(direction, index);
	inQuadrant(step1, index);
	inQuadrant(step2, index);
	leaveIntersection(index);
	lock_release(roadlocks[step2]);
	lock_release(roadlocks[step1]);
	lock_release(roadlocks[direction]);
	lock_acquire(lk);
	roadnum += 3;
	cv_broadcast(cv, lk);
	lock_release(lk);
	return;
}
