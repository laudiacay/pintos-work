#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"
#include "threads/thread.h"

#ifndef DEBUG_BULLSHIT
#define DEBUG_BULLSHIT
#ifdef DEBUG
#include <stdio.h>
# define DEBUG_PRINT(x) printf("THREAD: %p ", thread_current()); printf x 
#else
# define DEBUG_PRINT(x) do {} while (0)
#endif
#endif

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44

#define DIRECT_CNT 123
#define INDIRECT_CNT 1
#define DBL_INDIRECT_CNT 1
#define SECTOR_CNT (DIRECT_CNT + INDIRECT_CNT + DBL_INDIRECT_CNT)

#define PTRS_PER_SECTOR ((off_t) (BLOCK_SECTOR_SIZE / sizeof (block_sector_t)))
#define INODE_SPAN ((DIRECT_CNT                                              \
                     + PTRS_PER_SECTOR * INDIRECT_CNT                        \
                     + PTRS_PER_SECTOR * PTRS_PER_SECTOR * DBL_INDIRECT_CNT) \
                    * BLOCK_SECTOR_SIZE)

/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */
struct inode_disk
  {
    block_sector_t sectors[SECTOR_CNT];
    enum inode_type type;
    off_t length;                       /* File size in bytes. */
    unsigned magic;                     /* Magic number. */

  };

struct indirect_block_sector
{
  block_sector_t blocks[PTRS_PER_SECTOR];
};

void set_to_zeros (block_sector_t);
static void extend_file (struct inode *, off_t);

/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
static inline size_t
bytes_to_sectors (off_t size)
{
  return DIV_ROUND_UP (size, BLOCK_SECTOR_SIZE);
}

/* In-memory inode. */
struct inode 
  {
    struct list_elem elem;              /* Element in inode list. */
    block_sector_t sector;              /* Sector number of disk location. */
    int open_cnt;                       /* Number of openers. */
    bool removed;                       /* True if deleted, false otherwise. */
    struct lock lock;

// adding these fields atm so that it compiles
    struct lock deny_write_lock;
    struct condition no_writers_cond;
    int deny_write_cnt;                 /* 0: writes ok, >0: deny writes. */
    int writer_cnt;
  };

/* Returns the block device sector that contains byte offset POS
   within INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */
/*static block_sector_t
byte_to_sector (const struct inode *inode, off_t pos) 
{
  ASSERT (inode != NULL);
  if (pos < inode->data.length)
    return inode->data.sectors[pos / BLOCK_SECTOR_SIZE];
  else
    return -1;
    }*/

/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;

/* Controls access to open_inodes list. */
static struct lock open_inodes_lock;

static void deallocate_inode (const struct inode *);

/* Initializes the inode module. */
void
inode_init (void) 
{
  list_init (&open_inodes);
  lock_init (&open_inodes_lock);
}

/* Initializes an inode of the given TYPE, writes the new inode
   to sector SECTOR on the file system device, and returns the
   inode thus created.  Returns a null pointer if unsuccessful,
   in which case SECTOR is released in the free map. */
struct inode *
inode_create (block_sector_t sector, off_t length, enum inode_type type)
{

  // "DIRECTLY WRITE TO THE DISK AS WE WONT IMPLEMENT BUFFER"
  struct inode_disk *disk_inode = NULL;

  ASSERT (length >= 0);
  ASSERT (sizeof *disk_inode == BLOCK_SECTOR_SIZE);

  disk_inode = calloc (1, sizeof *disk_inode);
  if (disk_inode != NULL)
    {
      disk_inode->length = length;
      disk_inode->magic = INODE_MAGIC;
      disk_inode->type = type;
      // -1 means the block is unallocated
      int i;
      for (i = 0; i < SECTOR_CNT; i++) {
        disk_inode->sectors[i] = 0;
      }

      block_write (fs_device, sector, disk_inode);
    
      struct inode *mem_inode = inode_open(sector);
      if (mem_inode != NULL)
        {
          return mem_inode;
        }
    }
  return NULL;
}

/* Reads an inode from SECTOR
   and returns a `struct inode' that contains it.
   Returns a null pointer if memory allocation fails. */
