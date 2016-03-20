#include "vm/frame.h"
#include "threads/palloc.h"

#include <inttypes.h>
#include <stdio.h>
#include <hash.h>

/*
#include <stdlib.h>
#include <string.h>
#include "userprog/gdt.h"
#include "userprog/pagedir.h"
#include "userprog/tss.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/malloc.h"
#include "devices/input.h"
*/

struct hash frame_table;

/* Initialize the frame table */
void
frame_init()
{

}

/* Get a page of memory */
void *
frame_get_page(bool zero)
{
  uint8_t *kpage;
  if(zero)
     kpage = palloc_get_page(PAL_USER | PAL_ZERO);
  else
	 kpage = palloc_get_page(PAL_USER);

  //TODO account for if page is NULL
  return kpage;
}

frame_install_page()
{
}
