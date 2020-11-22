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
#include "vm/page.h"
#include <stdlib.h>

static void syscall_handler (struct intr_frame *);
static void copy_in (void *, const void *, size_t); 
static int sys_write (uint8_t*, void*);
static int sys_exit (uint8_t*, void*);
static void sys_halt (uint8_t*, void*);
static int sys_wait (uint8_t*, void*);
static pid_t sys_exec (uint8_t*, void*);
static bool sys_create (uint8_t*, void*);
static bool sys_remove (uint8_t*, void*);
static int sys_open (uint8_t*, void*);
static int sys_filesize (uint8_t*, void*);
static int sys_read (uint8_t*, void*);
static void sys_seek (uint8_t*, void*);
static unsigned sys_tell (uint8_t*, void*);
static void sys_close (uint8_t*, void*);

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
  if (ptr == NULL || !is_user_vaddr(ptr) || !pagedir_get_page(thread_current()->pagedir, ptr)) {
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

static char * copy_in_string (const char *);
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
  static int(*syscall)(uint8_t *, void*);
  copy_in (&call_nr, f->esp, sizeof call_nr); // See the copy_in function implementation below.
  DEBUG_PRINT(("made it to syscall handler?\n"));
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
    //printf("doing syscall with argument of f->esp + sizeof(call_nr): %s\n", f->esp + sizeof(call_nr));
    f->eax = syscall(f->esp + sizeof (call_nr), f->esp);
  }
  else f->eax = -1;
}

static void sys_halt (uint8_t* args_start, void* esp UNUSED) {
  shutdown_power_off();
}


static pid_t sys_exec (uint8_t* args_start, void* esp UNUSED) {
  
  char *cmd_line;
  DEBUG_PRINT(("in sys_exec***\n"));
  char* filename;
  copy_in (&filename, args_start, sizeof(char*));
  char* kernel_page = copy_in_string (filename);
  //check_str(cmd_line);
  //printf("****the kernel page is %p in kernel space\n", (void*)kernel_page);
  //DEBUG_PRINT(("copied in cmdline*** %s\n", kernel_page));
  
  //DEBUG_PRINT(("args_start is %s\n", args_start));
  pid_t process_id = process_execute((const char*)kernel_page);
  //DEBUG_PRINT(("called process_execute***\n"));
  palloc_free_page (kernel_page);
  if (process_id == TID_ERROR)
    return -1;

  // find this process in current thread's children list
  struct child_wrapper* child = thread_current()->cur_child;
  if (child == NULL)
    return -1;

  // wait for the child process to load
  if (child->loaded == 0) {
    DEBUG_PRINT(("about to call semadown on the kid\n"));
    sema_down(&child->realchild->load_semaphore);
    DEBUG_PRINT(("just called semadown on the kid\n"));
  }
  if (child->loaded == -1) {   // failed to load
    // child->exit_flag = 1;
    return -1;

  }

  return process_id;

}

static int sys_wait (uint8_t* args_start , void* esp UNUSED) {
  DEBUG_PRINT(("in sys_wait****\n"));

  tid_t child_id;
  copy_in (&child_id, args_start, sizeof(int));
  return process_wait(child_id);
}

static bool sys_create (uint8_t* args_start, void* esp UNUSED) {
  char* file_name_ptr;
  copy_in(&file_name_ptr, args_start, sizeof(char*));
  unsigned size;
  copy_in(&size, args_start+sizeof(char*), sizeof(int));

  char* file_name = copy_in_string (file_name_ptr);

  lock_acquire(&file_lock);
  bool status = filesys_create(file_name, size);
  DEBUG_PRINT(("status from filesys_create %d\n", status));
  lock_release(&file_lock);
  palloc_free_page(file_name);
  return status;
}

static bool sys_remove (uint8_t* args_start , void* esp UNUSED) {
  char *file_name;
  copy_in (&file_name, args_start, sizeof(char*));
  check_ptr(file_name);
  check_str(file_name);
  lock_acquire(&file_lock);
  bool status = filesys_remove(file_name);
  lock_release(&file_lock);
  return status;
}


static int sys_read (uint8_t* args_start, void* esp) {
  // if (args_start == NULL)
  //   return -1;
  int args[3];
  copy_in(&args, args_start, 3 * sizeof(int));
  int fd = args[0];
  const void* buffer = args[1];
  unsigned size = args[2];
  //check_buffer(buffer, size);
  struct file_in_thread* file;
  DEBUG_PRINT(("in sys_read******, goal buffer is %p?\n", buffer));
  int retval = 0;
  if (fd != 0) {
    lock_acquire(&file_lock);
    file = get_file(fd);
    if (!file) {
      lock_release(&file_lock);
      return -1;
    }
  }
  int ofs = 0;
  int total_bytes_read = 0;
  while (size > 0) {
    unsigned bytes_left_on_this_page = PGSIZE - pg_ofs(buffer);
    unsigned bytes_to_read_this_pass = bytes_left_on_this_page > size ? size : bytes_left_on_this_page;
    //printf("left on this page: %d, to read this pass: %d, total rem.: %d\n", bytes_left_on_this_page, bytes_to_read_this_pass, size);
    struct page *p = page_for_addr(buffer, esp);
    if (!p || !p->writable) {
      //printf("page_for addr said noooo\n");
      if (fd!=0) {
        lock_release(&file_lock);
      }
      //thread_current()->exitstatus = -1;
      thread_exit();
    }
    if (p->page_current_loc == INIT) p->page_current_loc = TOBEZEROED;
    if (bytes_to_read_this_pass > 0) {
      if (fd == 0) {
        page_lock(buffer, true, esp);
        for (int i = 0; i < bytes_to_read_this_pass; i++) {
          ((char*)buffer)[i] = input_getc();
        }
        page_unlock(buffer);
      } else {
        //printf("about to lock page!\n");
        page_lock(buffer, true, esp);
        //printf("locked page!\n");
        int bytes_read = file_read_at (file->fileptr, buffer, bytes_to_read_this_pass, ofs);
        //printf("about to unlock page!\n");
        page_unlock(buffer);
        //printf("bytes_read: %d\n", bytes_read);
        if (bytes_read < 0) {
          total_bytes_read = -1;
          break;
        }
        if (bytes_read < bytes_to_read_this_pass) {
          total_bytes_read += bytes_read;
          break;
        }
      }
    }
    total_bytes_read += bytes_to_read_this_pass;
    ofs += bytes_to_read_this_pass;
    buffer += bytes_to_read_this_pass;
    size -= bytes_to_read_this_pass;
  }
  if (fd != 0) {
    lock_release(&file_lock);
  }
  return total_bytes_read;
}

