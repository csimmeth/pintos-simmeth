#ifndef VM_SWAP_H
#define VM_SWAP_H

#include "vm/frame.h"


void swap_init(void);

int swap_write(struct frame_info *);

void swap_read(int position, void * buffer);


#endif /* vm/swap.h */
