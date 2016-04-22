#ifndef FILESYS_CACHE_H
#define FILESYS_CACHE_H

#include <stdbool.h>
#include "devices/block.h"

void cache_init(void);
void cache_read(block_sector_t, void*, int, size_t);
void cache_read_map(void*,int,size_t);
void cache_write(block_sector_t,const void*, int, size_t);
void cache_write_map(const void*,int,size_t);
void cache_create(block_sector_t sector_id, void * buffer);
void cache_close(void);


#endif /* filesys/cache.h */
