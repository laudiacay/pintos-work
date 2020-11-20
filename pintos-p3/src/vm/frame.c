#include "frame.h"
#include "threads/malloc.h"
#include "threads/palloc.h"
#include "threads/loader.h"
#include <debug.h>

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
    }
}

/* Tries to allocate and lock a frame for PAGE.
   Returns the frame if successful, false on failure. */
static struct frame *
try_frame_alloc_and_lock (struct page *page)
{
  return NULL;

}

/* Tries really hard to allocate and lock a frame for PAGE.
   Returns the frame if successful, false on failure. */
static struct frame *
frame_alloc_and_lock (struct page *page)
{
  for (int i = 0; i < frame_cnt; i++) {
    struct frame *f = &frames[i];
    if (lock_try_acquire(&f->lock)) {
      f->page = page;
      return f;
    }
  }
  PANIC ("no free frames");
  return NULL;

}

/* Locks P's frame into memory, if it has one.
   Upon return, p->frame will not change until P is unlocked. */
void
frame_lock (struct page *p) {
  

}

/* Releases frame F for use by another page.
   F must be locked for use by the current process.
   Any data in F is lost. */
void
frame_free (struct frame *f) {
  frame_unlock(f);
  f->page = NULL;
}

/* Unlocks frame F, allowing it to be evicted.
   F must be locked for use by the current process. */
void
frame_unlock (struct frame *f) {
  ASSERT((f->lock).holder == thread_current());
  lock_release(&f->lock);
}