struct inode *
inode_open (block_sector_t sector)
{
  // "DONT FORGET TO ACCESS OPEN_INODES LIST"
  // TODO CHECK ME
  struct list_elem *e;
  struct inode *inode;
  lock_acquire(&open_inodes_lock);
  /* Check whether this inode is already open. */
  for (e = list_begin (&open_inodes); e != list_end (&open_inodes);
       e = list_next (e)) 
    {
      inode = list_entry (e, struct inode, elem);
      if (inode->sector == sector) 
        {
          lock_release(&open_inodes_lock);
          inode_reopen (inode);
          return inode;
        }
    }

  /* Allocate memory. */
  inode = (struct inode*)malloc (sizeof(struct inode));
  if (inode == NULL) {
    lock_release(&open_inodes_lock);
    return NULL;
  }
  /* Initialize. */
  list_push_front (&open_inodes, &inode->elem);
  inode->sector = sector;
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->writer_cnt = 0;
  inode->removed = false;
  lock_init(&(inode->deny_write_lock));
  lock_init(&(inode->lock));
  cond_init(&(inode->no_writers_cond));
  lock_release(&open_inodes_lock);

  return inode;
}

/* Reopens and returns INODE. */
struct inode *
inode_reopen (struct inode *inode)
{
  if (inode != NULL)
    {
      lock_acquire (&open_inodes_lock);
      inode->open_cnt++;
      lock_release (&open_inodes_lock);
    }
  return inode;
}

/* Returns INODE's inode number. */
block_sector_t
inode_get_inumber (const struct inode *inode)
{
  return inode->sector;
}

/* Returns the type of INODE. */
enum inode_type
inode_get_type (const struct inode *inode)
{
  struct inode_disk from_disk;
  block_read (fs_device, inode->sector, (void*) &from_disk);
  return from_disk.type;
}

/* Closes INODE and writes it to disk.
   If this was the last reference to INODE, frees its memory.
   If INODE was also a removed inode, frees its blocks. */
void
inode_close (struct inode *inode) 
{
  // TODO CHECK ME
  // NOTE: from prof
  //  // check inode->open_cnt
  // deallocate inode if condition fulfills
  //NOTE:end
  /* Ignore null pointer. */
  if (inode == NULL)
    return;

  lock_acquire (&open_inodes_lock);

  /* Release resources if this was the last opener. */
  if (--inode->open_cnt == 0)
    {
      /* Remove from inode list and release lock. */
      list_remove (&inode->elem);
 
      /* Deallocate blocks if removed. */
      if (inode->removed) 
        {
          deallocate_inode (inode);
          free_map_release (inode->sector);
        }

      free (inode); 
    }
  lock_release (&open_inodes_lock);
}

/* Deallocates SECTOR and anything it points to recursively.
   LEVEL is 2 if SECTOR is doubly indirect,
   or 1 if SECTOR is indirect,
   or 0 if SECTOR is a data sector. */
static void
deallocate_recursive (block_sector_t sector, int level)
{
  if (sector == 0) {
    return;
  }
  if (level == 0) {
    free_map_release (sector);
    return;
  }
  else {
    block_sector_t blocks[PTRS_PER_SECTOR]; 
    block_read (fs_device, sector, blocks);
    // deallocate those blocks
    int i;
    for (i = 0; i < PTRS_PER_SECTOR; i++) {
      deallocate_recursive(blocks[i], level-1);
    }
    // deallocate this block?
    free_map_release (sector);
  }
}

/* Deallocates the blocks allocated for INODE. */
static void
deallocate_inode (const struct inode *inode)
{
  DEBUG_PRINT(("DEALLOCATE_INODE %p", inode));
  struct inode_disk from_disk;
  block_read (fs_device, inode->sector, &from_disk);
  int i;
  for (i = 0; i < SECTOR_CNT; i++) {
    if (i >= 0 && i < DIRECT_CNT) {
      deallocate_recursive (from_disk.sectors[i], 0);
    }
    else if (i >= DIRECT_CNT && i < DIRECT_CNT+INDIRECT_CNT) {
      deallocate_recursive (from_disk.sectors[i], 1);
    }
    else {
      deallocate_recursive (from_disk.sectors[i], 2);
    }
  }
  DEBUG_PRINT(("DONE DEALLOCATE_INODE %p", inode));
}

/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void
inode_remove (struct inode *inode) 
{
  ASSERT (inode != NULL);
  inode->removed = true;
}

