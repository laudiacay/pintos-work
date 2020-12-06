#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44

#define DIRECT_CNT 122
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
    block_sector_t start;               /* First data sector. */
    off_t length;                       /* File size in bytes. */
    unsigned magic;                     /* Magic number. */
    //uint32_t unused[125];               /* Not used. */

    block_sector_t sectors[SECTOR_CNT];
    enum inode_type type;
  };

struct indirect_block_sector
{
  block_sector_t blocks[PTRS_PER_SECTOR];
};

bool data_block_allocate (block_sector_t *, off_t, void **, block_sector_t *, bool);
bool indirect_block_allocate (block_sector_t *, off_t, struct indirect_block_sector *,
                              bool);


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
    int deny_write_cnt;                 /* 0: writes ok, >0: deny writes. */
    struct inode_disk data;             /* Inode content. */
  };

/* Returns the block device sector that contains byte offset POS
   within INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */
static block_sector_t
byte_to_sector (const struct inode *inode, off_t pos) 
{
  ASSERT (inode != NULL);
  if (pos < inode->data.length)
    return inode->data.start + pos / BLOCK_SECTOR_SIZE;
  else
    return -1;
}

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
      disk_inode->sectors[i] = -1;
    }

    block_write (fs_device, sector, disk_inode);

    struct inode *mem_inode = malloc(sizeof(struct inode));
    if (mem_inode != NULL)
    {
      mem_inode->sector = sector;
      mem_inode->open_cnt = 0;
      mem_inode->removed = false;
      mem_inode->deny_write_cnt = 0;
      mem_inode->data = *disk_inode;
      free (disk_inode);
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
  struct list_elem *e;
  struct inode *inode;

  /* Check whether this inode is already open. */
  for (e = list_begin (&open_inodes); e != list_end (&open_inodes);
       e = list_next (e)) 
    {
      inode = list_entry (e, struct inode, elem);
      if (inode->sector == sector) 
        {
          inode_reopen (inode);
          return inode; 
        }
    }

  /* Allocate memory. */
  inode = malloc (sizeof *inode);
  if (inode == NULL)
    return NULL;

  /* Initialize. */
  //lock the list?
  list_push_front (&open_inodes, &inode->elem);
  inode->sector = sector;
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->removed = false;
  block_read (fs_device, inode->sector, &inode->data);

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
  return inode->data.type;
}

/* Closes INODE and writes it to disk.
   If this was the last reference to INODE, frees its memory.
   If INODE was also a removed inode, frees its blocks. */
void
inode_close (struct inode *inode) 
{

  /* Ignore null pointer. */
  if (inode == NULL)
    return;

  /* Release resources if this was the last opener. */
  if (--inode->open_cnt == 0)
    {
      /* Remove from inode list and release lock. */
      list_remove (&inode->elem);
 
      /* Deallocate blocks if removed. */
      if (inode->removed) 
      {
        free_map_release (inode->sector, 1);
        deallocate_inode (inode);
      }

      free (inode); 
    }
}

/* Deallocates SECTOR and anything it points to recursively.
   LEVEL is 2 if SECTOR is doubly indirect,
   or 1 if SECTOR is indirect,
   or 0 if SECTOR is a data sector. */
static void
deallocate_recursive (block_sector_t sector, int level)
{
  if (level == 0) {
    free_map_release (sector, 1);
    return;
  }
  else {
    // get direct/indirect blocks that should be deallocated
    block_sector_t blocks[PTRS_PER_SECTOR]; 
    block_read (fs_device, sector, blocks);
    // deallocate those blocks
    int i;
    for (i = 0; i < PTRS_PER_SECTOR; i++) {
      deallocate_recursive(blocks[i], level-1);
    }
    // deallocate this block?
    free_map_release (sector, 1);
  }
}

/* Deallocates the blocks allocated for INODE. */
static void
deallocate_inode (const struct inode *inode)
{
  int i;
  for (i = 0; i < SECTOR_CNT; i++) {
    if (i >= 0 && i < DIRECT_CNT) {
      deallocate_recursive (inode->data.sectors[i], 0);
    }
    else if (i >= DIRECT_CNT && i < DIRECT_CNT+INDIRECT_CNT) {
      deallocate_recursive (inode->data.sectors[i], 1);
    }
    else {
      deallocate_recursive (inode->data.sectors[i], 2);
    }
  }
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
  if (sector_idx < DIRECT_CNT) {
    offsets[0] = sector_idx;
    *offset_cnt = 1;
  }
  else if (sector_idx >= DIRECT_CNT && sector_idx < DIRECT_CNT+INDIRECT_CNT) {
    offsets[0] = DIRECT_CNT;
    offsets[1] = sector_idx - DIRECT_CNT;
    *offset_cnt = 2;
  }
  else {
    offsets[0] = DIRECT_CNT + INDIRECT_CNT;
    offsets[1] = (sector_idx-DIRECT_CNT-INDIRECT_CNT*PTRS_PER_SECTOR)/PTRS_PER_SECTOR;
    offsets[2] = sector_idx-(DIRECT_CNT+INDIRECT_CNT*PTRS_PER_SECTOR+PTRS_PER_SECTOR*offsets[1]);
    *offset_cnt = 3;
  }
}

