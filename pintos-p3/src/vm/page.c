#include "page.h"
#include "threads/malloc.h"

/* Destroys a page, which must be in the current process's
   page table.  Used as a callback for hash_destroy(). */
static void
destroy_page (struct hash_elem *p_ , void *aux UNUSED)
{

}

/* Destroys the current process's page table. */
void
page_exit (void)
{

}

/* Returns the page containing the given virtual ADDRESS,
   or a null pointer if no such page exists.
   Allocates stack pages as necessary. */
static struct page *
page_for_addr (const void *address )
{
  struct thread* t = thread_current();
  ASSERT(t->supp_pt_initialized);
  struct page ht_page;
  ht_page.uaddr = pg_round_down(address);
  struct hash_elem *found_page_elem = hash_find(&t->supp_pt, &ht_page.hash_elem);
  if (found_page_elem) return hash_entry(found_page_elem, struct page, hash_elem);

  return page_allocate(address, false);
}

/* Locks a frame for page P and pages it in.
   Returns true if successful, false on failure. */
static bool
do_page_in (struct page *p )
{
  return false;
}

/* Faults in the page containing FAULT_ADDR.
   Returns true if successful, false on failure. */
bool
page_in (void *fault_addr)
{
  return false;
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
   struct page *p = malloc(sizeof(struct page));
   if (!p)
      return NULL;
   p->uaddr = vaddr;

   struct hash *s_pt = &thread_current()->supp_pt;
   struct hash_elem *e = hash_insert (s_pt, &p->hash_elem);
   if (e != NULL) {
     return NULL;
   }
   

   return p;

}

/* Evicts the page containing address VADDR
   and removes it from the page table. */
void
page_deallocate (void *vaddr )
{

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
page_lock (const void *addr , bool will_write )
{
  return false;
}

/* Unlocks a page locked with page_lock(). */
void
page_unlock (const void *addr )
{
}


