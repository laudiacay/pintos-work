#include "filesys/filesys.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "filesys/directory.h"
#include "threads/thread.h"
#include "threads/malloc.h"

#ifndef DEBUG_BULLSHIT
#define DEBUG_BULLSHIT
#ifdef DEBUG
#include <stdio.h>
# define DEBUG_PRINT(x) printf("THREAD: %d ", thread_current()->tid); printf x 
#else
# define DEBUG_PRINT(x) do {} while (0)
#endif
#endif

/* Partition that contains the file system. */
struct block *fs_device;

static void do_format (void);

/* Initializes the file system module.
   If FORMAT is true, reformats the file system. */
void
filesys_init (bool format) 
{
  fs_device = block_get_role (BLOCK_FILESYS);
  if (fs_device == NULL)
    PANIC ("No file system device found, can't initialize file system.");

  inode_init ();
  free_map_init ();

  if (format) 
    do_format ();

  free_map_open ();
  lock_init(&file_lock);
}

/* Shuts down the file system module, writing any unwritten data
   to disk. */
void
filesys_done (void) 
{
  free_map_close ();
}


/* Extracts a file name part from *SRCP into PART,
   and updates *SRCP so that the next call will return the next
   file name part.
   Returns 1 if successful, 0 at end of string, -1 for a too-long
   file name part. */
static int
get_next_part (char part[NAME_MAX], const char **srcp)
{
  const char *src = *srcp;
  char *dst = part;

  /* Skip leading slashes.
     If it's all slashes, we're done. */
  while (*src == '/')
    src++;
  if (*src == '\0')
    return 0;

  /* Copy up to NAME_MAX character from SRC to DST.
     Add null terminator. */
  while (*src != '/' && *src != '\0')
    {
      if (dst < part + NAME_MAX)
        *dst++ = *src;
      else
        return -1;
      src++;
    }
  *dst = '\0';

  /* Advance source pointer. */
  *srcp = src;
  return 1;
}

/* Resolves relative or absolute file NAME.
   Returns true if successful, false on failure.
   Stores the directory corresponding to the name into *DIRP,
   and the file name part into BASE_NAME. */
static bool
resolve_name_to_entry (const char *name,
                       struct dir **dirp, char base_name[NAME_MAX + 1])
{
  DEBUG_PRINT(("resolving %s\n", name));
  if (name[0] == '/') {
    DEBUG_PRINT(("STARTING FROM ROOT DIR\n"));
    *dirp = dir_open_root();
  } else {
    DEBUG_PRINT(("STARTING FROM CWD: %d\n", thread_current()->wd));
    *dirp = dir_open(inode_open(thread_current() -> wd));
  }
  if (*dirp == NULL) {
    base_name[0] = '\0';
    return false;
  }
  bool first_loop = true;
  char next_part[NAME_MAX + 1];
  char last_part[NAME_MAX + 1];
  struct inode* put_inode_here;
  struct dir* tmp;
  int status;
  while(true) {
    status = get_next_part(next_part, &name);
    if (status == 0) {
      strlcpy(base_name, last_part, NAME_MAX+1);
      return true;
    }
    if (status == -1) {
        dir_close(*dirp);
        *dirp = NULL;
        base_name[0] = '\0';
        return false;
      }
    if (!first_loop) {
      if (dir_lookup(*dirp, last_part, &put_inode_here)) {
        // update dirp and keeeep walkin
        tmp = *dirp;
        *dirp = dir_open(put_inode_here);
        dir_close(tmp);
      } else {
        dir_close(*dirp);
        *dirp = NULL;
        base_name[0] = '\0';
        return false;
      }
    }
    first_loop = false;
    strlcpy(last_part, next_part, NAME_MAX+1);
  }
  return false;
}

/* Resolves relative or absolute file NAME to an inode.
   Returns an inode if successful, or a null pointer on failure.
   The caller is responsible for closing the returned inode. */
static struct inode *
resolve_name_to_inode (const char *name)
{
  DEBUG_PRINT(("RESOLVING NAME TO INODE!\n"));
  if (name[0] == '/' && strlen(name) == 1) {
    return inode_open(1);
  }
  struct dir* dirp;
  char base_name[NAME_MAX+1];
  if (!resolve_name_to_entry(name, &dirp, base_name)) {
    return NULL;
  }
  DEBUG_PRINT(("RESOLVED NAME TO ENTRY FOR %s\n", name));
  struct inode* inode = malloc(sizeof(inode));

  DEBUG_PRINT(("ABOUT TO DIR_LOOKUP THE INODE!\n"));
  if (!dir_lookup(dirp, base_name, &inode)) {
    DEBUG_PRINT(("FAILED TO DIR_LOOKUP THE INODE!\n"));
    dir_close(dirp);
    return NULL;
  }
  DEBUG_PRINT(("SUCCESSFULLY RESOLVED NAME TO INODE!\n"));
  dir_close(dirp);
  return inode;
}

/* Creates a file named NAME with the given INITIAL_SIZE.
   Returns true if successful, false otherwise.
   Fails if a file named NAME already exists,
   or if internal memory allocation fails. */
bool
filesys_create (const char *name, off_t initial_size, enum inode_type inode_type) 
{
  struct dir* dirp;
  char base_name[NAME_MAX + 1];
  if (!resolve_name_to_entry(name, &dirp, base_name)) return false;

  struct inode* inode;

  if (dir_lookup(dirp, base_name, &inode)) {
    // file exists, oh no!
    dir_close(dirp);
    inode_close(inode);
    return false;
  }

  block_sector_t place_for_inode;
  if (!free_map_allocate(&place_for_inode)) {
      dir_close(dirp);
      return false;
  }
  if (inode_type == FILE) {
    inode = file_create(place_for_inode, initial_size);
  } else{
    inode = dir_create(place_for_inode, inode_get_inumber(dir_get_inode(dirp)));
  }

  if (inode == NULL) {
    free_map_release(place_for_inode);
    dir_close(dirp);
    return false;
  }

  bool success = dir_add(dirp, base_name, place_for_inode);
  dir_close(dirp);
  inode_close(inode);
  return success;
}

/* Opens the inode with the given NAME.
   Returns the new file if successful or a null pointer
   otherwise.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
struct inode *
filesys_open (const char *name)
{
  return resolve_name_to_inode(name);
}

/* Deletes the file named NAME.
   Returns true if successful, false on failure.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
bool
filesys_remove (const char *name) 
{
  struct dir *dir = dir_open_root ();
  bool success = dir != NULL && dir_remove (dir, name);
  dir_close (dir); 

  return success;
}

/* Change current directory to NAME.
   Return true if successful, false on failure. */
bool
filesys_chdir (const char *name)
{
  return false;
}

/* Formats the file system. */
static void
do_format (void)
{
  struct inode *inode;
  printf ("Formatting file system...");

  /* Set up free map. */
  free_map_create ();

  /* Set up root directory. */
  inode = dir_create (ROOT_DIR_SECTOR, ROOT_DIR_SECTOR);

  if (inode == NULL)
    PANIC ("root directory creation failed");
  inode_close (inode);

  free_map_close ();

  printf ("done.\n");
}
