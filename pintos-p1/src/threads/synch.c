/* This file is derived from source code for the Nachos
   instructional operating system.  The Nachos copyright notice
   is reproduced in full below. */

/* Copyright (c) 1992-1996 The Regents of the University of California.
   All rights reserved.

   Permission to use, copy, modify, and distribute this software
   and its documentation for any purpose, without fee, and
   without written agreement is hereby granted, provided that the
   above copyright notice and the following two paragraphs appear
   in all copies of this software.

   IN NO EVENT SHALL THE UNIVERSITY OF CALIFORNIA BE LIABLE TO
   ANY PARTY FOR DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR
   CONSEQUENTIAL DAMAGES ARISING OUT OF THE USE OF THIS SOFTWARE
   AND ITS DOCUMENTATION, EVEN IF THE UNIVERSITY OF CALIFORNIA
   HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

   THE UNIVERSITY OF CALIFORNIA SPECIFICALLY DISCLAIMS ANY
   WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
   WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
   PURPOSE.  THE SOFTWARE PROVIDED HEREUNDER IS ON AN "AS IS"
   BASIS, AND THE UNIVERSITY OF CALIFORNIA HAS NO OBLIGATION TO
   PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR
   MODIFICATIONS.
*/

#include "threads/synch.h"
#include <stdio.h>
#include <string.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

void update_priorities(struct thread *t);
void update_waiter_priorities(struct lock* lock);
bool sema_waiter_prio_less_func(const struct list_elem *a, const struct list_elem *b, void *aux UNUSED);
int get_max_waiter_prio(struct lock *lock);
void before_getting_in_line(struct lock* lock);
void upon_getting_the_lock(struct lock* lock);
bool list_less_func_lock_donated_priorities(const struct list_elem* a, const struct list_elem* b, void* aux UNUSED);
void update_thread_prio_based_on_locks_held(void);

bool sort_by_priority(const struct list_elem* a, const struct list_elem *b, void *aux UNUSED) {
  struct thread* t_a = list_entry(a, struct thread, elem);
  struct thread* t_b = list_entry(b, struct thread, elem);
  return t_a->priority > t_b->priority;
}

/* Initializes semaphore SEMA to VALUE.  A semaphore is a
   nonnegative integer along with two atomic operators for
   manipulating it:

   - down or "P": wait for the value to become positive, then
     decrement it.

   - up or "V": increment the value (and wake up one waiting
     thread, if any). */
void
sema_init (struct semaphore *sema, unsigned value) 
{
  ASSERT (sema != NULL);

  sema->value = value;
  list_init (&sema->waiters);
  ASSERT(list_empty(&sema->waiters));
}

/* Down or "P" operation on a semaphore.  Waits for SEMA's value
   to become positive and then atomically decrements it.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but if it sleeps then the next scheduled
   thread will probably turn interrupts back on. */
void
sema_down (struct semaphore *sema) 
{
  printf("we in semadown\n");
  enum intr_level old_level;

  ASSERT (sema != NULL);
  ASSERT (!intr_context ());

  old_level = intr_disable ();
  while (sema->value == 0) 
    {
      //list_insert_ordered (&sema->waiters, &thread_current()->elem, sort_by_priority, NULL);
      list_push_back (&sema->waiters, &thread_current ()->elem);
      thread_block ();
    }
  sema->value--;
  intr_set_level (old_level);
  printf("made it out of semadown\n");
}

/* Down or "P" operation on a semaphore, but only if the
   semaphore is not already 0.  Returns true if the semaphore is
   decremented, false otherwise.

   This function may be called from an interrupt handler. */
bool
sema_try_down (struct semaphore *sema) 
{
  enum intr_level old_level;
  bool success;

  ASSERT (sema != NULL);

  old_level = intr_disable ();
  if (sema->value > 0) 
    {
      sema->value--;
      success = true; 
    }
  else
    success = false;
  intr_set_level (old_level);

  return success;
}

/* Up or "V" operation on a semaphore.  Increments SEMA's value
   and wakes up one thread of those waiting for SEMA, if any.

   This function may be called from an interrupt handler. */