/* Translates SECTOR_IDX into a sequence of block indexes in
   OFFSETS and sets *OFFSET_CNT to the number of offsets.
   offset_cnt can be 1 to 3 depending on whether sector_idx
   points to sectors within DIRECT, INDIRECT, or DBL_INDIRECT ranges.
*/
static void
calculate_indices (off_t sector_idx, size_t offsets[], size_t *offset_cnt)
{
  // TODO CHECK ME
  if (sector_idx < DIRECT_CNT) {
    offsets[0] = sector_idx;
    *offset_cnt = 1;
  }
  else if (sector_idx >= DIRECT_CNT && sector_idx < DIRECT_CNT+INDIRECT_CNT) {
    offsets[0] = DIRECT_CNT;
    offsets[1] = sector_idx % DIRECT_CNT;
    *offset_cnt = 2;
  }
  else {
    offsets[0] = DIRECT_CNT + INDIRECT_CNT;
    offsets[1] = (sector_idx / PTRS_PER_SECTOR);
    offsets[2] = sector_idx % PTRS_PER_SECTOR;
    *offset_cnt = 3;
  }
  //  DEBUG_PRINT(("IN CALC INDICES! sector_idx: %d, offset_cnt: %d, off0: %d, off1: %d, off2: %d\n", sector_idx, *offset_cnt, offsets[0], offsets[1], offsets[2]));
}

void
set_to_zeros (block_sector_t sector) {

  block_sector_t zeroed[PTRS_PER_SECTOR];

  memset(zeroed, 0, BLOCK_SECTOR_SIZE);
  block_write (fs_device, sector, zeroed);
}

/* Retrieves the data block for the given byte OFFSET in INODE,
   setting *DATA_BLOCK to the block and data_sector to the sector to write
   (for inode_write_at method).
   Returns true if successful, false on failure.
   If ALLOCATE is false (usually for inode read), then missing blocks
   will be successful with *DATA_BLOCK set to a null pointer.
   If ALLOCATE is true (for inode write), then missing blocks will be allocated.
   This method may be called in parallel */
static bool
get_data_block (struct inode *inode, off_t offset, bool allocate,
                void **data_block, block_sector_t *data_sector)
{

  // TODO CHECK ME
  size_t offsets[3];
  size_t offset_cnt;
  calculate_indices (offset/BLOCK_SECTOR_SIZE, offsets, &offset_cnt);

  block_sector_t current_sector = inode->sector;
  block_sector_t next_sector;
  block_sector_t* block_data = calloc (PTRS_PER_SECTOR, sizeof (block_sector_t));
  block_read (fs_device, inode->sector, (void*) block_data);

  for (unsigned i = 0; i < offset_cnt; i++) {
    //DEBUG_PRINT(("about to offset number %u\n", i));
    //DEBUG_PRINT(("current sector is %u\n", current_sector));
    next_sector = block_data[offsets[i]];
    //DEBUG_PRINT(("offset was %u, next_sector will be %u\n", i, next_sector));
    if (next_sector == 0) {
      // DEBUG_PRINT(("next sector allocating\n"));
      if (!allocate) {
        //DEBUG_PRINT(("jk not allocate mode :(\n"));
        *data_block = NULL;
        free(block_data);
        return true;
      } else {
        if (!free_map_allocate(&next_sector)) {
          //DEBUG_PRINT(("failed to allocate in freemap:(\n"));
          *data_block = NULL;
          free(block_data);
          return false;
        }
        // update block_data block
        //DEBUG_PRINT(("new next sector allocated! %u:(\n", next_sector));
        block_data[offsets[i]] = next_sector;
        // write it out to disk
        block_write(fs_device, current_sector, block_data);
        //DEBUG_PRINT(("wrote out edited current sector to disk:(\n"));
        set_to_zeros(next_sector);
        //DEBUG_PRINT(("zeroed next_sector:(\n"));
      }
    }
    block_read (fs_device, next_sector, (void*) block_data);
    //DEBUG_PRINT(("read in next_sector:(\n"));
    current_sector = next_sector;
  }
  //DEBUG_PRINT(("we DONE\n"));
  *data_block = block_data;
  *data_sector = current_sector;
  return true;
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached.
   Some modifications might be needed for this function template. */
off_t
inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset)
{
  uint8_t *buffer = buffer_;
  off_t bytes_read = 0;
  block_sector_t target_sector = 0; // not really useful for inode_read

  while (size > 0)
    {
      /* Sector to read, starting byte offset within sector, sector data. */
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;
      void *block; // may need to be allocated in get_data_block method,
                   // and don't forget to free it in the end

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually copy out of this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0 || !get_data_block (inode, offset, false, &block, &target_sector))
        break;

      if (block == NULL)
        memset (buffer + bytes_read, 0, chunk_size);
      else
        {
          memcpy (buffer + bytes_read, block + sector_ofs, chunk_size);
          free(block);
        }
      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_read += chunk_size;
    }

  return bytes_read;
}

