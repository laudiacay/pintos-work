#include "userprog/process.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "userprog/gdt.h"
#include "userprog/pagedir.h"
#include "userprog/tss.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/synch.h"
#include "userprog/syscall.h"
#include "vm/frame.h"
#include "vm/page.h"

#ifndef DEBUG_BULLSHIT
#define DEBUG_BULLSHIT
#ifdef DEBUG
#include <stdio.h>
# define DEBUG_PRINT(x) printf("THREAD: %p ", thread_current()); printf x 
#else
# define DEBUG_PRINT(x) do {} while (0)
#endif
#endif

static thread_func start_process NO_RETURN;
static bool load (const char *cmdline, void (**eip) (void), void **esp);


char* space = " ";

/* Starts a new thread running a user program loaded from
   FILENAME.  The new thread may be scheduled (and may even exit)
   before process_execute() returns.  Returns the new process's
   thread id, or TID_ERROR if the thread cannot be created.
*/
tid_t
process_execute (const char *file_name) 
{
  char fn_copy[60];
  char* page_of_filename;
  tid_t tid;

  /* Make a copy of FILE_NAME.
     Otherwise there's a race between the caller and load(). */
  page_of_filename = palloc_get_page (0);
  if (page_of_filename == NULL)
    return TID_ERROR;
  strlcpy (page_of_filename, file_name, PGSIZE);
  strlcpy (fn_copy, file_name, 60);

  /* Create a new thread to execute FILE_NAME. */
  //printf("in process_execute, file_name is %s\n", page_of_filename);
  //printf("in process_execute, stored in a page at %p\n", page_of_filename);
  //printf("in process_execute, fn_copy is %s\n", fn_copy);
  tid = thread_create (fn_copy, PRI_DEFAULT, start_process, page_of_filename);

  return tid;
}

/* A thread function that loads a user process and starts it
   running. */
static void
start_process (void *file_name_)
{
  char *file_name = file_name_;
  //printf("in start_process, file_name is %s\n", file_name);
  struct intr_frame if_;
  bool success;

  /* Initialize interrupt frame and load executable. */
  memset (&if_, 0, sizeof if_);
  if_.gs = if_.fs = if_.es = if_.ds = if_.ss = SEL_UDSEG;
  if_.cs = SEL_UCSEG;
  if_.eflags = FLAG_IF | FLAG_MBS;
  success = load (file_name, &if_.eip, &if_.esp);
  //printf("loaded filename! going to free page %p.\n", (void*) file_name_);
  palloc_free_page (file_name_);

  if (success) {
    thread_current()->wrapper->loaded = 1;
  }
  else {
    thread_current()->wrapper->loaded = -1;
  }

  //printf("calling semaup on thread %p.\n", thread_current);
  sema_up(&thread_current()->load_semaphore);
  
  // printf("loaded thread %s, success val %d\n", thread_current()->name, success);

  /* If load failed, quit. */
  
  if (!success) {
    thread_exit ();
  }

  /* Start the user process by simulating a return from an
     interrupt, implemented by intr_exit (in
     threads/intr-stubs.S).  Because intr_exit takes all of its
     arguments on the stack in the form of a `struct intr_frame',
     we just point the stack pointer (%esp) to our stack frame
     and jump to it. */
  asm volatile ("movl %0, %%esp; jmp intr_exit" : : "g" (&if_) : "memory");
  NOT_REACHED ();
}

/* Waits for thread TID to die and returns its exit status.  If
   it was terminated by the kernel (i.e. killed due to an
   exception), returns -1.  If TID is invalid or if it was not a
   child of the calling process, or if process_wait() has already
   been successfully called for the given TID, returns -1
   immediately, without waiting.

   This function will be implemented in problem 2-2.  For now, it
   does nothing. */
