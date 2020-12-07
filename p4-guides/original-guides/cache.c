#include "filesys/cache.h"
#include <debug.h>
#include <string.h>
#include "filesys/filesys.h"
#include "devices/timer.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/thread.h"

#define INVALID_SECTOR ((block_sector_t) -1)


struct cache_block
  {
    struct lock block_lock;
    struct condition no_readers_or_writers;
    struct condition no_writers;
    int readers, read_waiters;
    int writers, write_waiters;
    block_sector_t sector;
    bool up_to_date;
    bool dirty;
    struct lock data_lock;
    uint8_t data[BLOCK_SECTOR_SIZE];
  };

/* Cache. */
#define CACHE_CNT 64
struct cache_block cache[CACHE_CNT];
struct lock cache_sync;
static int hand = 0;


static void flushd_init (void);
static void readaheadd_init (void);
static void readaheadd_submit (block_sector_t sector);


/* Initializes cache. */
void
cache_init (void)
{
}

/* Flushes cache to disk. */
void
cache_flush (void)
{
  // block_write (fs_device, b->sector, b->data);
}

/* Locks the given SECTOR into the cache and returns the cache
   block.
   If TYPE is EXCLUSIVE, then the block returned will be locked
   only by the caller.  The calling thread must not already
   have any lock on the block.
   If TYPE is NON_EXCLUSIVE, then block returned may be locked by
   any number of other callers.  The calling thread may already
   have any number of non-exclusive locks on the block. */
struct cache_block *
cache_lock (block_sector_t sector, enum lock_type type)
{
  int i;

 try_again:

  /* Is the block already in-cache? */

  /* Not in cache.  Find empty slot. */

  /* No empty slots.  Evict something. */




  /* Wait for cache contention to die down. */

  // sometimes, you might get into a situation where you
  // cannot find a block to evict, or you cannot lock
  // the desired block. If that's the case there might
  // some contention. So the safest way to do this, is to
  // release the cache_sync lock, and sleep for 1 sec, and
  // try again the whole operation.

  lock_release (&cache_sync);
  timer_msleep (1000);
  goto try_again;
}

/* Bring block B up-to-date, by reading it from disk if
   necessary, and return a pointer to its data.
   The caller must have an exclusive or non-exclusive lock on
   B. */
void *
cache_read (struct cache_block *b)
{
  // ...
  //      block_read (fs_device, b->sector, b->data);
  // ...
}

/* Zero out block B, without reading it from disk, and return a
   pointer to the zeroed data.
   The caller must have an exclusive lock on B. */
void *
cache_zero (struct cache_block *b)
{
  // ...
  //  memset (b->data, 0, BLOCK_SECTOR_SIZE);
  // ...

}

/* Marks block B as dirty, so that it will be written back to
   disk before eviction.
   The caller must have a read or write lock on B,
   and B must be up-to-date. */
void
cache_dirty (struct cache_block *b)
{
  // ...
}

/* Unlocks block B.
   If B is no longer locked by any thread, then it becomes a
   candidate for immediate eviction. */
void
cache_unlock (struct cache_block *b)
{
  // ...
}

/* If SECTOR is in the cache, evicts it immediately without
   writing it back to disk (even if dirty).
   The block must be entirely unused. */
void
cache_free (block_sector_t sector)
{
  // ...
}


/* Flush daemon. */

static void flushd (void *aux);

/* Initializes flush daemon. */
static void
flushd_init (void)
{
  thread_create ("flushd", PRI_MIN, flushd, NULL);
}

/* Flush daemon thread. */
static void
flushd (void *aux UNUSED)
{
  for (;;)
    {
      timer_msleep (30 * 1000);
      cache_flush ();
    }
}

