#ifndef VM_PAGE_H
#define VM_PAGE_H

#include "threads/thread.h"
#include "filesys/file.h"
#include <inttypes.h>
#include <stdio.h>

void page_init(void);
struct page_info * page_add(struct list * supp_page_table,
				uint8_t* user_vaddr,
				struct file * file,
				uint32_t read_bytes,
				uint32_t ofs,
				bool writable);
void page_remove(struct list * supp_page_table,
				 uint8_t * user_vaddr);

bool is_page(struct list * supp_page_table,
				 uint8_t * user_vaddr);
bool is_read_only(struct list * supp_page_table,
				 uint8_t * user_vaddr);

void remove_file_mappings(struct list * supp_page_table, 
	 				      struct file * file);
struct page_info
{
  struct list_elem elem;
  
  // The address
  uint8_t* user_vaddr;

  // Information about data in this page
  struct file * file;
  uint32_t read_bytes;
  uint32_t ofs;
  bool writable;
  uint32_t swap_location;
};

#endif /* vm/page.h */
