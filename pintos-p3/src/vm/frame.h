#include <synch.h>
#include <page.h>

struct frame {
	struct page* page;
	struct lock lock;
	void *base;
};

static struct frame* frames;
static size_t frame_cnt;

static struct lock scan_lock;
static size_t hand;


void frame_init (void);
static struct frame *try_frame_alloc_and_lock (struct page *page);
static struct frame *frame_alloc_and_lock (struct page *page);
void frame_lock (struct page *p);
void frame_free (struct frame *f);
void frame_unlock (struct frame *f);