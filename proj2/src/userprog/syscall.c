#include "userprog/syscall.h"
#include "userprog/pagedir.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "devices/shutdown.h"
#include "userprog/process.h"
#include "filesys/filesys.h"

static void syscall_handler (struct intr_frame *);

static void halt(void);
static void exit(struct intr_frame *f);
static void exec(struct intr_frame *f);
static void wait(struct intr_frame *f);
static void create(struct intr_frame *f);
static void open(struct intr_frame *f);
static void write(struct intr_frame *f);

struct lock file_lock;


void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
  lock_init(&file_lock);
}

static void 
verify_addr(void * a)
{
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

	case SYS_OPEN
	   open(f);
	   break;

	case SYS_WRITE:
	   write(f);
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
  thread_current()->exit_status = status;

  f->eax = status;

  thread_exit();	
}

static void
exec(struct intr_frame *f)
{
  char * file_name = get_char_ptr(f,1);
  
  lock_acquire(&file_lock);
  tid_t tid = process_execute(file_name); 

  /* If the execution failed, return */
  if(tid == TID_ERROR){
	f->eax = tid;
    lock_release(&file_lock);
	return;
  }

  /* Get the info of the created process */
  struct list_elem *e = list_back(&thread_current()->children);
  struct process_info *p = list_entry(e,struct process_info, elem);

  /* Wait until the process has been initialized */
  sema_down(&p->sema_start);

  /* Return the pid */
  if(p->success)
    f->eax = p->tid;
  else
	f->eax = TID_ERROR;

   lock_release(&file_lock);
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

  lock_acquire(&file_lock);

  bool success = filesys_create(name, size);

  f->eax = success;

  lock_release(&file_lock);

}

static void
open(struct intr_frame *f)
{
  char *file_name = get_char_ptr(f,1);

  lock_acquire(&file_lock);
}

static void 
write(struct intr_frame * f)
{
  //int fd = get_int(f,1);
  //printf("FD: %d\n", fd);
  char * buffer = get_char_ptr(f,2);
  int size = get_int(f,3);
  putbuf(buffer,size);

}

//TODO synch for all file sys calls
//deny writes to the running file
