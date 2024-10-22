#include "filesys/directory.h"
#include <stdio.h>
#include <string.h>
#include <list.h>
#include "filesys/free-map.h"
#include "filesys/filesys.h"
#include "filesys/inode.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#ifndef DEBUG_BULLSHIT
#define DEBUG_BULLSHIT
#ifdef DEBUG
#include <stdio.h>
# define DEBUG_PRINT(x) printf("THREAD: %d ", thread_current()->tid); printf x 
#else
# define DEBUG_PRINT(x) do {} while (0)
#endif
#endif
/* A directory. */
struct dir
  {
    struct inode *inode;                /* Backing store. */
    off_t pos;                          /* Current position. */
  };

/* A single directory entry. */
struct dir_entry
  {
    block_sector_t inode_sector;        /* Sector number of header. */
    char name[NAME_MAX + 1];            /* Null terminated file name. */
    bool in_use;                        /* In use or free? */
  };

/* Creates a directory in the given SECTOR.
   The directory's parent is in PARENT_SECTOR.
   Returns inode of created directory if successful,
   null pointer on faiilure.
   On failure, SECTOR is released in the free map. */
struct inode *
dir_create (block_sector_t sector, block_sector_t parent_sector)
{
  // ..
  struct inode *inode = inode_create(sector, 0, DIR);
  struct dir_entry d;

  d.inode_sector = sector;
  strlcpy(d.name, ".", NAME_MAX+1);
  d.in_use = true;

  inode_write_at(inode, (const void*) &d, sizeof(d), 0, true);

  d.inode_sector = parent_sector;
  strlcpy(d.name, "..", NAME_MAX+1);
  d.in_use = true;

  inode_write_at(inode, (const void*) &d, sizeof(d), sizeof(d), true);

  return inode;
}

/* Opens and returns the directory for the given INODE, of which
   it takes ownership.  Returns a null pointer on failure. */
struct dir *
dir_open (struct inode *inode)
{
  DEBUG_PRINT(("DIR OPEN: %d\n", inode_get_inumber(inode)));
  struct dir *dir = (struct dir*)calloc (1, sizeof(struct dir));
  if (inode != NULL && dir != NULL && inode_get_type (inode) == DIR)
    {
      dir->inode = inode;
      dir->pos = 0;
      DEBUG_PRINT(("FINISHED DIR OPEN: %d\n", inode_get_inumber(inode)));
      return dir;
    }
  else
    {
      inode_close (inode);
      free (dir);
      DEBUG_PRINT(("FAILED DIR OPEN\n"));
      return NULL;
    }
}

/* Opens the root directory and returns a directory for it.
   Return true if successful, false on failure. */
struct dir *
dir_open_root (void)
{
  struct inode* inode = inode_open(ROOT_DIR_SECTOR);
  if (!inode) return NULL;
  return dir_open (inode);
}

/* Opens and returns a new directory for the same inode as DIR.
   Returns a null pointer on failure. */
struct dir *
dir_reopen (struct dir *dir)
{
  DEBUG_PRINT(("DIR REOPEN: %d\n", inode_get_inumber(dir->inode)));
  return dir_open (inode_reopen (dir->inode));
  DEBUG_PRINT(("FINISHED DIR REOPEN: %d\n", inode_get_inumber(dir->inode)));
}

/* Destroys DIR and frees associated resources. */
void
dir_close (struct dir *dir)
{
  DEBUG_PRINT(("DIR CLOSE: %d\n", inode_get_inumber(dir->inode)));
  if (dir != NULL) {
      inode_close (dir->inode);
      free (dir);
    }
  DEBUG_PRINT(("FINISHED DIR CLOSE\n"));
}

/* Returns the inode encapsulated by DIR. */
struct inode *
dir_get_inode (struct dir *dir)
{
  return dir->inode;
}

/* Searches DIR for a file with the given NAME.
   If successful, returns the file's entry;
   otherwise, returns a null pointer. */
static bool
lookup (const struct dir *dir, const char *name,
        struct dir_entry *ep, off_t *ofsp)
{
  struct dir_entry e;
  size_t ofs;

  ASSERT (dir != NULL);
  ASSERT (name != NULL);

  for (ofs = 0; inode_read_at (dir->inode, &e, sizeof e, ofs) == sizeof e;
       ofs += sizeof e)
    if (e.in_use && !strcmp (name, e.name))
      {
        if (ep != NULL)
          *ep = e;
        if (ofsp != NULL)
          *ofsp = ofs;
        return true;
      }
  return false;
}

/* Searches DIR for a file with the given NAME
   and returns true if one exists, false otherwise.
   On success, sets *INODE to an inode for the file, otherwise to
   a null pointer.  The caller must close *INODE. */
