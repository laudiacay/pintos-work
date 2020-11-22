#include "vm/page.h"
#include "threads/malloc.h"
#include "vm/frame.h"
#include "threads/vaddr.h"
#include "debug.h"

/* Destroys a page, which must be in the current process's
   page table.  Used as a callback for hash_destroy(). */
static void
destroy_page (struct hash_elem *p_ , void *aux UNUSED)
{
  struct page * page = hash_entry(p_, struct page, hash_elem);
  if (page->frame) {
    frame_lock(page->frame);
    frame_free(page->frame);
  }
  free(page);
}

/* Destroys the current process's page table. */
void
page_exit (void)
{

}

/* Returns the page containing the given virtual ADDRESS,
   or a null pointer if no such page exists.
   Allocates stack pages as necessary. */
struct page *
page_for_addr (const void *address, void* esp)
{
  if (address >= PHYS_BASE){
    DEBUG_PRINT(("address %p greater than PHYS_BASE at %p\n", address, PHYS_BASE));
    return NULL;
  }
  DEBUG_PRINT(("getting page for %p\n", address));
  struct thread* t = thread_current();
  ASSERT(t->supp_pt_initialized);
  struct page ht_page;

  ht_page.uaddr = pg_round_down(address);
  DEBUG_PRINT(("getting page for rounded %p\n", ht_page.uaddr));
  struct hash_elem *found_page_elem = hash_find(&t->supp_pt, &ht_page.hash_elem);
  if (found_page_elem) {
    DEBUG_PRINT(("gotteeem for addr %p\n", address));
    return hash_entry(found_page_elem, struct page, hash_elem);
  }
  if (address > PHYS_BASE - STACK_MAX) {
    ASSERT(esp);
    DEBUG_PRINT(("considering allocating a new stack frame for %p, esp is at %p\n", address, esp));
    if (address == esp - 4 || address == esp - 32) {
      DEBUG_PRINT(("yeah ok we can do that...\n", address, esp));
      struct page* p = page_allocate(address, false);
      p -> page_current_loc = TOBEZEROED;
    } else {
      
      DEBUG_PRINT(("decided not to...\n"));
      return NULL;
    }
  } else {
      DEBUG_PRINT(("address is smaller than stack_max at %p\n", PHYS_BASE-STACK_MAX));
      return NULL;
  }
}

/* Locks a frame for page P and pages it in.
   Returns true if successful, false on failure. */
static bool
do_page_in (struct page *p )
{
  DEBUG_PRINT(("CALLING DOPAGE_IN on page at uaddr %p\n", p->uaddr));
  ASSERT(!p->frame);
  struct frame* f = frame_alloc_and_lock(p);
  if (!f) return false;
  p->frame = f;
  switch (p->page_current_loc) {
  case FROMFILE:
    memset (p->frame->base, 0, PGSIZE);
    DEBUG_PRINT(("p->file_bytes: %d\n", p->file_bytes));
    off_t actual_read_bytes = file_read_at (p->file, p->frame->base,
                                              p->file_bytes, p->file_offset);
    if (p->file_bytes != actual_read_bytes){
      DEBUG_PRINT(("those were not the same... actual_read_bytes was %d\n", actual_read_bytes));
      return false;
    }
    break;
  case TOBEZEROED:
    memset(p->frame->base, 0, PGSIZE);
    break;
  case INFRAME:
    PANIC("should not be calling this if we are in the frame");
    break;
  case INSWAP:
    PANIC("swap unimplemented");
    break;
  default:
    PANIC("whats this page current loc um");
    break;
  }
  p -> page_current_loc = INFRAME;
  return pagedir_set_page(thread_current()->pagedir, p->uaddr, p->frame->base, true);
}

/* Faults in the page containing FAULT_ADDR.
   Returns true if successful, false on failure. */
bool
page_in (void *fault_addr, void* esp)
{
  DEBUG_PRINT(("CALLING PAGE_IN on %p\n", fault_addr));
  //if (esp > fault_addr && esp != fault_addr + 4 && esp != fault_addr + 32) {
  //  DEBUG_PRINT(("esp is %p, fault_addr is %p, this is not going to work\n", esp, fault_addr));
  //  return false;
  //}
  struct page* p = page_for_addr(fault_addr, esp);
  if (!p) {
    DEBUG_PRINT(("could not get page for address\n"));
    return false;
  }

  // ?? ok now what tho...
  // i think step 1 is to get it a slot in the frame table
  frame_lock(p);
  if (p->frame == NULL) {
    if (!do_page_in(p)) {
      DEBUG_PRINT(("failed to do_page_in\n"));
      return false;
    }
  }
  // step 2 is to copy it into the frame table
  frame_unlock(p->frame);
  DEBUG_PRINT(("its in p->frame at... %p\n", p->frame));
  return true;
}

