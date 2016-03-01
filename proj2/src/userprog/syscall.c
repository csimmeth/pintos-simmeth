#include "userprog/syscall.h"
#include "userprog/pagedir.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "devices/shutdown.h"
#include "userprog/process.h"

static void syscall_handler (struct intr_frame *);

static void halt(void);
static void exit(struct intr_frame *f);
static void exec(struct intr_frame *f);
static void wait(struct intr_frame *f);
static void create(struct intr_frame *f);
static void remove(struct intr_frame *f);
static void open(struct intr_frame *f);
static void filesize(struct intr_frame *f);
static void read(struct intr_frame *f);
static void write(struct intr_frame *f);
static void seek(struct intr_frame *f);
static void tell(struct intr_frame *f);
static void close(struct intr_frame *f);



void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void 
verify_addr(void * a)
{
  /* Check that the address is not null, is part of user memory, 
   * and is part of a valid page */
  if(!a || !is_user_vaddr(a) ||
  !pagedir_get_page(thread_current()->pagedir,a))
	  thread_exit();
}

static void * 
get_arg(struct intr_frame *f, int arg){

  void * temp = f->esp + (arg * sizeof(int));
  verify_addr(temp);
  return temp;
}

static int
get_int(struct intr_frame *f, int arg){
  return *(int*)get_arg(f,arg);
}

static char*
get_char_ptr(struct intr_frame *f, int arg){
  verify_addr(*(char**)get_arg(f,arg));
  return *(char**)get_arg(f,arg);
}

static void*
get_void_ptr(struct intr_frame *f, int arg)
{
  verify_addr(*(void**)get_arg(f,arg));
  return *(void**)get_arg(f,arg);
}

static void
syscall_handler (struct intr_frame *f) 
{

  // Check for invalid memory address 
  verify_addr(f->esp);
	
  int syscall = get_int(f,0); 
  
  switch(syscall){

	case SYS_HALT:
	    halt();
		break;

	case SYS_EXIT:
	    exit(f);
		break;

	case SYS_EXEC:
		exec(f);
		break;

	case SYS_WAIT:
	   wait(f);
	   break;

	case SYS_CREATE:
	   create(f);
	   break;

	case SYS_REMOVE:
	   remove(f);
	   break;

	case SYS_OPEN:
	   open(f);
	   break;
	
	case SYS_FILESIZE:
       filesize(f);
	   break;

	case SYS_READ:
	   read(f);
	   break;

	case SYS_WRITE:
	   write(f);
	   break;

	case SYS_SEEK:
	   seek(f);
	   break;

	case SYS_TELL:
	   tell(f);
	   break;
	
	case SYS_CLOSE:
	   close(f);
	   break;

	default:
       printf ("system call unknown!\n");
       thread_exit ();
  }
}

static void
halt(void)
{
  shutdown_power_off();
}

static void 
exit(struct intr_frame * f)
{
  int status = get_int(f,1);

  /* Store the exit status for the parent */
  if(thread_current()->p_info != NULL){
    thread_current()->p_info->exit_status = status;
  }

  /* Store the thread's exit status for itself
   * Needed for final printout */
  thread_current()->exit_status = status;

  thread_exit();	
}

static void
exec(struct intr_frame *f)
{
  char * file_name = get_char_ptr(f,1);
  
  tid_t tid = process_execute(file_name); 

  f->eax = tid;

}

static void
wait(struct intr_frame *f)
{
  tid_t pid = get_int(f,1);
  int exit_status = process_wait(pid);
  f->eax = exit_status;
  
}

static void
create(struct intr_frame *f)
{
  char *name = get_char_ptr(f,1);
  uint32_t size = get_int(f,2);

  bool success = process_file_create(name, size);

  f->eax = success;
}

static void
remove(struct intr_frame *f)
{
  char *name = get_char_ptr(f,1);
  bool success = process_file_remove(name);
  f->eax = success;
}

static void
open(struct intr_frame *f)
{
  char *name = get_char_ptr(f,1);
  int fd = process_file_open(name);
  f->eax = fd;
}

static void 
filesize(struct intr_frame * f)
{
  int fd = get_int(f,1);
  int size = process_filesize(fd);
  f->eax = size;  
}

static void 
read(struct intr_frame * f)
{
  int fd = get_int(f,1);
  void * buffer = get_void_ptr(f,2); 
  uint32_t size = get_int(f,3);

  int read_size = process_read(fd, buffer, size);

  f->eax = read_size;
}

static void 
write(struct intr_frame * f)
{
  int fd = get_int(f,1);
  void * buffer = get_void_ptr(f,2);
  int size = get_int(f,3);

  int write_size = process_write(fd, buffer, size);

  f->eax = write_size;

}
static void 
seek(struct intr_frame * f)
{
  int fd = get_int(f,1);
  uint32_t position = get_int(f,2);

  process_seek(fd, position);
}
  
static void 
tell(struct intr_frame * f)
{
  int fd = get_int(f,1);
  uint32_t pos = process_tell(fd);

  f->eax = pos;
}

static void 
close(struct intr_frame * f)
{
  int fd = get_int(f,1);
  process_close(fd);
}
