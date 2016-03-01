#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"
#include "threads/synch.h"
#include <list.h>

tid_t process_execute (const char *file_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (void);
void process_init(void);
bool process_file_create(char *name, uint32_t size);
bool process_file_remove(char *name);
int process_file_open(char *name);

struct process_info
{
  tid_t tid;
  struct thread * child;
  struct semaphore sema_start;
  struct semaphore sema_finish;
  int exit_status;
  struct list_elem elem;
  bool success;
};

struct file_info
{
  struct file * file;
  int fd;
  struct list_elem elem;
};

#endif /* userprog/process.h */
