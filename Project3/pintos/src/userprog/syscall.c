#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

/* Added Headerfile */
/* For system call handler */
#include <stdint.h>                     // For unint32_t
#include "devices/shutdown.h"           // For SYS_HALT
#include "filesys/filesys.h"            // For SYS_CRATE, REMOVE
/* For process hierarchy */
#include "userprog/process.h"           // For SYS_EXEC
/* For file descriptor */
#include "filesys/file.h"               // For using filesys function
#include "threads/synch.h"              // For using lock function
#include "devices/input.h"              // For using input_getc function
/* For virtual memory */
#include "vm/page.h"                    // For using vm_entry & function
/* For Memory mapped file */
#include "threads/vaddr.h"              // For using PGSIZE
#include "userprog/pagedir.h"
#include "threads/malloc.h"
#include "threads/palloc.h"
#include "lib/kernel/list.h"
#include <string.h>

static void syscall_handler (struct intr_frame *);

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
  lock_init (&filesys_lock);
}

static void
syscall_handler (struct intr_frame *f UNUSED) 
{
  void *sp = f->esp;
  int **arg[3]; 
  check_address(sp,f->esp);
  int num = *(int*)sp;

  switch(num)
  {
    case SYS_HALT : 
    halt();
    break;

    case SYS_EXIT : 
    get_argument(sp, arg, 1);
    exit((int)*arg[0]);
    break;

    case SYS_EXEC : 
    get_argument(sp, arg, 1);
    check_valid_string((void*)*arg[0],f->esp);
    f->eax = exec((const char*)*arg[0]);
    break;

    case SYS_WAIT : 
    get_argument(sp, arg, 1);
    f->eax = wait((tid_t)*arg[0]);
    break;

    case SYS_CREATE : 
    get_argument(sp,arg,2);
    check_valid_string((void*)*arg[0],f->esp);
    f->eax = create((const char*)*arg[0],(unsigned)*arg[1]);
    break;

    case SYS_REMOVE : 
    get_argument(sp,arg,1);
    check_valid_string((void*)*arg[0],f->esp);
    f->eax = remove((const char*)*arg[0]);
    break;

    case SYS_OPEN : 
    get_argument(sp, arg, 1);
    check_valid_string((void*)*arg[0],f->esp);
    f->eax = open((const char*)*arg[0]);
    break;

    case SYS_FILESIZE : 
    get_argument(sp, arg, 1); 
    f->eax = filesize((tid_t)*arg[0]);
    break;  

    case SYS_READ : 
    get_argument(sp,arg,3);
    check_valid_buffer((void*)*arg[1], (unsigned)*arg[2], f->esp, true);
    f->eax = read((int)*arg[0],(void*)*arg[1],(unsigned)*arg[2]);
    break;

    case SYS_WRITE :
    get_argument(sp,arg,3); 
    check_valid_buffer((void*)*arg[1], (unsigned)*arg[2], f->esp, false);
    f->eax = write((int)*arg[0],(void*)*arg[1],(unsigned)*arg[2]);
    break;

    case SYS_SEEK : 
    get_argument(sp,arg,2);
    seek((int)*arg[0],(unsigned)*arg[1]);
    break;

    case SYS_TELL : 
    get_argument(sp, arg, 1);
    f->eax = tell((int)*arg[0]);
    break;

    case SYS_CLOSE : 
    get_argument(sp, arg, 1);
    close((int)*arg[0]);
    break;

    case SYS_MMAP : 
    get_argument(sp, arg, 2);
    f->eax = mmap((int)*arg[0],(void*)*arg[1]);
    break;

    case SYS_MUNMAP : 
    get_argument(sp, arg, 1);
    munmap((int)*arg[0]);
    break;

  }
}

/* Added function */
/* For system call handler */
/* Check whether the address is user area or not */
struct vm_entry
*check_address (void *addr, void *esp)
{
  uint32_t check_add = addr;
  uint32_t Max_add = 0xc0000000;
  uint32_t Min_add = 0x08048000;
  /* If the address is not user area */
  if(!(check_add>Min_add && check_add<Max_add))
  {
    /* Exit proecss */
    exit(-1);
    return NULL;
  }
  else
    return find_vme(addr);
}

/* Save the argument value to kernel as many as count */
void
get_argument (void *esp, int *arg, int count)
{
  int i;
  /* Calculate address where address of argument saved */
  void *r_esp = esp + 4;
  /* Save the argumnet value as many as count */
  for(i = 0; i < count; i++)
  {
    struct vm_entry *vm_e = check_address(r_esp,esp);
    if(vm_e ==NULL)
      exit(-1);
    /* Save the value pointed to by address */
    arg[i] = (uint32_t*)r_esp;
    /* Recalculate the next address of argument saved */
    r_esp = r_esp + 4;
  }
}

/* Shutdown pintos */
void
halt (void)
{
  shutdown_power_off();
}

