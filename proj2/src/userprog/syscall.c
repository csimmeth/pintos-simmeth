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

static void exit(struct intr_frame *f);
static void write(struct intr_frame *f);
static void halt(void);

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

  printf("Syscall: %d\n",syscall);
  

  switch(syscall){

	case SYS_HALT:
	    halt();
		break;

	case SYS_EXIT:
	    exit(f);
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
  printf("Status: %d\n",status);
  f->eax = status;
  process_exit();
}

static void 
write(struct intr_frame * f)
{
  int fd = get_int(f,1);
  char * buffer = get_char_ptr(f,2);
  int size = get_int(f,3);
  putbuf(buffer,size);

}

//TODO synch for all file sys calls
//deny writes to the running file
