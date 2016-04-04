#include "vm/page.h"

#include <inttypes.h>
#include <stdio.h>
#include "threads/thread.h"
#include "threads/malloc.h"
#include "threads/vaddr.h"
#include "filesys/file.h"
#include "userprog/pagedir.h"
#include "userprog/process.h"

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

struct page_info * page_add(struct list * supp_page_table,
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
  pi->swap_location = -1;

  list_push_back(supp_page_table,&pi->elem);

  return pi;
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
	  pagedir_clear_page(thread_current()->pagedir,user_vaddr);
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

bool is_read_only(struct list * supp_page_table,
				  uint8_t * user_vaddr)
{
  struct page_info * pi = get_pi(supp_page_table,user_vaddr);
  return !pi->writable;
}

void remove_file_mappings(struct list * supp_page_table, 
	                      struct file * file)
{

  struct list_elem *e = list_begin(supp_page_table);

  while (e != list_end(supp_page_table))
  {
	struct list_elem * f = list_next(e); 
    struct page_info * pi = list_entry(e, struct page_info,elem);
    if(pi->file == file)
    {
	  //Check if dirty
	  //write to file
	  uint32_t * pd = thread_current()->pagedir;
	  acquire_file_lock();
	  if(pagedir_is_dirty(pd,pi->user_vaddr)) /*||
	   	pagedir_is_dirty(pd, pagedir_get_page(pd,
			                          pi->user_vaddr)))*/
	  {
		file_write(file,pi->user_vaddr,pi->read_bytes);
	  }

	  release_file_lock(); 

      list_remove(e);
	  pagedir_clear_page(thread_current()->pagedir,pi->user_vaddr);
      free(pi);
    }
	e = f;
  }

}