/* Evicts page P.
   P must have a locked frame.
   Return true if successful, false on failure. */
bool
page_out (struct page *p )
{
  return false;
}

/* Returns true if page P's data has been accessed recently,
   false otherwise.
   P must have a frame locked into memory. */
bool
page_accessed_recently (struct page *p ) 
{
  return false;
}

/* Adds a mapping for user virtual address VADDR to the page hash
   table. Fails if VADDR is already mapped or if memory
   allocation fails. */
struct page *
page_allocate (void *vaddr, bool read_only)
{
  DEBUG_PRINT(("allocating page for vaddr: %p\n", vaddr));
   
   struct page *p = malloc(sizeof(struct page));
   if (!p)
      return NULL;
   p->uaddr = pg_round_down(vaddr);
   p->read_only = read_only;
   p->frame = NULL;
   p->page_current_loc = INIT;

   struct hash *s_pt = &thread_current()->supp_pt;
   struct hash_elem *e = hash_insert (s_pt, &p->hash_elem);
   if (e != NULL) {
     free(p);
     return NULL;
   }
   DEBUG_PRINT(("allocated page for vaddr: %p\n", vaddr));

   return p;

}

/* Evicts the page containing address VADDR
   and removes it from the page table. */
void
page_deallocate (void *vaddr )
{
  struct thread* t = thread_current();
  ASSERT(t->supp_pt_initialized);
  struct page ht_page;

  ht_page.uaddr = pg_round_down(vaddr);
  struct hash_elem *found_page_elem = hash_delete(&t->supp_pt, &ht_page.hash_elem);
  if (found_page_elem) {
    struct page * page = hash_entry(found_page_elem, struct page, hash_elem);
    frame_lock(page);
    page_out(page);
    destroy_page(found_page_elem, NULL);
  }
}

/* Returns a hash value for the page that E refers to. */
unsigned
page_hash (const struct hash_elem *e , void *aux UNUSED)
{
   const struct page *p = hash_entry (e, struct page, hash_elem);
   return hash_bytes (&p->uaddr, sizeof p->uaddr);
}

/* Returns true if page A precedes page B. */
bool
page_less (const struct hash_elem *a_, const struct hash_elem *b_, void *aux UNUSED)
{
   const struct page *a = hash_entry (a_, struct page, hash_elem);
   const struct page *b = hash_entry (b_, struct page, hash_elem);
   return a->uaddr < b->uaddr;
}

/* Tries to lock the page containing ADDR into physical memory.
   If WILL_WRITE is true, the page must be writeable;
   otherwise it may be read-only.
   Returns true if successful, false on failure. */
bool
page_lock (const void *addr , bool will_write, void* esp)
{
  // TODO what do i do with will_write???
  DEBUG_PRINT(("CALLING PAGE_LOCK on addr %p\n", addr));
  struct page* p = page_for_addr(addr, esp);
  //DEBUG_PRINT(("what came out of page_for_addr: %p\n", p));
  if (!p) {
    //DEBUG_PRINT(("failed to get page for addr... %p\n", addr));
    return false;
  }

  // ?? ok now what tho...
  // i think step 1 is to get it a slot in the frame table
  if (!p->frame) {
    //DEBUG_PRINT(("ok it had no frame, good... %p\n", addr));
    if (!do_page_in(p)) {
      //DEBUG_PRINT(("failed to do page in for addr... %p\n", addr));
      return false;
    }
  } else {
    //DEBUG_PRINT(("p had a frame and it was... %p\n", p->frame));
    frame_lock(p);
  }
  // step 2 is to copy it into the frame table
  return true;
}

/* Unlocks a page locked with page_lock(). */
void
page_unlock (const void *addr)
{
  DEBUG_PRINT(("CALLING PAGE_UNLOCK\n"));
  struct page* p = page_for_addr(addr, NULL);
  //DEBUG_PRINT(("what came out of page_for_addr: %p\n", p));
  if (!p) {
    DEBUG_PRINT(("failed to get page for addr... %p\n", addr));
    PANIC("we should be unlocking something that exists??");
  }
  ASSERT(p->frame);
  frame_unlock(p->frame);
}