/* Extends INODE to be at least LENGTH bytes long. */
static void
extend_file (struct inode *inode, off_t length)
{
  struct inode_disk inode_disk;
  block_read(fs_device, inode->sector, (void*) &inode_disk);
  if (length > inode_disk.length) {
    inode_disk.length = length;
    block_write(fs_device, inode->sector, (void*) &inode_disk);
  }
}


/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if an error occurs.
   Some modifications might be needed for this function template.*/
off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size,
                off_t offset, bool im_allowed_to_write_to_dirs_i_swear)
{
  const uint8_t *buffer = buffer_;
  off_t bytes_written = 0;
  block_sector_t target_sector = 0;
  /* Don't write if writes are denied. */
  lock_acquire (&inode->deny_write_lock);
  struct inode_disk idisk;
  block_read (fs_device, inode->sector, (void*) &idisk);
  if ((idisk.type == DIR && !im_allowed_to_write_to_dirs_i_swear)) {
    
    lock_release (&inode->deny_write_lock);
    return -1;
  }
  if (inode->deny_write_cnt)
    {
      lock_release (&inode->deny_write_lock);
      return 0;
    }
  inode->writer_cnt++;
  lock_release (&inode->deny_write_lock);

  while (size > 0)
    {
      /* Sector to write, starting byte offset within sector, sector data. */
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;
      void *block; // may need to be allocated in get_data_block method,
                   // and don't forget to free it in the end

      /* Bytes to max inode size, bytes left in sector, lesser of the two. */
      off_t inode_left = INODE_SPAN - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually write into this sector. */
      int chunk_size = size < min_left ? size : min_left;

      if (chunk_size <= 0 || !get_data_block (inode, offset, true, &block, &target_sector))
        break;
      ASSERT(block);
      memcpy (block + sector_ofs, buffer + bytes_written, chunk_size);
      block_write(fs_device, target_sector, block);
      free(block);
      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;
    }

  extend_file (inode, offset + size);

  lock_acquire (&inode->deny_write_lock);
  if (--inode->writer_cnt == 0)
    cond_signal (&inode->no_writers_cond, &inode->deny_write_lock);
  lock_release (&inode->deny_write_lock);

  return bytes_written;
}

/* Disables writes to INODE.
   May be called at most once per inode opener. */
void
inode_deny_write (struct inode *inode) 
{
  lock_acquire(&(inode->deny_write_lock));
  inode->deny_write_cnt++;
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
  lock_release(&(inode->deny_write_lock));
}

/* Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. */
void
inode_allow_write (struct inode *inode) 
{
  lock_acquire(&(inode->deny_write_lock));
  ASSERT (inode->deny_write_cnt > 0);
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
  inode->deny_write_cnt--;
  lock_release(&(inode->deny_write_lock));
}

/* Returns the length, in bytes, of INODE's data. */
off_t
inode_length (const struct inode *inode)
{
  struct inode_disk from_disk;
  block_read (fs_device, inode->sector, (void*) &from_disk);
  return from_disk.length;
}

/* Returns the number of openers. */
int
inode_open_cnt (const struct inode *inode)
{
  int open_cnt;

  lock_acquire (&open_inodes_lock);
  open_cnt = inode->open_cnt;
  lock_release (&open_inodes_lock);

  return open_cnt;
}

/* Locks INODE. */
void
inode_lock (struct inode *inode)
{
  lock_acquire (&inode->lock);
}

/* Releases INODE's lock. */
void
inode_unlock (struct inode *inode)
{
  lock_release (&inode->lock);
}