void
sema_up (struct semaphore *sema) 
{
  printf("we in semaup\n");
  enum intr_level old_level;

  ASSERT (sema != NULL);

  old_level = intr_disable ();
  if (!list_empty (&sema->waiters)) {
    struct thread* t = list_entry (list_pop_front (&sema->waiters), struct thread, elem);
    thread_unblock (t);
  }
  sema->value++;
  intr_set_level (old_level);
  printf("made it out of semaup\n");
}

static void sema_test_helper (void *sema_);

/* Self-test for semaphores that makes control "ping-pong"
   between a pair of threads.  Insert calls to printf() to see
   what's going on. */
void
sema_self_test (void) 
{
  struct semaphore sema[2];
  int i;

  printf ("Testing semaphores...");
  sema_init (&sema[0], 0);
  sema_init (&sema[1], 0);
  thread_create ("sema-test", PRI_DEFAULT, sema_test_helper, &sema);
  for (i = 0; i < 10; i++) 
    {
      sema_up (&sema[0]);
      sema_down (&sema[1]);
    }
  printf ("done.\n");
}

/* Thread function used by sema_self_test(). */
static void
sema_test_helper (void *sema_) 
{
  struct semaphore *sema = sema_;
  int i;

  for (i = 0; i < 10; i++) 
    {
      sema_down (&sema[0]);
      sema_up (&sema[1]);
    }
}

/* Initializes LOCK.  A lock can be held by at most a single
   thread at any given time.  Our locks are not "recursive", that
   is, it is an error for the thread currently holding a lock to
   try to acquire that lock.

   A lock is a specialization of a semaphore with an initial
   value of 1.  The difference between a lock and such a
   semaphore is twofold.  First, a semaphore can have a value
   greater than 1, but a lock can only be owned by a single
   thread at a time.  Second, a semaphore does not have an owner,
   meaning that one thread can "down" the semaphore and then
   another one "up" it, but with a lock the same thread must both
   acquire and release it.  When these restrictions prove
   onerous, it's a good sign that a semaphore should be used,
   instead of a lock. */
void
lock_init (struct lock *lock)
{
  ASSERT (lock != NULL);

  lock->holder = NULL;
  sema_init (&lock->semaphore, 1);
  lock->lock_max_priority = PRI_MIN;
}

#define THREAD_MAGIC 0xcd6abf4b
void update_priorities(struct thread *t) {
  while (t) {
  // assumes the thread's priority has already been bumped to the right value.
  ASSERT(t->original_pri <= t -> priority);
  // blocked, but not by a lock! done donating.
  if (t->blocked_by == NULL) {return;}
  struct thread *blocker = t->blocked_by->holder;
  // if there's nothing holding the lock.... this should never happen, but we're done.
  if (blocker == NULL) {return;}
  ASSERT(blocker->original_pri <= blocker -> priority);
  // this means that there's no more donation that needs to happen
  if (blocker->priority >= t->priority) {return;}
  // bump the blocking thread's priority
  blocker -> priority = t -> priority;
  // TODO: UPDATE READY_LIST now that this has been updated if BLOCKER is READY
  // TODO: that is if we ever bother to keep ready_list sorted...
  // if blocker is running, this'll update its prio, itll get downed in a second anyway
  // and update anything that might be blocking it
  t = blocker->status == THREAD_BLOCKED ? t : NULL;
  }
  }

void update_waiter_priorities(struct lock* lock) {
  ASSERT(&lock -> semaphore != NULL);
  // assumes lock_max_priority has been updated!
  struct list* waiters = &(lock -> semaphore).waiters;
  ASSERT(waiters!= NULL);
  //printf("list waiters length: %d\n", list_size(waiters));
  if (list_empty(waiters)) return;
  int new_prio = lock -> lock_max_priority;
  // printf("<1e>\n");
  struct list_elem* first_waiter = list_begin(waiters);
  struct list_elem* last_waiter = list_end(waiters);
  //printf("first waiter thread entry: %x\n", first_waiter);
  //printf("last waiter thread entry: %x\n", last_waiter);
  for (struct list_elem* e = list_begin (waiters); e != list_end (waiters);
       e = list_next (e)) {
    //printf("<1e loop start>\n");
      struct thread *waiting_t = list_entry (e, struct thread, elem);

      if (waiting_t -> priority < new_prio) {
          waiting_t -> priority = new_prio;
      }
      //printf("<1e loop finish>\n");
  }
}
bool sema_waiter_prio_less_func(const struct list_elem *a, const struct list_elem *b, void *aux UNUSED) {
  struct thread* t_a = list_entry(a, struct thread, elem);
  struct thread* t_b = list_entry(b, struct thread, elem);
  return t_a->priority < t_b->priority;
}

