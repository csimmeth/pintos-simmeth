#include "filesys/cache.h"
#include "threads/synch.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include "filesys/filesys.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "lib/kernel/list.h"
#include "devices/timer.h"

#define CACHE_SIZE 64

struct lock io_lock;
struct lock stack_lock;
struct lock main_lock;

struct list lru_stack;

bool running;

struct cache_entry
{
  block_sector_t sector_id;
  uint8_t* data;
  bool dirty;
  bool used;
  struct lock entry_lock;
  int rw_count;
  struct list_elem elem;
};

struct cache_entry cache[CACHE_SIZE];
uint8_t* free_map;

static void
cache_flush(void)
{
  for(int i = 0; i < CACHE_SIZE; i++)
  {
    struct cache_entry * e = &cache[i];
	lock_acquire(&io_lock);
	lock_acquire(&e->entry_lock);
	if(e->dirty)
	{
	 block_write(fs_device,e->sector_id,
			e->data);
    }

	lock_release(&e->entry_lock);
	lock_release(&io_lock);
  }
} 

static void 
auto_save(void *aux UNUSED)
{
  while(running)
  {
    cache_flush();
    timer_msleep(100);
  }

}

void
cache_init()
{
  list_init(&lru_stack);
  lock_init(&stack_lock);
  lock_init(&io_lock);
  lock_init(&main_lock);
  free_map = malloc(BLOCK_SECTOR_SIZE);
  block_read(fs_device,FREE_MAP_DATA,free_map);

  for(int i = 0; i < CACHE_SIZE; i++)
  {
    lock_init(&cache[i].entry_lock);
	cache[i].data = malloc(BLOCK_SECTOR_SIZE);
	cache[i].used= false;
	cache[i].dirty= false;
	cache[i].rw_count = 0;
	list_push_back(&lru_stack,&cache[i].elem);
  }
  running = true;
  //thread_create("AutoSaveCache",PRI_DEFAULT,&auto_save,NULL);
}	

static struct cache_entry *
evict_block(void)
{
  lock_acquire(&stack_lock);
  struct cache_entry * entry_to_evict  = NULL;
  while(!entry_to_evict)
  {
	struct list_elem *e;
	for( e = list_begin (&lru_stack); e != list_end (&lru_stack);
		 e = list_next(e))
	  {
		struct cache_entry * ce = list_entry (e, struct cache_entry,elem);
		lock_acquire(&ce->entry_lock);
        if(ce->rw_count == 0)
		{
	      entry_to_evict = ce;
		  ce->used = false;
		  lock_release(&ce->entry_lock);
		  break;
		}
		lock_release(&ce->entry_lock);
	  }
  }
  lock_release(&stack_lock);

  if(entry_to_evict->dirty)
  {
	block_write(fs_device,entry_to_evict->sector_id,
				entry_to_evict->data);
  }

  return entry_to_evict;
}
static struct cache_entry *
locate_data(block_sector_t sector_id, bool new){

  // Check if the data is already in the cache 
  // Doesn't need to happen if new
  if(!new)
  {
   for(int i = 0; i < CACHE_SIZE; i++)
   {
	lock_acquire(&cache[i].entry_lock);
    if(cache[i].sector_id == sector_id && cache[i].used)
	{
      // printf("found it at block %i\n",i);
	  cache[i].rw_count++;
	  lock_release(&cache[i].entry_lock);
	  return &cache[i];
	}
	lock_release(&cache[i].entry_lock);
   }
  }

  //Acquire I/O lock
  lock_acquire(&io_lock);

  struct cache_entry * e = NULL;

  // Find an unused entry 
  for(int i = 0; i < CACHE_SIZE; i++)
  {
	if(!cache[i].used)
	  e = &cache[i];	

	// Check if the data is now in the cache after acquiring the lock
	if(cache[i].sector_id == sector_id)
	{
	  lock_acquire(&cache[i].entry_lock);
	  cache[i].rw_count++;
	  lock_release(&cache[i].entry_lock);
      lock_release(&io_lock);
	  return &cache[i];
	}
  }


  // If there was no empty block, evict someone 
  // Loop until an empty cache spot is found
  if(!e)
    e = evict_block();

  // Bring in new data
  lock_acquire(&e->entry_lock);
  e->sector_id = sector_id;
  e->dirty = false;
  e->used = true;
  e->rw_count++;

  // If this is not a new inode, get previous data */
  if(!new)
  {
    block_read(fs_device,sector_id,e->data);
  }

  lock_release(&e->entry_lock);
  lock_release(&io_lock);
  return e;
}

void cache_read_map(void *buffer,int ofs,size_t size)
{
  lock_acquire(&main_lock);
  memcpy(buffer,free_map + ofs,size);
  lock_release(&main_lock);
}

void
cache_read(block_sector_t sector_id, void* buffer, 
	            int ofs, size_t size)
{

  lock_acquire(&main_lock);
  struct cache_entry * e = locate_data(sector_id, false);
  lock_acquire(&e->entry_lock);
  memcpy (buffer, e->data + ofs, size);
  e->rw_count--;
  lock_release(&e->entry_lock);

  //add to back of queue
  lock_acquire(&stack_lock);
  list_remove(&e->elem);
  list_push_back(&lru_stack,&e->elem);
  lock_release(&stack_lock);
  lock_release(&main_lock);
}

void 
cache_write_map(const void * buffer,int ofs,size_t size)
{
  lock_acquire(&main_lock);
  memcpy(free_map + ofs,buffer,size);
  lock_release(&main_lock);
}

void cache_write(block_sector_t sector_id, const void * buffer,
				 int ofs, size_t size)
{
  lock_acquire(&main_lock);
  struct cache_entry * e = locate_data(sector_id, false);
  lock_acquire(&e->entry_lock);
  memcpy(e->data + ofs, buffer, size);
  e->rw_count--;
  e->dirty = true;
  lock_release(&e->entry_lock);

  lock_acquire(&stack_lock);
  list_remove(&e->elem);
  list_push_back(&lru_stack,&e->elem);
  lock_release(&stack_lock);
  lock_release(&main_lock);
}

void cache_create(block_sector_t sector_id, void * buffer)
{
  lock_acquire(&main_lock);
  struct cache_entry * e = locate_data(sector_id, true);
  lock_acquire(&e->entry_lock);
  memcpy(e->data, buffer, BLOCK_SECTOR_SIZE);
  e->rw_count--;
  e->dirty = true;
  lock_release(&e->entry_lock);

  /* Put in back of queue */
  lock_acquire(&stack_lock);
  list_remove(&e->elem);
  list_push_back(&lru_stack,&e->elem);
  lock_release(&stack_lock);
  lock_release(&main_lock);
}


void
cache_close(void)
{
  running = false; 
  cache_flush();
  block_write(fs_device,FREE_MAP_DATA,free_map);
}

