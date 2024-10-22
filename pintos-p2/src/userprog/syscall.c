#include "userprog/syscall.h"
#include "threads/vaddr.h"
#include "filesys/file.h"
#include "threads/palloc.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "devices/shutdown.h"
#include "userprog/process.h"
#include "threads/synch.h"
#include <stdlib.h>

static void syscall_handler (struct intr_frame *);
static void copy_in (void *, const void *, size_t); 
static int sys_write (uint8_t*);
static int sys_exit (uint8_t*);
static void sys_halt (uint8_t*);
static int sys_wait (uint8_t*);
static pid_t sys_exec (uint8_t*);
static bool sys_create (uint8_t*);
static bool sys_remove (uint8_t*);
static int sys_open (uint8_t*);
static int sys_filesize (uint8_t*);
static int sys_read (uint8_t* args_start);
static void sys_seek (uint8_t* args_start);
static unsigned sys_tell (uint8_t* args_start);
static void sys_close (uint8_t* args_start);

void check_buffer(const void *buffer, unsigned size);
void check_ptr(const void *ptr);
void check_str (const void* str);
struct file_in_thread* get_file(int fd);

bool lock_initialized = false;

struct file_in_thread {
  struct file* fileptr;
  int fd;
  struct list_elem file_elem;
};

struct file_in_thread*
get_file(int fd) {
  struct thread *cur = thread_current();
  struct file_in_thread *target_file;
  struct list_elem *e;
  for (e = list_begin (&cur->file_list); e != list_end (&cur->file_list);
       e = list_next (e))
  {
    target_file = list_entry (e, struct file_in_thread, file_elem);
    if (target_file->fd == fd)
      break;
    else
      target_file = NULL;
  }
  return target_file;
}

void
check_str (const void* str) {
  void *ptr = pagedir_get_page(thread_current()->pagedir, str);
  if (ptr == NULL) {
    thread_current()->exitstatus = -1;
    thread_exit();
  }
}

void
check_ptr(const void *ptr) {
  if (ptr == NULL || !is_user_vaddr(ptr) || !pagedir_get_page(thread_current()->pagedir, ptr) ) {
    thread_current()->exitstatus = -1;
    thread_exit();
  }
}

void
check_buffer(const void *buffer, unsigned size) {
  int i;
  char *temp = (char*)buffer;
  for (i=0; i<size; i++) {
    check_ptr((const void*)temp);
    temp++;
  }
}

//static char * copy_in_string (const char *);
void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

/* System call handler. */
static void
syscall_handler (struct intr_frame *f)
{
  // printf("in syscall_handler, esp = %p\n", f->esp);
  if (!lock_initialized) {
    lock_init(&file_lock);
    lock_initialized = true;
  }

  unsigned call_nr = 0;
  static int(*syscall)(uint8_t *);
  copy_in (&call_nr, f->esp, sizeof call_nr); // See the copy_in function implementation below.
  //printf("made it?\n");
  switch (call_nr) {
  case SYS_WRITE: syscall = sys_write;
    break;
  case SYS_EXIT: syscall = sys_exit;
    break;
  case SYS_HALT: syscall = sys_halt;
    break;
  case SYS_WAIT: syscall = sys_wait;
    break;
  case SYS_EXEC: syscall = sys_exec;
    break;
  case SYS_CREATE: syscall = sys_create;
    break;
  case SYS_REMOVE: syscall = sys_remove;
    break;
  case SYS_OPEN: syscall = sys_open;
    break;
  case SYS_FILESIZE: syscall = sys_filesize;
    break;
  case SYS_READ: syscall = sys_read;
    break;
  case SYS_SEEK: syscall = sys_seek;
    break;
  case SYS_TELL: syscall = sys_tell;
    break;
  case SYS_CLOSE: syscall = sys_close;
    break;
  default:
    syscall = NULL;
    break;
  }
  if (syscall != NULL){
    //printf("about to do syscall\n");
    f->eax = syscall(f->esp + sizeof (call_nr));
  }
  else f->eax = -1;
}

static void sys_halt (uint8_t* args_start) {
  shutdown_power_off();
}


