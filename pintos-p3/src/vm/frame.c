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
      f->locked = false;
    }
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
  // printf("frame_alloc_and_lock time... for page at %p\n", page->uaddr);
  ASSERT(!page->frame);
  lock_acquire(&scan_lock);
  for (int i = 0; i < frame_cnt; i++) {
    struct frame *f = &frames[i];
    //printf("about to try and lock frame at %p\n", f->base);
    if (lock_try_acquire(&f->lock)) {
      if (!f->page) {
        f->page = page;
        page->frame = f;
        f->locked=true;
        //printf("LOCKING %p into frame at %p\n", page->uaddr, f->base);
        lock_release(&scan_lock);
        return f;
      }
      lock_release(&f->lock);
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
  if (p->frame) {
    ASSERT(!p->frame->locked);
    //printf("LOCKING %p into frame at %p\n", p->uaddr, p->frame->base);
    lock_acquire(&p->frame->lock);
    p->frame->locked = true;
  }
}

/* Releases frame F for use by another page.
   F must be locked for use by the current process.
   Any data in F is lost. */
void
frame_free (struct frame *f) {
  struct page* p = f -> page;
  f->page = NULL;
  p -> frame = NULL;
  frame_unlock(f);
}

/* Unlocks frame F, allowing it to be evicted.
   F must be locked for use by the current process. */
void
frame_unlock (struct frame *f) {
  ASSERT(f->locked && (f->lock).holder == thread_current());
  //printf("UNLOCKING %p into frame at %p\n", f->page->uaddr, f->base);
  lock_release(&f->lock);
  f->locked = false;
}
