#ifndef VM_FRAME_H
#define VM_FRAME_H

#include <stdio.h> /* needed for bool? */
#include "threads/thread.h"
#include "threads/palloc.h"
#include "vm/page.h"

void frame_init(void);
void * frame_get_page(enum palloc_flags flags,struct page_info * pi);
/*void frame_install_page(void * kpage, 
	                    void * upage, 
						struct thread * owner);
*/

void frame_free_page(void * kpage);
void frame_get_lock(void);
void frame_release_lock(void);


struct frame_info
{
  struct list_elem elem;
  void * kpage;
  void * upage;
  struct thread * owner;
  struct page_info * pi;
  
  int64_t access_time;
  
  // tid so that when a thread exits it's entries
  // can be freed

};

#endif /* vm/frame.h */
