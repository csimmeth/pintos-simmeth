#include "vm/frame.h"
#include "threads/palloc.h"

#include <inttypes.h>
#include <stdio.h>
#include <hash.h>
#include <threads/malloc.h>
#include <threads/palloc.h>
#include <threads/synch.h>
#include <devices/timer.h>

struct list frame_table;
struct lock frame_lock;

/* Initialize the frame table */
void
frame_init(void)
{
  list_init(&frame_table);
  lock_init(&frame_lock);
}

static void 
evict_page(void){

  lock_acquire(&frame_lock);
  /* Initialize with the first element in the list */
  struct frame_info * frame_to_evict;
  frame_to_evict = list_entry(list_front(&frame_table),
	  						  struct frame_info,elem);
  struct list_elem *e;
  for (e = list_begin(&frame_table); e != list_end(&frame_table);
	   e = list_next(e))
  {
    struct frame_info * fi = list_entry(e, struct frame_info, elem);
	if(fi->access_time < frame_to_evict->access_time)
	{
      frame_to_evict = fi;
	}

  }

  //int swap_location = swap_write(frame)
  
  
  
  

  
 lock_release(&frame_lock); 

}

/* Get a page of memory */
void *
frame_get_page(enum palloc_flags flags, struct page_info * pi)
{
  lock_acquire(&frame_lock);
  uint8_t *kpage;
  kpage = palloc_get_page(flags);

  if(kpage == NULL)
  {
    //Iterate through the frame list, find least recently used
	//Evict this, in that supp page table, mark that this has been
	//moved to swap, remove from that thread's page table
	//call frame_free_page on that kpage
	evict_page();

	kpage = palloc_get_page(flags);
  }


  struct frame_info * fi = malloc(sizeof(struct frame_info));
  fi->kpage = kpage;
  fi->access_time = timer_ticks();
  fi->pi = pi;
  fi->owner = thread_current();

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
