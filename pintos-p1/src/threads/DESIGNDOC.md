			+--------------------+
			|        CS 140      |
			| PROJECT 1: THREADS |
			|   DESIGN DOCUMENT  |
			+--------------------+
				   
---- GROUP ----

>> Fill in the names and email addresses of your group members.

Claudia Richoux    laudecay@uchicago.edu
Kate Hu            katehu@uchicago.edu

---- PRELIMINARIES ----

>> If you have any preliminary comments on your submission, notes for the
>> TAs, or extra credit, please give them here.

>> Please cite any offline or online sources you consulted while
>> preparing your submission, other than the Pintos documentation, course
>> text, lecture notes, and course staff.

			     ALARM CLOCK
			     ===========

---- DATA STRUCTURES ----

>> A1: Copy here the declaration of each new or changed `struct' or
>> `struct' member, global or static variable, `typedef', or
>> enumeration.  Identify the purpose of each in 25 words or less.

New members in struct thread:
1. int64_t wakeup_time: tracks the wakeup time of a sleeping thread
2. struct semaphore sleep_semaphore: keeps track of a binary value
	that indicates whether the thread is sleeping or not
3. struct list_elem waitlist_elem: list element that goes into the
   wait list of sleeping threads

struct list wait_list: keeps track of the list of sleeping threads

bool first_wakeup_from_l_elem(const struct list_elem* a,
		const struct list_elem* b, void* aux):
a function to compare two threads using their wake up time when
inserting into the wait list


---- ALGORITHMS ----

>> A2: Briefly describe what happens in a call to timer_sleep(),
>> including the effects of the timer interrupt handler.

In timer_sleep(), the current thread's wake up time is calculated
and stored. It is then put to sleep and inserted into a list of
sleeping threads sorted by their wake up times. The timer
interrupt handler wakes up any thread in the list of sleeping
threads that needs to wake up.

>> A3: What steps are taken to minimize the amount of time spent in
>> the timer interrupt handler?

We use ordered insert when inserting threads into the wait list so
that the list is always sorted, with the thread that has the earliest
wake up time being at the front. In this way when we search for 
threads to wake up in timer interrupt handler, we can traverse the
wait list starting at the front and break immediately when we
encounter a thread that shouldn't wake up.

---- SYNCHRONIZATION ----

>> A4: How are race conditions avoided when multiple threads call
>> timer_sleep() simultaneously?

???

>> A5: How are race conditions avoided when a timer interrupt occurs
>> during a call to timer_sleep()?

We disabled interrupts to protect inserting a thread into the wait
list, so the thread won't be interrupted when it's accessing the
global wait list.

---- RATIONALE ----

>> A6: Why did you choose this design?  In what ways is it superior to
>> another design you considered?

???

			 PRIORITY SCHEDULING
			 ===================

---- DATA STRUCTURES ----

>> B1: Copy here the declaration of each new or changed `struct' or
>> `struct' member, global or static variable, `typedef', or
>> enumeration.  Identify the purpose of each in 25 words or less.

New members in struct lock:
1. struct list_elem elem: list element that goes in lock holder's
	list of locks it holds
2. int lock_max_priority: tracks current maximum priority that
	holding this lock would give a thread

New members in struct semaphore_elem:
1. struct list_elem elem: list element that goes in the cond waiter list
2. struct semaphore semaphore: keeps track of a binary value that
	indicates whether the condition variable is available or not. It
	also contains a semaphore waitlist that contains threads waiting for
	the condition variable.
3. int priority: keeps track of the cond waiter's priority

New members in struct thread:
1. int original_pri: keeps track of the thread's priority that is not 
	donated
2. int priority: keeps track of the thread's donated priority. If there
	is no donated priority, it equals original_pri
3. struct list locks_held: tracks the list of locks that this thread holds
4. struct lock* blocked_by: tracks which lock this thread is waiting on


>> B2: Explain the data structure used to track priority donation.
>> Use ASCII art to diagram a nested donation.  (Alternately, submit a
>> .png file.)

???

---- ALGORITHMS ----

>> B3: How do you ensure that the highest priority thread waiting for
>> a lock, semaphore, or condition variable wakes up first?

