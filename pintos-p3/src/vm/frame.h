#ifndef VM_FRAME_H
#define VM_FRAME_H

#include "threads/synch.h"
#include "threads/thread.h"

struct frame {
	struct lock lock;
  struct page* page;
	void *base;
};

static struct frame* frames;
static size_t frame_cnt;

static struct lock scan_lock;
static size_t hand;


void frame_init (void);
struct frame *try_frame_alloc_and_lock (struct page *page);
struct frame *frame_alloc_and_lock (struct page *page);
void frame_lock (struct page *p);
void frame_free (struct frame *f);
void frame_unlock (struct frame *f);

#endif
