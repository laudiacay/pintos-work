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
   (and be swapped out). 
bool
swap_in (struct page *p)
{
    // might want to use these functions:
    // - lock_held_by_current_thread()
    // - block_read()
    // - bitmap_reset()
}

/* Swaps out page P, which must have a locked frame. 
bool 
swap_out (struct page *p) 
{
  
  // might want to use these functions:
  // - lock_held_by_current_thread()
  // - bitmap_scan_and_flip()
  // - block_write()
    
  
  }*/