int
process_wait (tid_t child_tid) 
{
  struct thread *cur = thread_current();
  struct child_wrapper* child;
  struct list_elem *e;
  DEBUG_PRINT(("process_wait in thread %p\n", cur));
  for (e = list_begin (&cur->children); e != list_end (&cur->children);
       e = list_next (e))
  {
    child = list_entry (e, struct child_wrapper, child_elem);
    if (child -> tid == child_tid)
      break;
    else
      child = NULL;
  }
  if (child == NULL)
    return -1;
  if (child->waitedfor == 1)
    return -1;
  child->waitedfor = 1;

  // TODO:return -1 if killed by kernel?

  if (child->exit_flag == 1) {
    return child->exitstatus;
  }

  sema_down(&child->realchild->exit_semaphore);

  int ret = child -> exitstatus;
  // TODO: we should free the resources, idk why the following causes page fault
  // palloc_free_page(child);
  return ret;

}

/* Free the current process's resources. */
void
process_exit (void)
{
  struct thread *cur = thread_current ();
  uint32_t *pd;

  lock_acquire(&file_lock);
  if (cur->exe_file) {
    file_allow_write(cur->exe_file);
    file_close (cur->exe_file);
  }
  lock_release(&file_lock);
 
  cur->wrapper->exit_flag = 1;
  sema_up(&cur->exit_semaphore);
  /* Destroy the current process's page directory and switch back
     to the kernel-only page directory. */
  page_exit();
  pd = cur->pagedir;
  if (pd != NULL) 
    {

      
      /* Correct ordering here is crucial.  We must set
         cur->pagedir to NULL before switching page directories,
         so that a timer interrupt can't switch back to the
         process page directory.  We must activate the base page
         directory before destroying the process's page
         directory, or our active page directory will be one
         that's been freed (and cleared). */
      cur->pagedir = NULL;
      pagedir_activate (NULL);
      pagedir_destroy (pd);
      printf ("%s: exit(%d)\n", cur->name, cur->exitstatus);
    }
}

/* Sets up the CPU for running user code in the current
   thread.
   This function is called on every context switch. */
void
process_activate (void)
{
  struct thread *t = thread_current ();

  /* Activate thread's page tables. */
  pagedir_activate (t->pagedir);

  /* Set thread's kernel stack for use in processing
     interrupts. */
  tss_update ();
}

/* We load ELF binaries.  The following definitions are taken
   from the ELF specification, [ELF1], more-or-less verbatim.  */

/* ELF types.  See [ELF1] 1-2. */
typedef uint32_t Elf32_Word, Elf32_Addr, Elf32_Off;
typedef uint16_t Elf32_Half;

/* For use with ELF types in printf(). */
#define PE32Wx PRIx32   /* Print Elf32_Word in hexadecimal. */
#define PE32Ax PRIx32   /* Print Elf32_Addr in hexadecimal. */
#define PE32Ox PRIx32   /* Print Elf32_Off in hexadecimal. */
#define PE32Hx PRIx16   /* Print Elf32_Half in hexadecimal. */

/* Executable header.  See [ELF1] 1-4 to 1-8.
   This appears at the very beginning of an ELF binary. */
struct Elf32_Ehdr
  {
    unsigned char e_ident[16];
    Elf32_Half    e_type;
    Elf32_Half    e_machine;
    Elf32_Word    e_version;
    Elf32_Addr    e_entry;
    Elf32_Off     e_phoff;
    Elf32_Off     e_shoff;
    Elf32_Word    e_flags;
    Elf32_Half    e_ehsize;
    Elf32_Half    e_phentsize;
    Elf32_Half    e_phnum;
    Elf32_Half    e_shentsize;
    Elf32_Half    e_shnum;
    Elf32_Half    e_shstrndx;
  };

/* Program header.  See [ELF1] 2-2 to 2-4.
   There are e_phnum of these, starting at file offset e_phoff
   (see [ELF1] 1-6). */
struct Elf32_Phdr
  {
    Elf32_Word p_type;
    Elf32_Off  p_offset;
    Elf32_Addr p_vaddr;
    Elf32_Addr p_paddr;
    Elf32_Word p_filesz;
    Elf32_Word p_memsz;
    Elf32_Word p_flags;
    Elf32_Word p_align;
  };

