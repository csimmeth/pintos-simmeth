#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"
#include "threads/synch.h"
#include <list.h>

tid_t process_execute (const char *file_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (void);

struct child_process
{
  struct thread * child;
  struct semaphore sema;
  int exit_status;
  struct list_elem elem;
};

#endif /* userprog/process.h */