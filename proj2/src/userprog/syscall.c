#include "userprog/syscall.h"
#include "userprog/pagedir.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"

static void syscall_handler (struct intr_frame *);

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
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

  int syscall = *(int*)f->esp;
  printf ("Call Number: %d\n",syscall);

  switch(syscall){
   // fill this out	
  }

  printf ("system call unknown!\n");
  thread_exit ();
}