/* Exit current proecess */
void
exit (int status)
{
  struct thread *cur = thread_current();
  /* Save the exit_status as input status */
  cur->exit_status = status; 
  /* Print name and status of process that exited */
  printf("%s: exit(%d)\n", cur->name, status );
  /* Exited current process */
  thread_exit();
}

/* Create file */
bool
create (const char *file, unsigned initial_size)
{ 
  /* Check the file */
  if (file == NULL)
    exit(-1);
  else
  {
    /* Return value of filesys_create */
    return filesys_create(file, initial_size);
  }
}

/* Remove file */
bool
remove (const char *file)
{
  /* Return value of filesys_remove */ 
  return filesys_remove(file);
}

/* For process hierarchy */
/* Execute child process */
tid_t
exec (const char *cmd_line)
{
  /* Create child process */
  tid_t child_pid = process_execute(cmd_line);
  /* Get the child process */
  struct thread *child_thread = get_child_process(child_pid);
  /* Wait for child process load */
  sema_down(&child_thread->sema_load);
  /* Select return value */
  if(child_thread->success_load == true)
    return child_pid;
  else 
    return -1;
}
/* Wait until child process exit */
int
wait (tid_t tid)
{
  int status = process_wait(tid);
  return status;
}

/* For file descriptor */
/* Open the file */
int 
open (const char *file)
{
  /* Check the file */
  if(file == NULL )
    exit(-1);
  /* Open the file */
  struct file *open_file = filesys_open(file);
  if (open_file != NULL)
  {
    /* Return the fd of file*/
    return process_add_file(open_file);
  }
  return -1;
}
/* Return value of file_size */
int 
filesize (int fd)
{
  /* Find file */
  struct file *size_file = process_get_file(fd);
  if(size_file != NULL)
  {
    /* Return the filesize of file */
    return file_length(size_file);
  }
  return -1;
}
/* Read data of opened file */
int 
read (int fd, void *buffer, unsigned size)
{
  int bytes = 0;
  char *read_buffer = (char *)buffer;
  /* Prevent simultaneous access to file */
  lock_acquire(&filesys_lock);
  /* If fd is 0 */
  if(fd == 0)
    {
      /* Save the Keyborad input to buffer */
      for(bytes=0; bytes <= size; bytes++)
        read_buffer[bytes] = input_getc();
      /* Add \0 value for data classification */
      read_buffer[bytes] = '\0';
    }
  /* If fd is not 0 */
  else
  {
    /* Find file corresponding to fd */
    struct file *read_file = process_get_file(fd);
    if(read_file != NULL)
    {
      /* Save the size of data to bytes */
      bytes = file_read(read_file, buffer, size);   
    }
  }  
  /* Release the filesys lock */
  lock_release(&filesys_lock);
  return bytes;
}
/* Write data of opened file */
int
write (int fd, void *buffer, unsigned size)
{
  
  int bytes = 0 ;
  /* Prevent simultaneous access to file */
  lock_acquire(&filesys_lock);
  /* If fd is 1 */
  if(fd == 1)
    {
      /* Print data stored in the buffer */
      putbuf((const char*)buffer, size);
      /* Save the size to bytes variable */
      bytes = size;
    }
  /* If fd is 1 */
  else
    {
      /* Find file corresponding to fd */
      struct file *write_file = process_get_file(fd);
      if(write_file != NULL)
      {
        /* Write & Save the size of data to bytes */
        bytes = file_write(write_file, buffer, size);
      }      
    }
  /* Release the filesys lock */
  lock_release(&filesys_lock);
  return bytes;
}
/* Move the file offset */
void 
seek (int fd, unsigned position)
{
  /* Find file corresponding to fd */
  struct file *seek_file = process_get_file(fd);
  if(seek_file != NULL)
  {
    /* Move the offset by position */
    file_seek(seek_file, position);
  }
}
/* Tell the offset position of opened file */
unsigned 
tell (int fd)
{
  /* Find file corresponding to fd */
  struct file *tell_file = process_get_file(fd);
  if(tell_file != NULL)
    /* Return offset of file */
    return file_tell(tell_file);
  else 
    return -1;
}
/* Close the opened file */
void
close (int fd)
{
  /* Find file corresponding to fd */
  struct file *close_file = process_get_file(fd);
  if(close_file != NULL)
  {
    /* Close the file */
    file_close(close_file);
    /* Initialize fd_table entry corresponding to file */
    thread_current()->fd_table[fd] = NULL;
  }
}

/* For virtual memory */
/* Check the buffer validity */
void 
check_valid_buffer (void *buffer, unsigned size, void *esp, bool to_write)
{
  struct vm_entry *vm_e;
  int i;
  char *char_buffer = (char*)buffer;

  /* Check the validity up to buffer+size */
  for(i=0; i<size; i++)
  {
    vm_e = check_address((void*)char_buffer, esp);
    if(vm_e != NULL)
    {
      if(to_write == true)
      {
        if(vm_e->writable != true)
          exit(-1);
      }
    char_buffer++;
    }
    else 
      exit(-1);    
  }
}