A thread is inserted into the waiter list according to its priority
so that the highest priority thread is always at the front of the list.
To take care of the case where a thread's priority is updated due to
priority donation while it is still in the waiter list, we first sort
the waiter list before returning the first element in it when we wake 
up a thread.

>> B4: Describe the sequence of events when a call to lock_acquire()
>> causes a priority donation.  How is nested donation handled?

The lock is set to be the lock that this thread is waiting on. If this
thread has a higher priority than the lock's current priority that it
should give its holder, update that priority to be the thread's priority.
Then the thread's priority is recursed up along the donation chain:
this thread gives its priority to its blocker (the holder of the lock
that this thread is waiting on,) and the blocker gives its priority to
its blocker, and so on, until we reach a blocker that is not waiting
on any lock. Then we sort the ready list in case that any thread that
is already in the ready list gets an updated priority. Finally this thread
is inserted into the lock's waiter list. 

>> B5: Describe the sequence of events when lock_release() is called
>> on a lock that a higher-priority thread is waiting for.

The lock's holder is set to be NULL. The lock is also removed from the
caller thread's list of locks that it holds. Then we set this thread's
priority based on the list of locks it still holds and its own base
priority. From the list of locks it still holds, we find the lock with
the maximum priority that a thread needs to have while holding this
lock. If the thread is not holding any lock, it means that it is not
blocking any thread and it won't have donated priority, then its 
priority is set to be its base priority. If there is such a lock,
we set the thread's priority to be the priority associated with this lock.
Finally we call sema up on the sempahore of the lock that just gets
released to wake up the highest priority thread that is waiting on this lock.

---- SYNCHRONIZATION ----

>> B6: Describe a potential race in thread_set_priority() and explain
>> how your implementation avoids it.  Can you use a lock to avoid
>> this race?

??? not sure why there would be race condition in set priority

---- RATIONALE ----

>> B7: Why did you choose this design?  In what ways is it superior to
>> another design you considered?

???

			  ADVANCED SCHEDULER
			  ==================

---- DATA STRUCTURES ----

>> C1: Copy here the declaration of each new or changed `struct' or
>> `struct' member, global or static variable, `typedef', or
>> enumeration.  Identify the purpose of each in 25 words or less.

---- ALGORITHMS ----

>> C2: Suppose threads A, B, and C have nice values 0, 1, and 2.  Each
>> has a recent_cpu value of 0.  Fill in the table below showing the
>> scheduling decision and the priority and recent_cpu values for each
>> thread after each given number of timer ticks:

timer  recent_cpu    priority   thread
ticks   A   B   C   A   B   C   to run
-----  --  --  --  --  --  --   ------
 0
 4
 8
12
16
20
24
28
32
36

>> C3: Did any ambiguities in the scheduler specification make values
>> in the table uncertain?  If so, what rule did you use to resolve
>> them?  Does this match the behavior of your scheduler?

>> C4: How is the way you divided the cost of scheduling between code
>> inside and outside interrupt context likely to affect performance?

---- RATIONALE ----

>> C5: Briefly critique your design, pointing out advantages and
>> disadvantages in your design choices.  If you were to have extra
>> time to work on this part of the project, how might you choose to
>> refine or improve your design?

>> C6: The assignment explains arithmetic for fixed-point math in
>> detail, but it leaves it open to you to implement it.  Why did you
>> decide to implement it the way you did?  If you created an
>> abstraction layer for fixed-point math, that is, an abstract data
>> type and/or a set of functions or macros to manipulate fixed-point
>> numbers, why did you do so?  If not, why not?

			   SURVEY QUESTIONS
			   ================

Answering these questions is optional, but it will help us improve the
course in future quarters.  Feel free to tell us anything you
want--these questions are just to spur your thoughts.  You may also
choose to respond anonymously in the course evaluations at the end of
the quarter.

>> In your opinion, was this assignment, or any one of the three problems
>> in it, too easy or too hard?  Did it take too long or too little time?

>> Did you find that working on a particular part of the assignment gave
>> you greater insight into some aspect of OS design?

>> Is there some particular fact or hint we should give students in
>> future quarters to help them solve the problems?  Conversely, did you
>> find any of our guidance to be misleading?

>> Do you have any suggestions for the TAs to more effectively assist
>> students, either for future quarters or the remaining projects?

>> Any other comments?
