#ifndef VM_SWAP_H
#define VM_SWAP_H

#include "threads/synch.h"
#include "page.h"
#include "frame.h"
#include "lib/kernel/bitmap.h"
#include "devices/block.h"
#include "threads/vaddr.h"

/* The swap device. */
static struct block *swap_device;

/* Used swap pages. */
static struct bitmap *swap_bitmap;

/* Protects swap_bitmap. */
static struct lock swap_lock;

/* Number of sectors per page. */
#define PAGE_SECTORS (PGSIZE / BLOCK_SECTOR_SIZE)

void swap_init (void);
void swap_in (struct page *p);
void swap_out (struct page *p);
#endif