/* Values for p_type.  See [ELF1] 2-3. */
#define PT_NULL    0            /* Ignore. */
#define PT_LOAD    1            /* Loadable segment. */
#define PT_DYNAMIC 2            /* Dynamic linking info. */
#define PT_INTERP  3            /* Name of dynamic loader. */
#define PT_NOTE    4            /* Auxiliary info. */
#define PT_SHLIB   5            /* Reserved. */
#define PT_PHDR    6            /* Program header table. */
#define PT_STACK   0x6474e551   /* Stack segment. */

/* Flags for p_flags.  See [ELF3] 2-3 and 2-4. */
#define PF_X 1          /* Executable. */
#define PF_W 2          /* Writable. */
#define PF_R 4          /* Readable. */

static bool setup_stack (void **esp, const char *cmdline);
static bool validate_segment (const struct Elf32_Phdr *, struct file *);
static bool load_segment (struct file *file, off_t ofs, uint8_t *upage,
                          uint32_t read_bytes, uint32_t zero_bytes,
                          bool writable);

/* Loads an ELF executable from FILE_NAME into the current thread.
   Stores the executable's entry point into *EIP
   and its initial stack pointer into *ESP.
   Returns true if successful, false otherwise. */
bool
load (const char *file_name, void (**eip) (void), void **esp) 
{
  struct thread *t = thread_current ();
  struct Elf32_Ehdr ehdr;
  struct file *file = NULL;
  off_t file_ofs;
  bool success = false;
  int i;

  /* Allocate and activate page directory. */
  t->pagedir = pagedir_create ();
  // need to destroy hashtable and pagedir if need be?
  if (!t->supp_pt_initialized) {
    hash_init (&t->supp_pt, page_hash, page_less, NULL);
    t->supp_pt_initialized = true;
  } 
  if (t->pagedir == NULL) 
    goto done;
  process_activate ();

  /* Open executable file. */
  char file_name_copy[60];
  strlcpy(file_name_copy, file_name, 60);
  char* save_ptr;
  char* file_name_real = strtok_r ( file_name_copy, " ", &save_ptr);
  t->exe_file = file = filesys_open (file_name_real);

  if (file == NULL) 
    {
      printf ("load: %s: open failed\n", file_name);
      //jankjankjankjankjank
      //t->exitstatus = -1;
      goto done; 
    }
  file_deny_write (file);

  /* Read and verify executable header. */
  if (file_read (file, &ehdr, sizeof ehdr) != sizeof ehdr
      || memcmp (ehdr.e_ident, "\177ELF\1\1\1", 7)
      || ehdr.e_type != 2
      || ehdr.e_machine != 3
      || ehdr.e_version != 1
      || ehdr.e_phentsize != sizeof (struct Elf32_Phdr)
      || ehdr.e_phnum > 1024) 
    {
      printf ("load: %s: error loading executable\n", file_name);
      goto done; 
    }

  /* Read program headers. */
  file_ofs = ehdr.e_phoff;
  for (i = 0; i < ehdr.e_phnum; i++) 
    {
      struct Elf32_Phdr phdr;

      if (file_ofs < 0 || file_ofs > file_length (file))
        goto done;
      file_seek (file, file_ofs);

      if (file_read (file, &phdr, sizeof phdr) != sizeof phdr)
        goto done;
      file_ofs += sizeof phdr;
      switch (phdr.p_type) 
        {
        case PT_NULL:
        case PT_NOTE:
        case PT_PHDR:
        case PT_STACK:
        default:
          /* Ignore this segment. */
          break;
        case PT_DYNAMIC:
        case PT_INTERP:
        case PT_SHLIB:
          goto done;
        case PT_LOAD:
          if (validate_segment (&phdr, file)) 
            {
              bool writable = (phdr.p_flags & PF_W) != 0;
              uint32_t file_page = phdr.p_offset & ~PGMASK;
              uint32_t mem_page = phdr.p_vaddr & ~PGMASK;
              uint32_t page_offset = phdr.p_vaddr & PGMASK;
              uint32_t read_bytes, zero_bytes;
              if (phdr.p_filesz > 0)
                {
                  /* Normal segment.
                     Read initial part from disk and zero the rest. */
                  read_bytes = page_offset + phdr.p_filesz;
                  zero_bytes = (ROUND_UP (page_offset + phdr.p_memsz, PGSIZE)
                                - read_bytes);
                }
              else 
                {
                  /* Entirely zero.
                     Don't read anything from disk. */
                  read_bytes = 0;
                  zero_bytes = ROUND_UP (page_offset + phdr.p_memsz, PGSIZE);
                }
              if (!load_segment (file, file_page, (void *) mem_page,
                                 read_bytes, zero_bytes, writable))
                goto done;
            }
          else
            goto done;
          break;
        }
    }
  //nprintf("about to setup stack\n");
  if (!setup_stack (esp, file_name))
    goto done;
  //printf("successfully set up stack\n");
  
  /* Start address. */
  *eip = (void (*) (void)) ehdr.e_entry;

  success = true;

 done:
  /* We arrive here whether the load is successful or not. */
  //printf("load success? %d\n", success);
  //printf("eip: %p , esp: %p\n", *eip, *esp);
  return success;
}
/* load() helpers. */