int get_max_waiter_prio(struct lock *lock) {
  ASSERT(&lock -> semaphore != NULL);
  struct list* waiters = &(lock->semaphore).waiters;
  ASSERT(waiters!= NULL);
  if (list_empty(waiters)) return PRI_MIN;
  struct thread* max_priority_waiter = list_entry(list_max(waiters, sema_waiter_prio_less_func, NULL), struct thread, elem);
  return max_priority_waiter -> priority;
}

void before_getting_in_line(struct lock* lock) {
  enum intr_level old_level = intr_disable ();
  struct thread* t = thread_current();
  //printf("<1a>\n");
  // t is blocked by this lock
  t->blocked_by = lock;
  //printf("<1b>\n");
  // does priority donation occur?
  if (t -> priority > lock -> lock_max_priority) {
    //printf("<1c>\n");
      // set lock_max_priority to bump to new level
      lock -> lock_max_priority = t -> priority;
      // update priorities of everything that's in line in front of this thread, which also are all blocked.
      //printf("<1d>\n");
      update_waiter_priorities(lock);
      //    printf("<finished updating waiter priorities ok>\n");
      // recurse up through current holders and do donations.
      update_priorities(t);
      //printf("<1f>\n");

    }
  intr_set_level (old_level);
}

void upon_getting_the_lock(struct lock* lock) {
  enum intr_level old_level = intr_disable ();
  struct thread* t = thread_current();
  // t is no longer blocked by this lock!
  t->blocked_by = NULL;
  // append this lock to t's locks that it holds.
  list_push_front(&t->locks_held, &lock->elem);
  // bump lock_max_priority back down, lock isnt blocked by this thread anymore
  lock -> lock_max_priority = get_max_waiter_prio(lock);
  intr_set_level (old_level);
}

/* Acquires LOCK, sleeping until it becomes available if
   necessary.  The lock must not already be held by the current
   thread.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but interrupts will be turned back on if
   we need to sleep. */
void
lock_acquire (struct lock *lock)
{ printf("acquiring a lock\n");
  ASSERT (lock != NULL);
  ASSERT (!intr_context ());
  ASSERT (!lock_held_by_current_thread (lock));
  printf("<1>\n");
  
  before_getting_in_line(lock);
  printf("<2>\n");
  sema_down (&lock->semaphore);
  printf("<3>\n");

  upon_getting_the_lock(lock);
  printf("<4>\n");

  lock->holder = thread_current ();
  printf("hooray motherfuckers\n");
}

/* Tries to acquires LOCK and returns true if successful or false
   on failure.  The lock must not already be held by the current
   thread.

   This function will not sleep, so it may be called within an
   interrupt handler. */
bool
lock_try_acquire (struct lock *lock)
{
  bool success;

  printf("trying to acquire.\n");
  ASSERT (lock != NULL);
  ASSERT (!lock_held_by_current_thread (lock));

  success = sema_try_down (&lock->semaphore);
  if (success){
    struct thread* t = thread_current();
    list_push_front(&t->locks_held, &lock->elem);
    lock->holder = t;
  }
  printf("survived trying to acquire.\n");
  return success;
}

bool list_less_func_lock_donated_priorities(const struct list_elem* a, const struct list_elem* b, void* aux UNUSED) {
  struct lock* lock_a = list_entry(a, struct lock, elem);
  struct lock* lock_b = list_entry(b, struct lock, elem);
  return lock_a->lock_max_priority < lock_b->lock_max_priority;
}

void update_thread_prio_based_on_locks_held(void) {
  struct thread* t = thread_current();
  struct lock* biggest_prio_lock = list_entry(list_max(&t->locks_held, list_less_func_lock_donated_priorities, NULL), struct lock, elem);
  if (biggest_prio_lock == NULL) {
    t-> priority = t->original_pri;
    return;
  }
  int donated_priority = biggest_prio_lock->lock_max_priority;
  if (donated_priority > t-> original_pri) {
      t->priority = donated_priority;
  } else {
    t->priority = t->original_pri;
  }
}

/* Releases LOCK, which must be owned by the current thread.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to release a lock within an interrupt
   handler. */
