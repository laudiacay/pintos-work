#ifndef VM_PAGE_H
#define VM_PAGE_H

#define STACK_MAX (1024 * 1024)

#include "lib/kernel/hash.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/thread.h"

enum page_current_loc{FROMFILE, INFRAME, TOBEZEROED, INSWAP, INIT};

struct page {
	void *uaddr;
  enum page_current_loc page_current_loc;
	struct file *file;
	off_t file_offset;
	size_t file_bytes;
  struct frame* frame;
  bool read_only;
	struct hash_elem hash_elem;
};

static void destroy_page (struct hash_elem *p_, void *aux);
void page_exit (void);
struct page *page_for_addr (const void *address);
static bool do_page_in (struct page *p);
bool page_in (void *fault_addr);
bool page_out (struct page *p);
bool page_accessed_recently (struct page *p);
struct page * page_allocate (void *vaddr, bool read_only);
void page_deallocate (void *vaddr);
unsigned page_hash (const struct hash_elem *e, void *aux);
bool page_less (const struct hash_elem *a_, const struct hash_elem *b_, void *aux);
bool page_lock (const void *addr, bool will_write);
void page_unlock (const void *addr);
#endif