static pid_t sys_exec (uint8_t* args_start) {
  
  char *cmd_line;
  copy_in (&cmd_line, args_start, sizeof(char*));
  check_str(cmd_line);
  
  pid_t process_id = process_execute((const char*)cmd_line);
  if (process_id == TID_ERROR)
    return -1;

  // find this process in current thread's children list
  struct child_wrapper* child = thread_current()->cur_child;
  if (child == NULL)
    return -1;

  // wait for the child process to load
  if (child->loaded == 0) {
    sema_down(&child->realchild->load_semaphore);
  }
  if (child->loaded == -1) {   // failed to load
    // child->exit_flag = 1;
    return -1;
  }

  return process_id;

}

static int sys_wait (uint8_t* args_start) {
  tid_t child_id;
  copy_in (&child_id, args_start, sizeof(int));
  return process_wait(child_id);
}

static bool sys_create (uint8_t* args_start) {
  char *file_name;
  unsigned size;
  copy_in (&file_name, args_start, sizeof(char*));
  copy_in (&size, args_start + sizeof(char*), sizeof(int));

  check_ptr(file_name);
  check_str(file_name);

  lock_acquire(&file_lock);
  bool status = filesys_create(file_name, size);
  lock_release(&file_lock);
  return status;
}

static bool sys_remove (uint8_t* args_start) {
  char *file_name;
  copy_in (&file_name, args_start, sizeof(char*));
  check_ptr(file_name);
  check_str(file_name);
  lock_acquire(&file_lock);
  bool status = filesys_remove(file_name);
  lock_release(&file_lock);
  return status;
}

static int sys_read (uint8_t* args_start) {
  // if (args_start == NULL)
  //   return -1;
  int fd;
  const void* buffer;
  unsigned size;
  copy_in (&fd, args_start, sizeof(int));
  copy_in (&buffer, args_start + sizeof(int), sizeof(int));
  copy_in (&size, args_start + 2 *sizeof(int), sizeof(int));
  check_buffer(buffer, size);
  int retval = 0;
  if (fd == 0) {
    int i;
    uint8_t *store = (uint8_t *)buffer;
    for (i = 0;i < size; i++) {
      store[i] = input_getc();
    }
    return size;
  }
  else {
    lock_acquire(&file_lock);
    struct file_in_thread* file = get_file(fd);
    if (file == NULL) {
      lock_release(&file_lock);
      return -1;
    } 
    retval = file_read(file->fileptr, buffer, size);
    lock_release(&file_lock);
  }
  return retval;
}

/* Write system call. */
static int sys_write (uint8_t* args_start) {
  int fd;
  const void* buffer;
  unsigned size;
  copy_in (&fd, args_start, sizeof(int));
  copy_in (&buffer, args_start + sizeof(int), sizeof(int));
  copy_in (&size, args_start + 2 *sizeof(int), sizeof(int));
  check_buffer(buffer, size);

  int size_to_write = size;
  int retval = 0;
  // if the handle is stdout, need to do chunks at a time until sizetowrite is zero
  if (fd == 1) {
    int write_amt;
    write_amt = size_to_write > 300 ? 300 : size_to_write;
    while (size_to_write > 0) {
      putbuf (buffer + retval, write_amt);
      retval += write_amt;
      size_to_write -= write_amt;
    }
  }
  else {
    lock_acquire(&file_lock);
    struct file_in_thread* file = get_file(fd);
    if (file == NULL) {
      lock_release(&file_lock);
      return -1;
    } 
    retval = file_write(file->fileptr, buffer, size);
    lock_release(&file_lock);
  }
  return retval;
}

static int sys_exit(uint8_t* args_start) {
  int status_code;

  copy_in (&status_code, args_start, sizeof(int));
  struct thread *cur = thread_current ();
  cur->exitstatus = status_code;
  cur->wrapper->exitstatus = status_code;
  thread_exit ();

  return status_code;
}

static int sys_open(uint8_t* args_start) {
  char *file_name;
  copy_in (&file_name, args_start, sizeof(char*));

  check_ptr(file_name);
  check_str(file_name);

  lock_acquire(&file_lock);
  struct file *file = filesys_open ((const char *)file_name);
  if (file == NULL) {
    lock_release(&file_lock);
    return -1;
  }
  struct file_in_thread *new_file = malloc(sizeof(struct file_in_thread));
  new_file->fd = thread_current()->fd;
  new_file->fileptr = file;
  thread_current()->fd++;
  list_push_back(&thread_current()->file_list, &new_file->file_elem);
  lock_release(&file_lock);
  return new_file->fd;
}

