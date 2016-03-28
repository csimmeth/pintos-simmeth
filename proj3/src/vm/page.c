#include "vm/page.h"

#include <inttypes.h>
#include <stdio.h>
#include "threads/thread.h"
#include "threads/malloc.h"
#include "threads/vaddr.h"
#include "filesys/file.h"

struct list supp_page_table;

static struct page_info *
get_pi(struct list * supp_page_table,
	   uint8_t* user_vaddr)
{
  struct list_elem *e;
  for (e = list_begin(supp_page_table); e != list_end(supp_page_table);
	   e = list_next(e))
  {
    struct page_info * pi = list_entry(e, struct page_info,elem);
    if(pg_no(pi->user_vaddr) == pg_no(user_vaddr))
    {
	  return pi;
    }
  }
  return NULL;
}

void page_add(struct list * supp_page_table,
	            uint8_t* user_vaddr,
				struct file * file,
				uint32_t read_bytes,
				uint32_t ofs,
				bool writable)
{

  //printf("adding %p\n",user_vaddr);
  struct page_info * pi = malloc(sizeof(struct page_info));
  pi->user_vaddr = user_vaddr;
  pi->file = file;
  pi->read_bytes = read_bytes;
  pi->writable = writable;
  pi->ofs = ofs;

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
  /*
  struct page_info * pi = get_pi(supp_page_table,user_vaddr);
  if(pi != NULL)
  {
    list_remove(&pi->elem);
    free(pi);
  }
  */
}

bool is_page(struct list * supp_page_table, 
	         uint8_t * user_vaddr)
{
  /*
  struct list_elem *e;
  for (e = list_begin(supp_page_table); e != list_end(supp_page_table);
	   e = list_next(e))
  {
    struct page_info * pi = list_entry(e, struct page_info,elem);
	//printf("User: %p In table: %p\n",user_vaddr,pi->user_vaddr);
    if(pg_no(pi->user_vaddr) == pg_no(user_vaddr))
    {
	  return true;
    }
  }
  return false;
  struct page_info * pi = get_pi(supp_page_table,user_vaddr);
  if(pi == NULL)
	return false;
  else 
	return true;
  */
  return get_pi(supp_page_table,user_vaddr);
}


