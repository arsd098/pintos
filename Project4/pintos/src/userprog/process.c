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

/* Added Headerfile */
/* For process hierarchy */
#include <list.h>                // For using list function
/* For file descriptor */
#include "filesys/filesys.h"     // For using Filesys function
/* For denying write to executable */
#include "threads/synch.h"              // For using lock function
#include "userprog/syscall.h"           // For using filesys_lock


static thread_func start_process NO_RETURN;
static bool load (const char *cmdline, void (**eip) (void), void **esp);

/* Starts a new thread running a user program loaded from
   FILENAME.  The new thread may be scheduled (and may even exit)
   before process_execute() returns.  Returns the new process's
   thread id, or TID_ERROR if the thread cannot be created. */
tid_t
process_execute (const char *file_name) 
{
  char *fn_copy;
  tid_t tid;
  /* Added variables */
  /* For argument passing */
  char *name, *save_ptr;  

  /* Make a copy of FILE_NAME.
     Otherwise there's a race between the caller and load(). */
  fn_copy = palloc_get_page (0);
  if (fn_copy == NULL)
    return TID_ERROR;
  strlcpy (fn_copy, file_name, PGSIZE);

  /* For argument passing */
  name = palloc_get_page (0);
  if (name == NULL)
    return TID_ERROR;
  /* Copy FILE_NAME to token */
  strlcpy (name, file_name, PGSIZE);
  /* Exectue token */
  strtok_r (name, " ", &save_ptr);

  /* Create a new thread to execute FILE_NAME. */
  tid = thread_create (name, PRI_DEFAULT, start_process, fn_copy);
  /* Free the name page */
  palloc_free_page (name);
  if (tid == TID_ERROR)
    palloc_free_page (fn_copy); 
  return tid;
} 

/* A thread function that loads a user process and starts it
   running. */
