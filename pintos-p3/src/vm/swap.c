#include "swap.h"
#include <stdio.h>
#include "debug.h"

#ifndef DEBUG_BULLSHIT
#define DEBUG_BULLSHIT
#ifdef DEBUG
#include <stdio.h>
# define DEBUG_PRINT(x) printf("THREAD: %p ", thread_current()); printf x 
#else
# define DEBUG_PRINT(x) do {} while (0)
#endif
#endif
/*

Managing the swap table

You should handle picking an unused swap slot for evicting a page from its
frame to the swap partition. And handle freeing a swap slot which its page
is read back.

You can use the BLOCK_SWAP block device for swapping, obtaining the struct
block that represents it by calling block_get_role(). Also to attach a swap
disk, please see the documentation.

and to attach a swap disk for a single run, use this option ‘--swap-size=n’

*/

// we just provide swap_init() for swap.c
// the rest is your responsibility

/* Set up*/
void
swap_init (void)
{
  swap_device = block_get_role (BLOCK_SWAP);
  if (swap_device == NULL)
    {
      PANIC("no swap device--swap disabled\n");
      swap_bitmap = bitmap_create (0);
    }
  else
    swap_bitmap = bitmap_create (block_size (swap_device)
                                 / PAGE_SECTORS);
  if (swap_bitmap == NULL)
    PANIC ("couldn't create swap bitmap");
  lock_init (&swap_lock);
}

/* Swaps in page P, which must have a locked frame
   (and be swapped out). */
void
swap_in (struct page *p)
{
  DEBUG_PRINT(("calling swap_in on page at %p\n", p->uaddr));
  ASSERT(p);
  ASSERT(p->frame);
  ASSERT(p->frame->lock.holder == thread_current());
  ASSERT(p->page_current_loc == INSWAP);
    lock_acquire (&swap_lock);
    ASSERT(p);
    ASSERT(p->frame);
    ASSERT(p->frame->lock.holder == thread_current());
    ASSERT(p->page_current_loc == INSWAP);
    uint32_t sector = p->sector;
    DEBUG_PRINT(("coming from sector %d\n", sector));
    void *c = p->frame->base;
    int i;
    for (i = 0; i < PAGE_SECTORS; i++) {
      block_read (swap_device, sector * PAGE_SECTORS + i, c);
      c += BLOCK_SECTOR_SIZE;
    }
    bitmap_reset (swap_bitmap, sector);
    lock_release (&swap_lock);
    p->sector = -1;
    DEBUG_PRINT(("finished calling swap_in on page at %p\n", p->uaddr));
}

/* Swaps out page P, which must have a locked frame. */
void 
swap_out (struct page *p) 
{
  DEBUG_PRINT(("calling swap_out on page at %p\n", p->uaddr));
  ASSERT(p);
  ASSERT(p->frame);
  ASSERT(p->page_current_loc == INFRAME);
  ASSERT(p->frame->lock.holder == thread_current());
  //DEBUG_PRINT(("made it thru the asserts... %p\n", p->uaddr));
    lock_acquire (&swap_lock);
    ASSERT(p);
    ASSERT(p->frame);
    ASSERT(p->page_current_loc == INFRAME);
    ASSERT(p->frame->lock.holder == thread_current());
    void *c = p->frame->base;
    //DEBUG_PRINT(("<1>\n"));
    //DEBUG_PRINT(("c: %p\n", c));
    //DEBUG_PRINT(("bitmap at %p, size is %u\n", &swap_bitmap, bitmap_size(&swap_bitmap)));
    size_t sector_num = bitmap_scan_and_flip (swap_bitmap, 0, 1, false);
    if (sector_num == BITMAP_ERROR) PANIC("bitmap error\n");
    p->sector = sector_num;
    DEBUG_PRINT(("going to sector %d\n", sector_num));
    //DEBUG_PRINT(("<2>\n"));
    int i;
    for (i = 0; i < PAGE_SECTORS; i++) {
      //DEBUG_PRINT(("<2a>\n"));
      block_write (swap_device, sector_num * PAGE_SECTORS + i, c);
      //DEBUG_PRINT(("<2b>\n"));
      c += BLOCK_SECTOR_SIZE;
      //DEBUG_PRINT(("<2c>\n"));
    }
    //DEBUG_PRINT(("<3>\n"));
    
    lock_release (&swap_lock);
    DEBUG_PRINT(("finished calling swap_out on page at %p\n", p->uaddr));
}
