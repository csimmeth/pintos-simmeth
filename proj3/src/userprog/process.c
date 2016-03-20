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
#include "threads/malloc.h"
#include "devices/input.h"

static thread_func start_process NO_RETURN;
static bool load (const char *cmdline, void (**eip) (void), void **esp);
static struct lock file_lock;
static struct file * get_file(int fd);

/* Starts a new thread running a user program loaded from
   FILENAME.  The new thread may be scheduled (and may even exit)
   before process_execute() returns.  Returns the new process's
   thread id, or TID_ERROR if the thread cannot be created. */
void
process_init(void)
{
  lock_init(&file_lock);
}

tid_t
process_execute (const char *file_name) 
{
  lock_acquire(&file_lock);
  
  char *fn_copy;
  tid_t tid;

  /* Make a copy of FILE_NAME.
     Otherwise there's a race between the caller and load(). */
  fn_copy = palloc_get_page (0);
  if (fn_copy == NULL)
    return TID_ERROR;
  strlcpy (fn_copy, file_name, PGSIZE);

  /* Create the child struct and initialize elements */
  struct process_info * child_info = malloc(sizeof(struct process_info));
  child_info->exit_status = -1;
  sema_init(&child_info->sema_start,0);
  sema_init(&child_info->sema_finish,0);

  /* Add the child to the current thread's list of children */
  list_push_back(&thread_current()->children,&child_info->elem);

  /* Create a new thread to execute FILE_NAME. */
  tid = thread_create (file_name, PRI_DEFAULT, start_process, fn_copy);
  if (tid == TID_ERROR)
  {
    palloc_free_page (fn_copy); 
  }

  lock_release(&file_lock);

  sema_down(&child_info->sema_start);

  if(!child_info->success)
	tid = TID_ERROR;

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

  /* Initialize interrupt frame and load executable. */
  memset (&if_, 0, sizeof if_);
  if_.gs = if_.fs = if_.es = if_.ds = if_.ss = SEL_UDSEG;
  if_.cs = SEL_UCSEG;
  if_.eflags = FLAG_IF | FLAG_MBS;
  success = load (file_name, &if_.eip, &if_.esp);

  palloc_free_page (file_name);

  thread_current()->p_info->success = success;


  /* If load failed, quit. */
  if (!success) 
  {
    thread_exit ();
  }

  int fd = process_file_open(thread_current()->process_name);

  lock_acquire(&file_lock);
  file_deny_write(get_file(fd));
  lock_release(&file_lock);

  /* Because the parent blocks, p_info will always be available */
  sema_up(&thread_current()->p_info->sema_start);


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
  struct thread * t = thread_current();
  struct list * child_list = &t->children;
  struct process_info * p_info = NULL;

  /* Iterate through the list of children until we find a match */
  struct list_elem *e;
  for (e = list_begin (child_list); e != list_end(child_list);
	   e = list_next(e))
  {
	struct process_info *p = list_entry(e,struct process_info,elem);
	if(p->tid == child_tid)
	{
      p_info = p;
	  break;
	}
  }

  // If no match, return -1
  if(p_info == NULL)
  {
	return -1;
  }

  // Wait for the thread to exit
  sema_down(&p_info->sema_finish);
  
  int status = p_info->exit_status;

  // remove e from list and deallocate memory
  // Don't need to modify the child because it has exited
  list_remove(e);
  free(p_info);

  return status;
}

