#include "swap.h"
#include <stdio.h>

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
      printf ("no swap device--swap disabled\n");
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
bool
swap_in (struct page *p)
{
  if (lock_held_by_current_thread(&p->frame->lock)
        && p->page_current_loc == INSWAP)
  {
    lock_acquire (&swap_lock);
    uint32_t sector = p->sector;
    char *c = (char *) p->frame->base;
    int i;
    for (i = 0; i < PAGE_SECTORS; i++) {
      block_read (swap_device, sector * PAGE_SECTORS + i, c);
      c += BLOCK_SECTOR_SIZE;
    }
    bitmap_reset (swap_bitmap, sector);
    lock_release (&swap_lock);
    return true;
  }
  return false;
}

/* Swaps out page P, which must have a locked frame. */
bool 
swap_out (struct page *p) 
{
  if (lock_held_by_current_thread(&p->frame->lock))
  {
    lock_acquire (&swap_lock);
    char *c = (char *) p->frame->base;
    size_t sector_num = bitmap_scan_and_flip (swap_bitmap, 0, 1, false);
    p->sector = sector_num;
    int i;
    for (i = 0; i < PAGE_SECTORS; i++) {
      block_write (swap_device, sector_num * PAGE_SECTORS + i, c);
      c += BLOCK_SECTOR_SIZE;
    }
    lock_release (&swap_lock);
    return true;
  }
  return false;
    
}