/* Check the string validity */
void
check_valid_string (const void *str, void *esp)
{
  char *char_str = (char*)str;
  /* Check the vm_entry presence for str */
  if(check_address((void*)char_str,esp) == NULL)
    exit(-1);
  /* Check the validity for char_type str */
  while(*char_str != 0)
  {
    if(check_address(char_str,esp) == NULL)
      exit(-1);
    char_str++;
  }  
}

/* For Memory mapped file */
/* Load & Map file data into memory */
int
mmap (int fd, void *addr)
{
  struct thread *cur = thread_current();
  struct file *file;
  struct mmap_file *mmap_f;
  struct vm_entry *vm_e;
  
  void *v_addr = (void*) addr;
  int ofs = 0;
  
  /* check addr is valid */
  if(addr == NULL || (int)v_addr % PGSIZE != 0)
    return -1;
  /* Open the file */
  file = file_reopen(process_get_file(fd));
  if (file == NULL)
    return -1;
  uint32_t len = file_length(file); 
  /* Allocate mmap_file */
  mmap_f = malloc(sizeof(struct mmap_file));
  if (mmap_f == NULL)
    return -1;
  /* Set the mapid */
  cur->mapid += 1;
  mmap_f->mapid = cur->mapid;
  /* Initialize vme_list of mmap_file */  
  list_init(&mmap_f->vme_list);
  
  while(len > 0)
  {
      size_t page_read_bytes = len < PGSIZE ? len : PGSIZE;
      size_t page_zero_bytes = PGSIZE - page_read_bytes;
      
      /* For virtual memory */
      /* Allocate vm_entry */
      vm_e = malloc(sizeof(struct vm_entry));
      if (vm_e == NULL) 
        return -1;

      /* Initialize vm_entry field */
      vm_e->type = VM_FILE;
      vm_e->vaddr = v_addr;
      vm_e->writable = true;
      vm_e->is_loaded = false;
      vm_e->file = file;
      vm_e->offset = ofs;
      vm_e->read_bytes = page_read_bytes;
      vm_e->zero_bytes = page_zero_bytes;

      /* Insert vm_entry into hash_table */
      if(insert_vme(&thread_current()->vm, vm_e) != true)
        return -1;
      /* Insert vm_entry into mmap_file */
      list_push_back(&mmap_f->vme_list, &vm_e->mmap_elem);

      /* Advance. */
      len -= page_read_bytes;
      ofs += page_read_bytes;
      v_addr += PGSIZE;
    }
  /* Insert mmap_file into mmap_list of thread */
  list_push_back(&cur->mmap_list, &mmap_f->elem);
  return mmap_f->mapid;
}

/* Remove file maaping */
void
munmap (int mapping)
{
  struct thread *cur = thread_current();
  struct list_elem *e;
  struct mmap_file *mmap_f;

  /* Searches the mmap_list of thread */
  for(e = list_begin(&cur->mmap_list); e != list_end(&cur->mmap_list);
      e = list_next(e))
  {
    /* Get mmap_file */
    mmap_f = list_entry(e, struct mmap_file, elem);
    if(mmap_f->mapid == mapping || mapping == EXIT_PROCESS)
    {
      do_munmap(mmap_f);
      /* Close the file */
      file_close(mmap_f->file);
      /* Remove element from mmap_list */
      struct list_elem *a = list_prev(e);
      list_remove(e);
      e = a;
      /* Free the mmap_file */
      free(mmap_f);
      if(mapping == EXIT_PROCESS)
        continue;
      else break;
    }
  }
}

/* Remove vm_entry about mmap_file */
void
do_munmap(struct mmap_file *mmap_file)
{
  struct thread *cur = thread_current();
  struct list_elem *e;
  struct vm_entry *vm_e;

  void *p_addr;

  /* Searches the vm_entry of mmap_file */
  for(e = list_begin(&mmap_file->vme_list); e != list_end(&mmap_file->vme_list);
      e = list_next(e))
  {
    /* Get vm_entry */
    vm_e = list_entry(e, struct vm_entry, mmap_elem);
    /* If vm_entry is loaded */
    if(vm_e->is_loaded == true)
    {  
      /* Get physical address */
      p_addr = pagedir_get_page(cur->pagedir, vm_e->vaddr);
      if(pagedir_is_dirty(cur->pagedir, vm_e->vaddr) == true)
        file_write_at(vm_e->file, vm_e->vaddr, vm_e->read_bytes, vm_e->offset);
      /* Clear page */
      pagedir_clear_page(cur->pagedir, vm_e->vaddr);
      /* Free physical memory */
      palloc_free_page(p_addr);
    }
    /* Remove element from vme_list */
    struct list_elem *a = list_prev(e);
    list_remove(e);
    e = a;
    /* Free & delete vm_entry */
    delete_vme(&cur->vm, vm_e);
  }
}