bool
dir_lookup (const struct dir *dir, const char *name,
            struct inode **inode)
{
  struct dir_entry e;
  bool ok;

  ASSERT (dir != NULL);
  ASSERT (name != NULL);

  inode_lock (dir->inode);
  ok = lookup (dir, name, &e, NULL);
  inode_unlock (dir->inode);

  *inode = ok ? inode_open (e.inode_sector) : NULL;
  return *inode != NULL;
}

/* Adds a file named NAME to DIR, which must not already contain a
   file by that name.  The file's inode is in sector
   INODE_SECTOR.
   Returns true if successful, false on failure.
   Fails if NAME is invalid (i.e. too long) or a disk or memory
   error occurs. */
bool
dir_add (struct dir *dir, const char *name, block_sector_t inode_sector)
{
  struct dir_entry e;
  off_t ofs;
  bool success = false;

  ASSERT (dir != NULL);
  ASSERT (name != NULL);

  /* Check NAME for validity. */
  if (*name == '\0' || strchr (name, '/') || strlen (name) > NAME_MAX)
    return false;

  /* Check that NAME is not in use. */
  inode_lock (dir->inode);
  if (lookup (dir, name, NULL, NULL))
    goto done;

  /* Set OFS to offset of free slot.
     If there are no free slots, then it will be set to the
     current end-of-file.

     inode_read_at() will only return a short read at end of file.
     Otherwise, we'd need to verify that we didn't get a short
     read due to something intermittent such as low memory. */
  for (ofs = 0; inode_read_at (dir->inode, &e, sizeof e, ofs) == sizeof e;
       ofs += sizeof e)
    if (!e.in_use)
      break;

  /* Write slot. */
  e.in_use = true;
  strlcpy (e.name, name, sizeof e.name);
  e.inode_sector = inode_sector;
  success = inode_write_at (dir->inode, &e, sizeof e, ofs, true) == sizeof e;

 done:
  inode_unlock (dir->inode);
  return success;
}

/* Returns whether the DIR is empty. */
bool
dir_is_empty (const struct dir *dir)
{
  struct dir_entry e;
  off_t ofs;

  for (ofs = 2*sizeof e; inode_read_at (dir->inode, &e, sizeof e, ofs) == sizeof e;
       ofs += sizeof e)
  {
    if (e.in_use)
      return false;
  }
  return true;
}

/* Removes any entry for NAME in DIR.
   Returns true if successful, false on failure,
   which occurs only if there is no file with the given NAME. */
bool
dir_remove (struct dir *dir, const char *name)
{
  struct dir_entry e;
  struct inode *inode = NULL;
  bool success = false;
  off_t ofs;

  ASSERT (dir != NULL);
  ASSERT (name != NULL);

  if (!strcmp (name, ".") || !strcmp (name, ".."))
    return false;

  /* Find directory entry. */
  inode_lock (dir->inode);
  if (!lookup (dir, name, &e, &ofs))
    goto done;

  /* Open inode. */
  inode = inode_open (e.inode_sector);
  if (inode == NULL)
    goto done;

  /* Verify that it is not an in-use or non-empty directory. */
  if (inode_get_type(inode) == DIR) {
    struct dir *dir_to_remove = dir_open(inode);
    bool empty = dir_is_empty(dir_to_remove);
    int open_cnt = inode_get_opencnt (inode);
    if (!empty || open_cnt > 1 || inode_get_inumber(inode) == thread_current()->wd) {
      free(dir_to_remove);
      goto done;
    }
  }

  /* Erase directory entry. */
  e.in_use = false;
  if (inode_write_at (dir->inode, &e, sizeof e, ofs, true) != sizeof e)
    goto done;

  /* Remove inode. */
  inode_remove (inode);
  success = true;

 done:
  inode_unlock (dir->inode);
  inode_close (inode);
  return success;
}

/* Reads the next directory entry in DIR and stores the name in
   NAME.  Returns true if successful, false if the directory
   contains no more entries. */
bool
dir_readdir (struct dir *dir, char name[NAME_MAX + 1])
{
  struct dir_entry e;

  inode_lock (dir->inode);
  while (inode_read_at (dir->inode, &e, sizeof e, dir->pos) == sizeof e)
    {
      dir->pos += sizeof e;
      if (e.in_use && strcmp (e.name, ".") != 0 && strcmp (e.name, "..") != 0)
        {
          inode_unlock (dir->inode);
          strlcpy (name, e.name, NAME_MAX + 1);
          return true;
        }
    }
  inode_unlock (dir->inode);
  return false;
}