/* Write system call. */
static int sys_write (uint8_t* args_start, void* esp) {
  DEBUG_PRINT(("in sys_write***\n"));
  int args[3];
  copy_in(&args, args_start, 3 * sizeof(int));
  int fd = args[0];
  const void* buffer = args[1];
  unsigned size = args[2];
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

static int sys_exit(uint8_t* args_start, void* esp UNUSED) {
  int status_code;
  DEBUG_PRINT(("in sys_exit***\n"));

  copy_in (&status_code, args_start, sizeof(int));
  struct thread *cur = thread_current ();
  cur->exitstatus = status_code;
  cur->wrapper->exitstatus = status_code;
  thread_exit ();
  DEBUG_PRINT(("finished sys_exit***\n"));

  return status_code;
}

static int sys_open(uint8_t* args_start, void* esp UNUSED) {
  DEBUG_PRINT(("in sys_open***\n"));
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

static int sys_filesize(uint8_t* args_start, void* esp UNUSED) {
  int fd;
  copy_in (&fd, args_start, sizeof(int));
  lock_acquire(&file_lock);
  struct file_in_thread* file = get_file(fd);
  int filesize = file_length(file->fileptr);
  lock_release(&file_lock);
  return filesize;
}

static void sys_seek (uint8_t* args_start, void* esp UNUSED) {
  int args[2];
  copy_in(&args, args_start, 2 * sizeof(int));
  int fd = args[0];
  unsigned position = args[1];

  lock_acquire(&file_lock);
  struct file_in_thread* file = get_file(fd);
  if (file == NULL) {
    lock_release(&file_lock);
    return;
  }
  file_seek(file->fileptr, position);
  lock_release(&file_lock);
}

static unsigned sys_tell (uint8_t* args_start, void* esp UNUSED) {
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

static void sys_close (uint8_t* args_start, void* esp UNUSED) {
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
  int i = 0;
  while (i < size) {
    void* current_page = pg_round_down(usrc_ + i);

    if (!page_lock(current_page, false, NULL)) {
      DEBUG_PRINT(("could not pagelock us\n"));
      thread_exit();
    }
    for (;usrc_ + i < current_page + PGSIZE && i < size; i++) {
      //DEBUG_PRINT(("in for loop in copy_in\n"));
      
      //ASSERT(get_user(dst_+i, usrc_+1));
      //*((char*)(dst_ + i)) = *((char*)(usrc_ + i));
      if (!get_user((uint8_t *)(dst_ + i), (const uint8_t *)(usrc_ + i))) {
        page_unlock(current_page);
        thread_exit();
      }
    }
    page_unlock(current_page);
  }
}

/* Creates a copy of user string US in kernel memory and returns it as a
   page that must be **freed with palloc_free_page()**.  Truncates the string
   at PGSIZE bytes in size.  Call thread_exit() if any of the user accesses
   are invalid. */
static char *
copy_in_string (const char *us)
{
  char *ks;

  ks = palloc_get_page (0);
  //printf("*****pallocing a page for a string at %p.\n", (void*) ks);
  if (ks == NULL)
    thread_exit ();

  int i = 0;
  while (i < PGSIZE) {
    void* current_page = pg_round_down(us + i);

    if (!page_lock(current_page, false, NULL)) {
      DEBUG_PRINT(("could not pagelock us\n"));
      palloc_free_page(ks);
      thread_exit();
    }
    for (;us + i < current_page + PGSIZE && i < PGSIZE; i++) {
      //DEBUG_PRINT(("in loop in copy_in_string. us+i at %p, contents are %c\n", us + i, *(us+i)));
      //*(ks + i) = *(us + i);
      if (!get_user((uint8_t *)(ks + i), (const uint8_t *)(us + i))) {
        page_unlock(current_page);
        palloc_free_page(ks);
        thread_exit();
      }
      //printf("just copied into ks + i, %c, %d\n", *(ks+i), *(ks+i));
      if (*(ks + i) == '\x00') break;
    }
    page_unlock(current_page);
    //printf("ks + i, %c\n", *(ks+i));
    if (*(ks + i) == '\x00') break;
  }
  //printf("ks is %s\n", ks);
  return ks;

  // TODO: don't forget to call palloc_free_page(..) when you're done
  // with this page, before you return to user from syscall
  }