/* Free the current process's resources. */
void
process_exit (void)
{
  struct thread *cur = thread_current ();
  uint32_t *pd;

  if(cur->is_process){
    printf("%s: exit(%d)\n",cur->process_name, cur->exit_status);
  }

  /* Remove all dynamic memory for child process info */
  /* This happen even if the thread is not a process 
   * since the list will just be empty */
  struct list *c = &cur->children;
  struct list_elem *e = list_begin (c);
  while(e != list_end(c))
  {
    struct process_info * child_info = list_entry(e, struct process_info,
		 										    elem);
    e = list_next(e);	

	/* Mark that the parent has quit for each child
	 * that has not already exited*/
	if(child_info->child != NULL)
	  child_info->child->p_info = NULL;

    free(child_info);
  }

  /* Close all open files */
  struct list *files = &cur->files;
  e = list_begin(files);
  while(e != list_end(files))
  {
    struct file_info * fi = list_entry(e, struct file_info, elem);
	e = list_next(e);

	process_close(fi->fd);
  }

  /* Modify p_info to show that this child has quit
   * if the parent has not already quit */
  if(cur->p_info != NULL){

	/* Mark that this thread is no longer active */
	cur->p_info->child = NULL;
	
	/* If the parent is waiting, release it */
	sema_up(&cur->p_info->sema_finish);

	/* Check if this thread ever got started */
	if(!cur->p_info->success)
	{
	  /* If it did not, release the creator */
	  sema_up(&cur->p_info->sema_start);
	}

  }


  /* Destroy the current process's page directory and switch back
     to the kernel-only page directory. */
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

void
syscall_process_exit(int status){
  struct thread * t = thread_current();

  /* Store the exit status for the parent */
  if(t->p_info != NULL)
  {
    t->p_info->exit_status = status;	
  }

  /* Store the exit status for self - needed for printout */
  t->exit_status = status;

  thread_exit();

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

static struct file_info *
get_file_info(int fd)
{
  struct list * files = &thread_current()->files;
  struct list_elem *e;
  for(e = list_begin(files); e != list_end(files); e = list_next(e))
  {
	struct file_info * fi = list_entry(e, struct file_info,elem);
	if(fi->fd == fd)
	  return fi;
  }
  return NULL;
}

static struct file *
get_file(int fd)
{
  struct file_info * fi = get_file_info(fd);

  if(fi)
	return fi->file;

  return NULL;
}

/* Create a file */
bool
process_file_create(char* name, uint32_t size)
{
  lock_acquire(&file_lock);
  bool success = filesys_create(name,size);
  lock_release(&file_lock);
  return success;
}

bool 
process_file_remove(char *name)
{
  lock_acquire(&file_lock);
  bool success = filesys_remove(name);
  lock_release(&file_lock);
  return success;
}

int 
process_file_open(char *name)
{
  lock_acquire(&file_lock);
  struct file * file = filesys_open(name);
  int fd = -1;

  if(file)
  {
	/* Get a new fd from the current thread*/
	fd = thread_current()->file_counter++;

    /* Create a struct to associate this file with a file number */
    struct file_info * fi = malloc(sizeof(struct file_info));   
    fi->file = file;
    fi->fd = fd; 

    /* Add the struct to the current thread's list of open files */
    list_push_back(&thread_current()->files,&fi->elem);
  }

  lock_release(&file_lock);

  return fd;
}

int
process_filesize(int fd)
{

  struct file * file = get_file(fd);
  if(!file)
	return -1;

  lock_acquire(&file_lock);

  int length =  file_length(file);

  lock_release(&file_lock);
  
  return length;
}

int 
process_read(int fd, void *buffer, uint32_t size)
{

  int bytes_read = -1;


  if(fd == 0)
  {
    uint8_t input = input_getc();
	*(char*)buffer = input;
	bytes_read = 1;
  }
  else
  {
    struct file * file = get_file(fd);
    /* Check that the file is vaild */
    if(!file)
      return -1;

	 
    lock_acquire(&file_lock);
    bytes_read = file_read(file, buffer, size);
    lock_release(&file_lock);
	 
  }


  return bytes_read;
}

int
process_write(int fd, void *buffer, uint32_t size)
{

  int bytes_written= 0;

  if(fd == 1)
  {
    putbuf(buffer,size);
	bytes_written = size;
  }
  else
  {
    struct file * file = get_file(fd);
	if(!file)
	  return 0;

	lock_acquire(&file_lock);

	bytes_written = file_write(file, buffer, size);

	lock_release(&file_lock);
  }

  return bytes_written;
}

void
process_seek(int fd, uint32_t position)
{

  lock_acquire(&file_lock);

  struct file * file = get_file(fd); 
  if(file)
    file_seek(file,position);

	lock_release(&file_lock);
}

uint32_t 
process_tell(int fd)
{
  struct file * file = get_file(fd);
  if(!file)
	return 0;
  
  lock_acquire(&file_lock);

  uint32_t pos = file_tell(file);

  lock_release(&file_lock);

  return pos;
}

void 
process_close(int fd)
{
  struct file_info * fi = get_file_info(fd);  
  if(fi)
  {
	lock_acquire(&file_lock);

    file_close(fi->file);
	list_remove(&fi->elem);
	free(fi);

	lock_release(&file_lock);

  }
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
static bool init_stack (void **esp, const char *file_name); 
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
  char * fn_copy = NULL;

  lock_acquire(&file_lock);


  /* Mark that this is a process */
  t->is_process = true;

  /* Allocate and activate page directory. */
  //TODO pagedir_destroy if this fails later
  t->pagedir = pagedir_create ();
  if (t->pagedir == NULL) 
    goto done;
  process_activate ();

  /* Create a copy of the file name */
  fn_copy = palloc_get_page (0);
  if (fn_copy == NULL)
    return TID_ERROR;
  strlcpy (fn_copy, file_name, PGSIZE);
  
  /* Parse the file name */
  char *args, *save_ptr;
  args = strtok_r(fn_copy, " ", &save_ptr);

  /* Open executable file. */
  file = filesys_open (args);
  /* Done with the copy */
  if(fn_copy != NULL)
    palloc_free_page (fn_copy); 

  if (file == NULL) 
    {
      printf ("load: %s: open failed\n", file_name);
      goto done; 
    }

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

  if (!init_stack(esp, file_name))
	goto done;

  /* Start address. */
  *eip = (void (*) (void)) ehdr.e_entry;

  success = true;

 done:
  /* We arrive here whether the load is successful or not. */
  file_close (file);

  lock_release(&file_lock);
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

/* Make sure the esp is within a page from PHYS_BASE */
static bool 
dec_esp(void ** esp,int size)
{
 *esp -= size;
 return (void*)*esp > (void*)((uint8_t *) PHYS_BASE) - PGSIZE;
}

static bool
init_stack(void **esp, const char *file_name)
{

  bool success = false;

  char * fn_copy = palloc_get_page (0);
  if(fn_copy == NULL)
	return success;

  /* Create a copy of the file name */
  strlcpy (fn_copy, file_name, PGSIZE);

  /* Create an array to store pointers to each arg */
  void ** pointers = palloc_get_page(0);
  
  /* Check for successful memory allocation */
  if( pointers == NULL)
  {
    palloc_free_page(fn_copy);
    return success;
  }


  /* Iterate through each arg adding it to the stack */
  char *args, *save_ptr;
  int argc = 0;
  for (args = strtok_r (fn_copy, " ", &save_ptr);
		args != NULL; 
		args = strtok_r(NULL, " ", &save_ptr))
  {
	int size = strnlen(args, PGSIZE);

	if(!dec_esp(esp,size+1))
    { 
	  palloc_free_page(fn_copy);
      palloc_free_page(pointers);
      return false; 
	}


	strlcpy((char*)*esp,args,size + 1);
	pointers[argc] = *esp;

	argc++;
  }

 /* Word Align */ 
  while((int)*esp % 4 != 0){
	*(char*)--*esp = '0';
  }

  /* Add pointers to stack */
  pointers[argc] = NULL;
  for(int i = argc; i >= 0; i--)
  {
	/* Decrement and check validity */
	if(!dec_esp(esp,4))
    { 
	  palloc_free_page(fn_copy);
      palloc_free_page(pointers);
      return false; 
	}
    *(int*)*esp = (int)pointers[i];
  }


  /* Add argv and argc */
  int * argv = *esp;

  /* Check the next 12 bytes all at once for conveniance*/
  if(!dec_esp(esp,12))
  {
    palloc_free_page(fn_copy);
    palloc_free_page(pointers);
    return false; 
  }

  /* We then need to add because we decrimented extra */
  *(int*)(*esp+8) = (int)argv; 

  *(int*)(*esp+4) = (int)argc; 

  /* Add a fake return */
  *(int*)*esp = (int)NULL; 

  palloc_free_page(fn_copy);
  palloc_free_page(pointers);

  //return verify_esp(esp);

  /* Following line is for testing */
  //hex_dump(0,*esp,PHYS_BASE - *esp,true); 
  
  success = true;
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