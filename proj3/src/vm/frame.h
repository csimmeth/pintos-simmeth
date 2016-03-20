#ifndef VM_FRAME_H
#define VM_FRAME_H

#include <stdio.h> /* needed for bool? */

void frame_init();
void * frame_get_page(bool zero);

#endif /* vm/frame.h */