void
lock_release (struct lock *lock) 
{
  printf("releasing a lock...\n");
  ASSERT (lock != NULL);
  ASSERT (lock_held_by_current_thread (lock));
  // disabling interrupts so that current thread stays running through this whole thing
  enum intr_level old_level = intr_disable ();
  struct thread* t = thread_current();
  printf("<a>\n");
  lock->holder = NULL;
  t = thread_current();
  // remove the lock from the thread's locks_held
  printf("<b>\n");
  list_remove(&lock->elem);
  t = thread_current();
  // set own priority according to locks still held and internal priority
  update_thread_prio_based_on_locks_held();
  printf("<c>\n");
  t = thread_current();
  intr_set_level(old_level);
  printf("<d>\n");
  t = thread_current();
  sema_up (&lock->semaphore);
  printf("<e>\n");
  t = thread_current();

  printf("released a lock...\n");
}

/* Returns true if the current thread holds LOCK, false
   otherwise.  (Note that testing whether some other thread holds
   a lock would be racy.) */
bool
lock_held_by_current_thread (const struct lock *lock) 
{
  ASSERT (lock != NULL);

  return lock->holder == thread_current ();
}

/* One semaphore in a list. */
struct semaphore_elem 
  {
    struct list_elem elem;              /* List element. */
    struct semaphore semaphore;         /* This semaphore. */
    int priority;
  };

bool sort_by_sema_elem_priority(const struct list_elem* a, const struct list_elem *b, void *aux UNUSED) {
  struct semaphore_elem* s_a = list_entry(a, struct semaphore_elem, elem);
  struct semaphore_elem* s_b = list_entry(b, struct semaphore_elem, elem);
  return s_a->priority > s_b->priority;
}

/* Initializes condition variable COND.  A condition variable
   allows one piece of code to signal a condition and cooperating
   code to receive the signal and act upon it. */
void
cond_init (struct condition *cond)
{
  ASSERT (cond != NULL);

  list_init (&cond->waiters);
}

/* Atomically releases LOCK and waits for COND to be signaled by
   some other piece of code.  After COND is signaled, LOCK is
   reacquired before returning.  LOCK must be held before calling
   this function.

   The monitor implemented by this function is "Mesa" style, not
   "Hoare" style, that is, sending and receiving a signal are not
   an atomic operation.  Thus, typically the caller must recheck
   the condition after the wait completes and, if necessary, wait
   again.

   A given condition variable is associated with only a single
   lock, but one lock may be associated with any number of
   condition variables.  That is, there is a one-to-many mapping
   from locks to condition variables.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but interrupts will be turned back on if
   we need to sleep. */
void
cond_wait (struct condition *cond, struct lock *lock) 
{
  struct semaphore_elem waiter;

  ASSERT (cond != NULL);
  ASSERT (lock != NULL);
  ASSERT (!intr_context ());
  ASSERT (lock_held_by_current_thread (lock));
  
  sema_init (&waiter.semaphore, 0);
  //waiter.priority = thread_current()->priority;
  list_push_back (&cond->waiters, &waiter.elem);
  //list_insert_ordered (&cond->waiters, &waiter.elem,
  //        sort_by_sema_elem_priority, NULL);
  lock_release (lock);
  sema_down (&waiter.semaphore);
  lock_acquire (lock);
}

/* If any threads are waiting on COND (protected by LOCK), then
   this function signals one of them to wake up from its wait.
   LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
void
cond_signal (struct condition *cond, struct lock *lock UNUSED) 
{
  ASSERT (cond != NULL);
  ASSERT (lock != NULL);
  ASSERT (!intr_context ());
  ASSERT (lock_held_by_current_thread (lock));

  if (!list_empty (&cond->waiters)) {
    // wake the thread with highest priority
    // list_sort (&cond->waiters, sort_by_priority, NULL);
    struct semaphore_elem* t = list_entry (list_pop_front (&cond->waiters), struct semaphore_elem, elem);
    sema_up (&t->semaphore);
  }
    
}

/* Wakes up all threads, if any, waiting on COND (protected by
   LOCK).  LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
void
cond_broadcast (struct condition *cond, struct lock *lock) 
{
  ASSERT (cond != NULL);
  ASSERT (lock != NULL);

  while (!list_empty (&cond->waiters))
    cond_signal (cond, lock);
}
