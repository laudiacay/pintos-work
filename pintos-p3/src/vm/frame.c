#include "frame.h"
#include "threads/malloc.h"
#include "threads/palloc.h"
#include "threads/loader.h"
#include <debug.h>
#include <random.h>

/*
Managing the frame table

The main job is to obtain a free frame to map a page to. To do so:

1. Easy situation is there is a free frame in frame table and it can be
obtained. If there is no free frame, you need to choose a frame to evict
using your page replacement algorithm based on setting accessed and dirty
bits for each page. See section 4.1.5.1 and A.7.3 to know details of
replacement algorithm(accessed and dirty bits) If no frame can be evicted
without allocating a swap slot and swap is full, you should panic the
kernel.

2. remove references from any page table that refers to.

3.write the page to file system or swap.

*/



void
frame_init (void)
{
  void *base;

  lock_init (&scan_lock);

  frames = malloc (sizeof *frames * init_ram_pages);
  if (frames == NULL)
    PANIC ("out of memory allocating page frames");

  while ((base = palloc_get_page (PAL_USER)) != NULL)
    {
      struct frame *f = &frames[frame_cnt++];
      lock_init (&f->lock);
      f->base = base;
      f->page = NULL;
      f->locked = false;
    }
  random_init(3);
}

/* Tries to allocate and lock a frame for PAGE.
   Returns the frame if successful, false on failure. */
struct frame *
try_frame_alloc_and_lock (struct page *page)
{
  return NULL;

}

/* Tries really hard to allocate and lock a frame for PAGE.
   PAGE may not have a frame.
   Returns the frame if successful, false on failure. */
struct frame *
frame_alloc_and_lock (struct page *page)
{
  DEBUG_PRINT(("frame_alloc_and_lock time... for page at %p\n", page->uaddr));
  ASSERT(!page->frame);
  DEBUG_PRINT(("hmm\n"));
  lock_acquire(&scan_lock);
  ASSERT(!page->frame);
  DEBUG_PRINT(("mmhmm\n"));
  for (int i = 0; i < frame_cnt; i++) {
    struct frame *f = &frames[i];
    //printf("about to try and lock frame at %p\n", f->base);
    if (lock_try_acquire(&f->lock)) {
      //DEBUG_PRINT(("coolbeans i have a lock for %p\n", f->base));
      if (!f->page) {
        DEBUG_PRINT(("LOCKING %p into frame at %p\n", page->uaddr, f->base));

        f->page = page;
        page->frame = f;
        f->locked=true;
        lock_release(&scan_lock);
        return f;
      }
      lock_release(&f->lock);
    }
  }

  // ok haha we are going to try to evict something random until we manage it :)
  while(1) {
    DEBUG_PRINT(("need to evict... trying again\n"));

    unsigned int to_evict = random_ulong() % frame_cnt;
    struct frame *f = &frames[to_evict];

    DEBUG_PRINT(("attempting to evict frame at base_addr %p\n", f->base));
    if (lock_try_acquire(&f->lock)) {
      f->locked = true;
      struct page* page_to_evict = f->page;
      ASSERT(page_to_evict);
      ASSERT(page_to_evict->frame->page == page_to_evict);
      DEBUG_PRINT(("got the lock, the page uaddr is %p\n", f->page->uaddr));
      ASSERT(page_to_evict->page_current_loc == INFRAME);
      page_out(page_to_evict);
      DEBUG_PRINT(("finished calling page_out on %p\n", page_to_evict->uaddr));
      ASSERT(f);
      ASSERT(page);
      f->page = page;
      page->frame = f;
      frame_lock(page);
      lock_release(&scan_lock);
      DEBUG_PRINT(("evicted %p and installed %p at frame %p\n",page_to_evict->uaddr, f->page->uaddr, f->base));
      return f;
    }
  }
  PANIC ("no free frames");
  lock_release(&scan_lock);
  return NULL;

}

/* Locks P's frame into memory, if it has one.
   Upon return, p->frame will not change until P is unlocked. */
void
frame_lock (struct page *p) {
  ASSERT(p);
  struct frame *f = p->frame;
  if (f) {
    DEBUG_PRINT(("LOCKING %p into frame at %p\n", p->uaddr, p->frame->base));
    lock_acquire(&f->lock);
    if (f != p->frame) {
      DEBUG_PRINT(("frame %p moved out from under page %p\n", f-> base, p->uaddr));
      lock_release(&f->lock);
      return;
    }
    ASSERT(!p->frame->locked);
    p->frame->locked = true;
    ASSERT(p == p->frame->page);
  }
}

/* Releases frame F for use by another page.
   F must be locked for use by the current process.
   Any data in F is lost. */
void
frame_free (struct frame *f) {

  ASSERT(f);
  ASSERT(f->locked && (f->lock).holder == thread_current());
  ASSERT(f->page);
  DEBUG_PRINT(("freeing frame %p from page %p\n", f->page->uaddr, f->base));
  struct page* p = f -> page;
  f->page = NULL;
  p -> frame = NULL;
  lock_release(&f->lock);
  f->locked = false;
}

/* Unlocks frame F, allowing it to be evicted.
   F must be locked for use by the current process. */
void
frame_unlock (struct frame *f) {
  ASSERT(f);
  ASSERT(f->locked && (f->lock).holder == thread_current());
  ASSERT(f->page);
  DEBUG_PRINT(("UNLOCKING %p from frame at %p\n", f->page->uaddr, f->base));
  lock_release(&f->lock);
  f->locked = false;
}
