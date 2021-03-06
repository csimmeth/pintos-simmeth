#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"
#include "threads/synch.h"
#include <list.h>

tid_t process_execute (const char *file_name);
int process_wait (tid_t);
void process_exit (void);
void syscall_process_exit(int status);
void process_activate (void);
void process_init(void);
bool process_file_create(char *name, uint32_t size);
bool process_file_remove(char *name);
int process_file_open(char *name);
int process_filesize(int fd);
int process_read(int fd, void*buffer, uint32_t size);
int process_write(int fd, void*buffer, uint32_t size);
void process_seek(int fd, uint32_t position);
uint32_t process_tell(int fd);
void process_close(int fd);
int process_mmap(int fd, void * addr);
void process_munmap(int mapid);
bool acquire_file_lock(void);
void release_file_lock(void);


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