static bool install_page (void *upage, void *kpage, bool writable);

/* Checks whether PHDR describes a valid, loadable segment in
   FILE and returns true if so, false otherwise. */
static bool
validate_segment (const struct Elf32_Phdr *phdr, struct file *file) 
{
  /* p_offset and p_vaddr must have the same page offset. */
  if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK)) 
    return false; 

  /* p_offset must point within FILE. */
  if (phdr->p_offset > (Elf32_Off) file_length (file)) 
    return false;

  /* p_memsz must be at least as big as p_filesz. */
  if (phdr->p_memsz < phdr->p_filesz) 
    return false; 

  /* The segment must not be empty. */
  if (phdr->p_memsz == 0)
    return false;
  
  /* The virtual memory region must both start and end within the
     user address space range. */
  if (!is_user_vaddr ((void *) phdr->p_vaddr))
    return false;
  if (!is_user_vaddr ((void *) (phdr->p_vaddr + phdr->p_memsz)))
    return false;

  /* The region cannot "wrap around" across the kernel virtual
     address space. */
  if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
    return false;

  /* Disallow mapping page 0.
     Not only is it a bad idea to map page 0, but if we allowed
     it then user code that passed a null pointer to system calls
     could quite likely panic the kernel by way of null pointer
     assertions in memcpy(), etc. */
  if (phdr->p_vaddr < PGSIZE)
    return false;

  /* It's okay. */
  return true;
}

/* Loads a segment starting at offset OFS in FILE at address
   UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
   memory are initialized, as follows:

        - READ_BYTES bytes at UPAGE must be read from FILE
          starting at offset OFS.

        - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.

   The pages initialized by this function must be writable by the
   user process if WRITABLE is true, read-only otherwise.

   Return true if successful, false if a memory allocation error
   or disk read error occurs. */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
              uint32_t read_bytes, uint32_t zero_bytes, bool writable) 
{
  ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
  ASSERT (pg_ofs (upage) == 0);
  ASSERT (ofs % PGSIZE == 0);
  //printf("in load_segment at thread %s\n", thread_current() -> name);
  file_seek (file, ofs);
  while (read_bytes > 0 || zero_bytes > 0) 
    {
      /* Calculate how to fill this page.
         We will read PAGE_READ_BYTES bytes from FILE
         and zero the final PAGE_ZERO_BYTES bytes. */
      size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
      size_t page_zero_bytes = PGSIZE - page_read_bytes;

      // ***************************************
      // NEW: load segment modification
      // calls page allocate and set page to file info mapping
      // ***************************************
      //printf("about to allocate a page... for upage %p\n", upage);
      struct page *p = page_allocate (upage, !writable);
      //page_lock(upage, true, NULL);
      //nprintf("just allocated a page... for upage %p\n", upage);

      if (p == NULL)
        return false;
      if (page_read_bytes > 0)
        {
          p->file = file;
          p->file_offset = ofs;
          p->file_bytes = page_read_bytes; 
          p->page_current_loc = FROMFILE;
        }
      else {
        p->page_current_loc = TOBEZEROED;
        //printf("allocating zero page?? %p\n", upage);
      }
      read_bytes -= page_read_bytes;
      zero_bytes -= page_zero_bytes;
      ofs += page_read_bytes;
      upage += PGSIZE;
    }
  return true;
}