static void
start_process (void *file_name_)
{
  char *file_name = file_name_;
  struct intr_frame if_;
  bool success;

  /* Added variables */
  /* For argument passing */
  int argc = 0, len = strlen(file_name);
  char *token, *save_ptr;
  char **argv[len];  

  /* Execute token while searching the file_name */
  for(token = strtok_r (file_name, " ", &save_ptr); token != NULL;
      token = strtok_r (NULL, " ", &save_ptr))
  {
    /* Save token in argv array */
    argv[argc] = token;
    /* Increse the value of argc(= # of argument) */
    argc++;
  }

  /* Initialize interrupt frame and load executable. */
  memset (&if_, 0, sizeof if_);
  if_.gs = if_.fs = if_.es = if_.ds = if_.ss = SEL_UDSEG;
  if_.cs = SEL_UCSEG;
  if_.eflags = FLAG_IF | FLAG_MBS;
  success = load (file_name, &if_.eip, &if_.esp);
  
  /* For argument passing */
  /* For process hierarchy */

  /* If load success */
  if (success)
  {
    /* Run argument_stack */
    argument_stack(argv, argc, &if_.esp);
    /* Record that memory load is success */
    thread_current()->success_load = true;
  }
  /* Increse sema_load value for restart parent process */
  sema_up(&thread_current()->sema_load);
  
  /* Debugging Tool for vadlidation of result */
  // hex_dump(if_.esp, if_.esp, PHYS_BASE - if_.esp, true);

  /* If load failed, quit. */
  palloc_free_page (file_name);
  if (!success)
  { 
    /* Record that memory load is failed */
    thread_current()->success_load = false;
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
process_wait (tid_t child_tid UNUSED) 
{
  /* For process hierarchy */
  int status = -1;
  /* Get child process using child_tid */
  struct thread *child_thread = get_child_process(child_tid);
  /* If child process is not exist */
  if(child_thread == NULL)
    return status;
  /* Waiting for child process to exit */
  sema_down(&child_thread->sema_exit);
  /* Save exit status of child thread */
  status = child_thread->exit_status;
  /* Remove child process from discriptor */
  remove_child_process(child_thread);
  return status;
}

/* Free the current process's resources. */
void
process_exit (void)
{
  struct thread *cur = thread_current ();
  uint32_t *pd;

  /* For file descriptor */
  /* Close the all open files of process */
  int i;
  for(i = cur->fd_next; i>2; i--)
    process_close_file(i);
  /* Free the fd_table memory */
  palloc_free_page(cur->fd_table);

  /* Destroy the current process's page directory and switch back
     to the kernel-only page directory. */
  /* For denying write to executable */
  /* Close the run_file of thread */
  if (thread_current()->run_file != NULL )
    file_close(thread_current()->run_file);

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

static bool setup_stack (void **esp);
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
  if (t->pagedir == NULL) 
    goto done;
  process_activate ();

  /* For denying write to executable */
  /* Prevent simultaneous access to file */
  lock_acquire(&filesys_lock);

  /* Open executable file. */
  file = filesys_open (file_name);
  if (file == NULL) 
    {
      /* Release the filesys lock */
      lock_release(&filesys_lock);
      printf ("load: %s: open failed\n", file_name);
      goto done; 
    }
  /* Save the file in run_file of thread */
  thread_current()->run_file = file;
  /* Deny write about the file */
  file_deny_write(file);
  /* Release the filesys lock */
  lock_release(&filesys_lock);  

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

  /* Set up stack. */
  if (!setup_stack (esp))
    goto done;

  /* Start address. */
  *eip = (void (*) (void)) ehdr.e_entry;

  success = true; 
 done:
  /* We arrive here whether the load is successful or not. */

  //file_close(file);
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

  file_seek (file, ofs);
  while (read_bytes > 0 || zero_bytes > 0) 
    {
      /* Calculate how to fill this page.
         We will read PAGE_READ_BYTES bytes from FILE
         and zero the final PAGE_ZERO_BYTES bytes. */
      size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
      size_t page_zero_bytes = PGSIZE - page_read_bytes;

      /* Get a page of memory. */
      uint8_t *kpage = palloc_get_page (PAL_USER);
      if (kpage == NULL)
        return false;

      /* Load this page. */
      if (file_read (file, kpage, page_read_bytes) != (int) page_read_bytes)
        {
          palloc_free_page (kpage);
          return false; 
        }
      memset (kpage + page_read_bytes, 0, page_zero_bytes);

      /* Add the page to the process's address space. */
      if (!install_page (upage, kpage, writable)) 
        {
          palloc_free_page (kpage);
          return false; 
        }

      /* Advance. */
      read_bytes -= page_read_bytes;
      zero_bytes -= page_zero_bytes;
      upage += PGSIZE;
    }
  return true;
}

/* Create a minimal stack by mapping a zeroed page at the top of
   user virtual memory. */
static bool
setup_stack (void **esp) 
{
  uint8_t *kpage;
  bool success = false;

  kpage = palloc_get_page (PAL_USER | PAL_ZERO);
  if (kpage != NULL) 
    {
      success = install_page (((uint8_t *) PHYS_BASE) - PGSIZE, kpage, true);
      if (success)
        *esp = PHYS_BASE;
      else
        palloc_free_page (kpage);
    }
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
static bool
install_page (void *upage, void *kpage, bool writable)
{
  struct thread *t = thread_current ();

  /* Verify that there's not already a page at that virtual
     address, then map our page there. */
  return (pagedir_get_page (t->pagedir, upage) == NULL
          && pagedir_set_page (t->pagedir, upage, kpage, writable));
}

/* Added functions */
/* For argument passing */
void
argument_stack (char **parse, int count, void **esp)
{
  int i,j, sum_len = 0;
  unsigned int add[count];  /* For save the address of argument */
  /* Put argv[..] data in stack */
  for(i=count-1; i>-1; i--)
  { 
    for(j = strlen(parse[i]); j>-1; j--)
    { /* Decrese esp address by 1 */
      *esp = *esp-1;
      /* Put argument by one letter */
      **(char **)esp = parse[i][j];
    }
    /* Save the length of saved word */
    sum_len += strlen(parse[i])+1;
    /* Save the address of saved argument */
    add[i] = *(unsigned int*)esp;
  }
  /* Calculate the length of word_aling */
  sum_len = sum_len%4;
  /* Set the esp address to multiple of 4 */
  *esp -= sum_len != 0 ? 4-sum_len : 0; 
  /* Put NULL in stack */
  *esp -= 4;
  *(int*)(*esp) = 0;
  /* Put argv[..] address in stack */
  for(i=count-1; i>-1; i--)
  {
    *esp -= 4;
    *(char**)(*esp) = (char*)add[i];
  }
  /* Pun argv address in stack */
  *esp -= 4;
  *(char**)(*esp) = (char*)(*esp+4);
  /* Put argc(=count) in stack */
  *esp -= 4;
  *(int*)(*esp) = count;
  /* Put return address in stack */
  *esp -= 4;
  *(int*)(*esp) = 0; 
}

/* For process hierarchy */
/* Return the child process corresponding to pid */
struct thread
*get_child_process (int pid)
{
  struct list *child_list = &thread_current()->child_list;
  struct list_elem *e;

  /* Search for child list */
  for(e = list_begin(child_list); e != list_end(child_list);
      e = list_next(e))
  {
    struct thread *child_thread = list_entry(e,struct thread,child_elem);
    /* If find child process corresponding to pid */
    if(pid == child_thread->tid)
    {
      /* Return child process */
      return child_thread;  
    }
  }
  /* If the child process not exist, return NULL */
  return NULL;
}

/* Remove child process in child list */
void
remove_child_process (struct thread * cp)
{
  if(cp != NULL)
  {
    /* Remove child process in child list */
    list_remove(&cp->child_elem);
    /* Free memory of child process */
    palloc_free_page(cp);
  }
}

/* For file descriptor */
/* Add the file in file descriptor table */
int
process_add_file (struct file *f)
{
  struct file **fd_table = thread_current()->fd_table;
  int fd_next = thread_current()->fd_next;
  
  if(fd_table == NULL)
    return -1;

  /* Add file in file descriptor table*/
  fd_table[fd_next] = f;
  /* Update file descriptor */
  thread_current()->fd_next = fd_next+1;
  /* Return file descriptor value */
  return fd_next; 
}
/* Retrun file corresponding to fd */
struct file
*process_get_file (int fd)
{
  /* If fd is smaller than fd_next */
  if (fd < thread_current()->fd_next)
    return thread_current()->fd_table[fd];   
  else 
    return NULL;
}
/* Close the file corresponding to fd */
void
process_close_file (int fd)
{
  /* Find the file corresponding to fd */
  struct file *file = process_get_file(fd);
  if (file != NULL)
    {
      /* Close file */
      file_close(file);
      /* Initialize fd_table entry corresponding to file */
      thread_current()->fd_table[fd] = NULL;
    }
}

