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
static void write(struct intr_frame *f);

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void * 
get_arg(struct intr_frame *f, int arg){

  return f->esp + (arg * sizeof(int));
}

static int
get_int(struct intr_frame *f, int arg){
  return *(int*)get_arg(f,arg);
}

static char*
get_char_ptr(struct intr_frame *f, int arg){
  return *(char**)get_arg(f,arg);
}

static void
syscall_handler (struct intr_frame *f) 
{
  
  // Check for invalid memory address 
  if(f->esp == NULL || f->esp >= PHYS_BASE){
    printf("esp > phys_base or NULL\n");
    printf("esp: %p\n",f->esp);
	thread_exit();
  }
	
  void * page = pagedir_get_page(thread_current()->pagedir,f->esp);
  if(page == NULL){
     printf ("Page not found!\n");
     thread_exit ();
  }

  int syscall = get_int(f,0); 

  //printf("Syscall: %d\n",syscall);
  

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

	case SYS_WRITE:
	   write(f);
	   break;

	case SYS_WAIT:
	   wait(f);
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
  //TODO this might cause problems if p_info is gone
  if(thread_current()->p_info != NULL){
    thread_current()->p_info->exit_status = status;
  }
  thread_current()->exit_status = status;

  f->eax = status;
   //printf("Exit Status: %d\n", f->eax);

  thread_exit();	
}

static void
exec(struct intr_frame *f)
{
  char * file_name = get_char_ptr(f,1);
  tid_t tid = process_execute(file_name); 

  /* If the execution failed, return */
  if(tid == TID_ERROR){
	f->eax = tid;
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
}

static void
wait(struct intr_frame *f)
{
  tid_t pid = get_int(f,1);
  int exit_status = process_wait(pid);
  f->eax = exit_status;
  
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
