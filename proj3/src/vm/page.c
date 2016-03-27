#include "vm/page.h"

#include <inttypes.h>
#include <stdio.h>
#include "threads/thread.h"
#include "threads/malloc.h"
#include "filesys/file.h"

struct list supp_page_table;

void page_add(struct list * supp_page_table,
	            uint8_t* user_vaddr,
				struct file * file,
				uint32_t read_bytes,
				bool writable)
{

  struct page_info * pi = malloc(sizeof(struct page_info));
  pi->user_vaddr = user_vaddr;
  pi->file = file;
  pi->read_bytes = read_bytes;
  pi->writable = writable;

  list_push_back(supp_page_table,&pi->elem);
}

void page_remove(struct list * supp_page_table,
				 uint8_t * user_vaddr)
{
  struct list_elem *e;
  for (e = list_begin(supp_page_table); e != list_end(supp_page_table);
	   e = list_next(e))
  {
    struct page_info * pi = list_entry(e, struct page_info,elem);
    if(pi->user_vaddr == user_vaddr)
    {
      list_remove(e);
      free(pi);
      break;
    }
  }
}


