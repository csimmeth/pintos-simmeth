#include "vm/frame.h"
#include "threads/palloc.h"

#include <inttypes.h>
#include <stdio.h>
#include <hash.h>
#include <threads/malloc.h>
#include <threads/palloc.h>
#include <threads/synch.h>


/*
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
*/

//struct hash frame_table;
struct list frame_table;
struct lock frame_lock;

/* Initialize the frame table */
void
frame_init(void)
{
  list_init(&frame_table);
  lock_init(&frame_lock);
}

/* Get a page of memory */
void *
frame_get_page(enum palloc_flags flags)
{
  lock_acquire(&frame_lock);
  uint8_t *kpage;
  kpage = palloc_get_page(flags);

  if(kpage == NULL)
  {
	
   //Evict someone 
    lock_release(&frame_lock);
	return kpage;
  }


  struct frame_info * fi = malloc(sizeof(struct frame_info));
  fi->kpage = kpage;
  list_push_back(&frame_table,&fi->elem);

  lock_release(&frame_lock);
  return kpage;
}

void
frame_install_page(void * kpage, void * upage, struct thread * owner)
{
  lock_acquire(&frame_lock);
  struct list_elem *e;
  for (e = list_begin(&frame_table); e != list_end(&frame_table);
	   e = list_next(e))
  {
    struct frame_info * fi = list_entry(e, struct frame_info, elem);
    if(fi->kpage == kpage)
	{
	  fi->upage = upage;
	  fi->owner = owner;
	}
  }	

  lock_release(&frame_lock);
}

void frame_free_page(void * kpage)
{
  lock_acquire(&frame_lock);
  /* free the page */
  palloc_free_page(kpage);  


  /* Remove from list and free allocated memory */
  struct list_elem *e;
  struct frame_info * fi = NULL;
  for (e = list_begin(&frame_table); e != list_end(&frame_table);
	   e = list_next(e))
  {
    fi = list_entry(e, struct frame_info, elem);
	if(fi->kpage == kpage)
	{
      list_remove(e);
      free(fi);
	  break;
	}
  }
  lock_release(&frame_lock);
}