// allocate a data block, return true if successful
bool
data_block_allocate (block_sector_t blocks[], off_t offset, 
                      void **data_block, block_sector_t *data_sector,
                      bool allocate)
{
  if (blocks[offset] != -1) {
    *data_sector = blocks[offset];
    block_read (fs_device, *data_sector, *data_block);
    return true;
  }
  if (allocate) {
    if (!free_map_allocate (1, data_sector))
      return false;
    blocks[offset] = *data_sector;
    block_read (fs_device, *data_sector, *data_block);
    return true;
  }
  *data_block = NULL;
  return true;
}

// allocate an indirect block and init blcok entries to -1
// return true if successful
bool
indirect_block_allocate (block_sector_t blocks[], off_t offset, 
                          struct indirect_block_sector *indir_block,
                          bool allocate)
{
  if (blocks[offset] != -1) {
    return true;
  }
  if (allocate) {
    block_sector_t *block_number;
    if (!free_map_allocate (1, block_number))
      return false;
    blocks[offset] = *block_number;
    indir_block = malloc(sizeof(*indir_block));
    int i;
    for (i = 0; i < PTRS_PER_SECTOR; i++) {
      indir_block->blocks[i] = -1;
    }
    ASSERT (sizeof *indir_block == BLOCK_SECTOR_SIZE);
    block_write (fs_device, *block_number, indir_block);
    return true;
  }
  return true;
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
  size_t offsets[3];
  size_t offset_cnt;
  calculate_indices (offset, offsets, &offset_cnt);
  /* direct block */
  if (offset_cnt == 1) {
    return data_block_allocate (inode->data.sectors, offsets[0], data_block, 
                          data_sector, allocate);
  }
  /* indirect block */
  else if (offset_cnt == 2) {
    struct indirect_block_sector *indir_block;
    if (indirect_block_allocate(inode->data.sectors, offsets[0], indir_block,
                              allocate))
    {
      return data_block_allocate (indir_block->blocks, offsets[1], data_block, 
                          data_sector, allocate);
    }
  }
  /* doubly indirect block */
  else if (offset_cnt == 3){
    struct indirect_block_sector *doubly_indir_block;
    if (indirect_block_allocate(inode->data.sectors, offsets[0],
                                        doubly_indir_block, allocate))
    {
      struct indirect_block_sector *indir_block;
      if (indirect_block_allocate(doubly_indir_block->blocks, offsets[1],
                                        indir_block, allocate))
      {
        return data_block_allocate (indir_block->blocks, offsets[2], data_block, 
                          data_sector, allocate);
      }
    }
  }

  return false;
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
        }

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_read += chunk_size;
    }

  return bytes_read;
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
off_t
inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset) 
{
  uint8_t *buffer = buffer_;
  off_t bytes_read = 0;
  uint8_t *bounce = NULL;

  while (size > 0) 
    {
      /* Disk sector to read, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually copy out of this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
        {
          /* Read full sector directly into caller's buffer. */
          block_read (fs_device, sector_idx, buffer + bytes_read);
        }
      else 
        {
          /* Read sector into bounce buffer, then partially copy
             into caller's buffer. */
          if (bounce == NULL) 
            {
              bounce = malloc (BLOCK_SECTOR_SIZE);
              if (bounce == NULL)
                break;
            }
          block_read (fs_device, sector_idx, bounce);
          memcpy (buffer + bytes_read, bounce + sector_ofs, chunk_size);
        }
      
      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_read += chunk_size;
    }
  free (bounce);

  return bytes_read;
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if end of file is reached or an error occurs.
   (Normally a write at end of file would extend the inode, but
   growth is not yet implemented.) */
off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size,
                off_t offset) 
{
  const uint8_t *buffer = buffer_;
  off_t bytes_written = 0;
  uint8_t *bounce = NULL;

  if (inode->deny_write_cnt)
    return 0;

  while (size > 0) 
    {
      /* Sector to write, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually write into this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
        {
          /* Write full sector directly to disk. */
          block_write (fs_device, sector_idx, buffer + bytes_written);
        }
      else 
        {
          /* We need a bounce buffer. */
          if (bounce == NULL) 
            {
              bounce = malloc (BLOCK_SECTOR_SIZE);
              if (bounce == NULL)
                break;
            }

          /* If the sector contains data before or after the chunk
             we're writing, then we need to read in the sector
             first.  Otherwise we start with a sector of all zeros. */
          if (sector_ofs > 0 || chunk_size < sector_left) 
            block_read (fs_device, sector_idx, bounce);
          else
            memset (bounce, 0, BLOCK_SECTOR_SIZE);
          memcpy (bounce + sector_ofs, buffer + bytes_written, chunk_size);
          block_write (fs_device, sector_idx, bounce);
        }

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;
    }
  free (bounce);

  return bytes_written;
}

/* Disables writes to INODE.
   May be called at most once per inode opener. */
void
inode_deny_write (struct inode *inode) 
{
  inode->deny_write_cnt++;
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
}

/* Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. */
void
inode_allow_write (struct inode *inode) 
{
  ASSERT (inode->deny_write_cnt > 0);
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
  inode->deny_write_cnt--;
}

/* Returns the length, in bytes, of INODE's data. */
off_t
inode_length (const struct inode *inode)
{
  return inode->data.length;
}