static int sys_filesize(uint8_t* args_start) {
  int fd;
  copy_in (&fd, args_start, sizeof(int));
  lock_acquire(&file_lock);
  struct file_in_thread* file = get_file(fd);
  int filesize = file_length(file->fileptr);
  lock_release(&file_lock);
  return filesize;
}

static void sys_seek (uint8_t* args_start) {
  int fd;
  unsigned position;
  copy_in (&fd, args_start, sizeof(int));
  copy_in (&position, args_start+sizeof(int), sizeof(int));

  lock_acquire(&file_lock);
  struct file_in_thread* file = get_file(fd);
  if (file == NULL) {
    lock_release(&file_lock);
    return;
  }
  file_seek(file->fileptr, position);
  lock_release(&file_lock);
}

static unsigned sys_tell (uint8_t* args_start) {
  int fd;
  copy_in (&fd, args_start, sizeof(int));

  lock_acquire(&file_lock);
  struct file_in_thread* file = get_file(fd);
  if (file == NULL) {
    lock_release(&file_lock);
    return -1;
  }
  int pos = file_tell(file->fileptr);
  lock_release(&file_lock);
  return pos;
}

static void sys_close (uint8_t* args_start) {
  int fd;
  copy_in (&fd, args_start, sizeof(int));

  lock_acquire(&file_lock);
  struct file_in_thread* file = get_file(fd);
  if (file == NULL) {
    lock_release(&file_lock);
    return;
  }
  file_close(file->fileptr);
  list_remove(&file->file_elem);
  free(file);
  lock_release(&file_lock);
}


/* Copies a byte from user address USRC to kernel address DST.  USRC must
   be below PHYS_BASE.  Returns true if successful, false if a segfault
   occurred. Unlike the one posted on the p2 website, this one takes two
   arguments: dst, and usrc */

static inline bool
get_user (uint8_t *dst, const uint8_t *usrc)
{
  int eax;
  asm ("movl $1f, %%eax; movb %2, %%al; movb %%al, %0; 1:"
       : "=m" (*dst), "=&a" (eax) : "m" (*usrc));
  return eax != 0;

}


/* Writes BYTE to user address UDST.  UDST must be below PHYS_BASE.
   Returns true if successful, false if a segfault occurred. */

static inline bool
put_user (uint8_t *udst, uint8_t byte)
{
  int eax;
  asm ("movl $1f, %%eax; movb %b2, %0; 1:"
       : "=m" (*udst), "=&a" (eax) : "q" (byte));
  return eax != 0;
}



/* Copies SIZE bytes from user address USRC to kernel address DST.  Call
   thread_exit() if any of the user accesses are invalid. */ 

static void copy_in (void *dst_, const void *usrc_, size_t size) { 
  //printf("copying %d bytes to %p kernel from %p user\n", size, dst_, usrc_);
  uint8_t *dst = dst_; 
  const uint8_t *usrc = usrc_;
  //printf("user ptr out of bounds? %d\n", usrc_ >= ( (uint8_t *) PHYS_BASE));
  struct thread* t = thread_current();
  for (; size > 0; size--, dst++, usrc++)
    {
      check_ptr(usrc_);
      if (!get_user (dst, usrc)) {
        t->exitstatus = -1;
        thread_exit ();
    }

    }
}



/* Creates a copy of user string US in kernel memory and returns it as a
   page that must be **freed with palloc_free_page()**.  Truncates the string
   at PGSIZE bytes in size.  Call thread_exit() if any of the user accesses
   are invalid. */
/*static char *
copy_in_string (const char *us)
{
  char *ks;

  ks = palloc_get_page (0);
  if (ks == NULL)
    thread_exit ();

  for (uint i = 0; i < PGSIZE; i++) {
    if (!get_user((uint8_t *)(ks + i), (const uint8_t *)(us + i))) thread_exit();
    if (*(ks + i) == '\x00') break;
  }
  return ks;

  // TODO: don't forget to call palloc_free_page(..) when you're done
  // with this page, before you return to user from syscall
  }*/
