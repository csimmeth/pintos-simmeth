#include "vm/swap.h"
#include "vm/frame.h"
#include "devices/block.h"
#include "lib/kernel/bitmap.h"

static struct block * global_swap_block;
static struct bitmap * swap_map;

void swap_init(void)
{
  global_swap_block = block_get_role (BLOCK_SWAP);
  swap_map = bitmap_create(1000);

}

int swap_write(struct frame_info * fi)
{
  /* Find an open position in the bitmap and read
   * the data from the frame into the location  * 8 
   * on the block, then return that location */

  return -1;

}

void swap_read(int position, void * buffer)
{

  /* Read the data from position in the block into the
   * buffer that is a frame location */

}

