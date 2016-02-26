#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"
#include "threads/synch.h"
#include <list.h>

tid_t process_execute (const char *file_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (void);

struct process_info
{
  tid_t tid;
  struct semaphore sema_start;
  struct semaphore sema_finish;
  int exit_status;
  struct list_elem elem;
  bool success;
};

#endif /* userprog/process.h */