/* Create a minimal stack by mapping a zeroed page at the top of
   user virtual memory. */
static bool
setup_stack (void **esp, const char *cmdline) 
{
  bool success = false;

  struct page* p = page_allocate(PHYS_BASE - PGSIZE, false);
  if (!p) {
    //printf("could not allocate page...\n");
    return false;
  }
  p->page_current_loc= TOBEZEROED;
  struct frame* f = frame_alloc_and_lock (p);
  if (!f){
    //printf("could not lock a frame...\n");
    return false;
  } 
  success = pagedir_set_page(thread_current()->pagedir, p->uaddr, f->base, true);
  if (success)
    *esp = PHYS_BASE;
  else return success;
  if (sizeof(cmdline) > PGSIZE) {
    return false;
  }

  int MAX_ARGC = PGSIZE/sizeof(char*);
  char* token;
  char* saveptr;
  int argc = 0;
  int strlen_arg = 0;
  struct thread *cur = thread_current ();
  bool first = true;
  for (token = strtok_r((char*)cmdline, " ", &saveptr); token != NULL;
        token = strtok_r(NULL, " ", &saveptr)) {
    if (first) strlcpy(cur->name, token, 60);
    first = false;
    strlen_arg =(strlen(token) + 1) * sizeof(char);
    *esp -= strlen_arg;
    memcpy(*esp, token, strlen_arg);
    argc++;
    if (argc >= MAX_ARGC) {
      return false;
    }
  }
  char* argv_tracker = *esp;

  // word_align and push argv[argc]
  int word_align = (int)(*esp) % sizeof(char*);
  *esp -= word_align;
  memset(*esp, 0, word_align);
  
  *esp -= 4;
  memset(*esp, 0, 4);
  *esp -= 4;
  memset(*esp, 0, 4);

  void* top_of_argv = *esp - argc * sizeof(void*);
  //*esp = top_of_argv;
  //push cmd args to stack and copy address to argv
  for (int i = argc-1; i >= 0; i--) {
    memcpy(*esp, &argv_tracker, 4);
    //*esp += sizeof (char*);
    *esp -= sizeof(char*);
    argv_tracker += strlen(argv_tracker) + 1;
  }
  *esp = top_of_argv;
  //*esp -= sizeof (char*);
  
  // push argv
  top_of_argv += 4;
  *esp -= sizeof (char*);
  memcpy(*esp, &top_of_argv, 4);

  // push argc
  *esp -= sizeof (int);
  memcpy(*esp, &argc, sizeof(int));
  // push fake return address
  argc = 0;
  *esp -= sizeof(char*);
  memcpy(*esp, &argc, sizeof (void*));
  p->page_current_loc= INFRAME;
  frame_unlock(p->frame);
  //printf("aaa??? done setting up stack???\n");
  return success;
}

/* Adds a mapping from user virtual address UPAGE to kernel
   virtual address KPAGE to the page table.
   If WRITABLE is true, the user process may modify the page;
   otherwise, it is read-only.
   UPAGE must not already be mapped.
   KPAGE should probably be a page obtained from the user pool
   with palloc_get_page().
   Returns true on success, false if UPAGE is already mapped or
   if memory allocation fails. */
/*static bool
install_page (void *upage, void *kpage, bool writable)
{
  struct thread *t = thread_current ();

  /* Verify that there's not already a page at that virtual
     address, then map our page there. 
  return (pagedir_get_page (t->pagedir, upage) == NULL
          && pagedir_set_page (t->pagedir, upage, kpage, writable));
}
*/
