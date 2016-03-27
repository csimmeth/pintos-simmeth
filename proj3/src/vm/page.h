#ifndef VM_PAGE_H
#define VM_PAGE_H

#include "threads/thread.h"
#include "filesys/file.h"
#include <inttypes.h>
#include <stdio.h>

void page_init(void);
void page_add(struct list * supp_page_table,
				uint8_t* user_vaddr,
				struct file * file,
				uint32_t read_bytes,
				bool writable);
void page_remove(struct list * supp_page_table,
				 uint8_t * user_vaddr);

struct page_info
{
  struct list_elem elem;
  
  uint8_t* user_vaddr;
  // Store time of access
  //
  struct file * file;
  uint32_t read_bytes;
  bool writable;

};

#endif /* vm/page.h */
