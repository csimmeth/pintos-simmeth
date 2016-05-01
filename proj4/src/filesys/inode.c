#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include <stdio.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "filesys/cache.h"
#include "threads/malloc.h"
#include "threads/synch.h"

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44
#define INODE_OFS 4

/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */
struct inode_disk
  {
    off_t length;                       /* File size in bytes. */
    block_sector_t sectors[126];               /* First data sector. */
    unsigned magic;                     /* Magic number. */
  };

/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
static inline size_t
bytes_to_sectors (off_t size)
{
  return DIV_ROUND_UP (size, BLOCK_SECTOR_SIZE);
}

/* In-memory inode. */
struct inode 
  {
    struct list_elem elem;              /* Element in inode list. */
    block_sector_t sector;              /* Sector number of disk location. */
    int open_cnt;                       /* Number of openers. */
    bool removed;                       /* True if deleted, false otherwise. */
    int deny_write_cnt;                 /* 0: writes ok, >0: deny writes. */

	/* Inode Content */
	block_sector_t data_start;
	off_t data_length;
	struct lock ilock;
	
  };

static void
allocate_sector(block_sector_t * psector, block_sector_t parent, int ofs)
{
  /* Get a new sector and write it in the parent block */
  static char zeros[BLOCK_SECTOR_SIZE];
  free_map_allocate(1,psector);
  cache_create(*psector,zeros);
  cache_write(parent,psector,ofs,4);
}  

static block_sector_t
byte_to_psector (const struct inode *inode, off_t pos)
{
  block_sector_t psector;
  block_sector_t vsector = pos / BLOCK_SECTOR_SIZE;

  if(vsector < 12)
  {
	/* Read the data sector directly from the inode */
    int ofs = INODE_OFS + 4 * vsector;
    cache_read(inode->sector,&psector,ofs,4);

    if(!psector)
    	allocate_sector(&psector,inode->sector,ofs);
  }
  else if (vsector < 140)
  {
	/* First get the iblock */
	block_sector_t isector;
	int iofs = 48 + INODE_OFS;
	cache_read(inode->sector,&isector,iofs,4);
	if(!isector)
	  allocate_sector(&isector,inode->sector,iofs);

	/* Then get the data sector */
	vsector -= 12;
	int ofs = 4 * vsector;
	cache_read(isector,&psector,ofs,4);
	if(!psector)
	  allocate_sector(&psector,isector,ofs);
  }
  else if (vsector < 16524)
  {
	vsector -= 140;
	/* First get the double iblock */
	block_sector_t double_isector;
	int double_ofs = 52 + INODE_OFS;
	cache_read(inode->sector,&double_isector,double_ofs,4);
	if(!double_isector)
	  allocate_sector(&double_isector,inode->sector,double_ofs);

	/* Then get the iblock */
	block_sector_t isector;
	int iofs = (vsector / 128) * 4;
	cache_read(double_isector,&isector,iofs,4);
	if(!isector)
	  allocate_sector(&isector,double_isector,iofs);

	/* Finally, read the data sector */
    int ofs = (vsector % 128) * 4;
	cache_read(isector,&psector,ofs,4);
	if(!psector)
	  allocate_sector(&psector,isector,ofs);
  }
  else
  {
	printf("File too big!");
	psector = -1;
  }
 //printf("returning psector %d at vsector %d from sector %d\n",psector,vsector,inode->sector);
  return psector;
}

/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;

/* Initializes the inode module. */
void
inode_init (void) 
{
  list_init (&open_inodes);
}

/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   device.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
bool
inode_create (block_sector_t sector, off_t length)
{
  struct inode_disk *disk_inode = NULL;
  bool success = false;

  ASSERT (length >= 0);

  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
  ASSERT (sizeof *disk_inode == BLOCK_SECTOR_SIZE);

  disk_inode = calloc (1, sizeof *disk_inode);
  if (disk_inode != NULL)
    {
      disk_inode->length = length;
      disk_inode->magic = INODE_MAGIC;
	  if(sector ==0 )
		  {
            static char zeros[BLOCK_SECTOR_SIZE];
			disk_inode->sectors[0] = FREE_MAP_DATA;
			cache_create(disk_inode->sectors[0],zeros);
		  }

	  cache_create(sector,disk_inode);
      success = true; 

      free (disk_inode);
    }
  return success;
}

/* Reads an inode from SECTOR
   and returns a `struct inode' that contains it.
   Returns a null pointer if memory allocation fails. */
struct inode *
inode_open (block_sector_t sector)
{
  struct list_elem *e;
  struct inode *inode;

  /* Check whether this inode is already open. */
  for (e = list_begin (&open_inodes); e != list_end (&open_inodes);
       e = list_next (e)) 
    {
      inode = list_entry (e, struct inode, elem);
      if (inode->sector == sector) 
        {
          inode_reopen (inode);
          return inode; 
        }
    }

  /* Allocate memory. */
  inode = malloc (sizeof *inode);
  if (inode == NULL)
    return NULL;

  /* Initialize. */
  list_push_front (&open_inodes, &inode->elem);
  inode->sector = sector;
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->removed = false;
  lock_init(&inode->ilock);
  cache_read (inode->sector,&inode->data_start,4,4);
  cache_read (inode->sector,&inode->data_length,0,4);
  return inode;
}

/* Reopens and returns INODE. */
struct inode *
inode_reopen (struct inode *inode)
{
  if (inode != NULL)
    inode->open_cnt++;
  return inode;
}

/* Returns INODE's inode number. */
block_sector_t
inode_get_inumber (const struct inode *inode)
{
  return inode->sector;
}

/* Closes INODE and writes it to disk.
   If this was the last reference to INODE, frees its memory.
   If INODE was also a removed inode, frees its blocks. */
void
inode_close (struct inode *inode) 
{
  /* Ignore null pointer. */
  if (inode == NULL)
    return;

  /* Release resources if this was the last opener. */
  if (--inode->open_cnt == 0)
    {
      /* Remove from inode list and release lock. */
      list_remove (&inode->elem);
 
      /* Deallocate blocks if removed. */
      if (inode->removed) 
        {
          free_map_release (inode->sector, 1);
		  int bytes = 1;
		  block_sector_t psector;
		  while(bytes < inode->data_length)
		  {
			psector = byte_to_psector(inode,bytes);
			free_map_release(psector,1);
			bytes += BLOCK_SECTOR_SIZE;
		  }	
        }

      free (inode); 
    }
}

/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void
inode_remove (struct inode *inode) 
{
  ASSERT (inode != NULL);
  inode->removed = true;
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
off_t
inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset) 
{
  uint8_t *buffer = buffer_;
  off_t bytes_read = 0;

  if(inode->sector ==0)
  {
    cache_read_map(buffer,offset,size);
	return size;
  }

  while (size > 0) 
    {
      /* Disk sector to read, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_psector (inode, offset);
	  //printf("reading from sector %d, from inode %d\n",sector_idx,inode->sector);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually copy out of this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

	  cache_read(sector_idx,buffer+bytes_read, sector_ofs, chunk_size);

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_read += chunk_size;
    }

  return bytes_read;
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if end of file is reached or an error occurs.
   (Normally a write at end of file would extend the inode, but
   growth is not yet implemented.) */
off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size,
                off_t offset) 
{
  const uint8_t *buffer = buffer_;
  off_t bytes_written = 0;

  if (inode->deny_write_cnt)
    return 0;

  if(inode->sector ==0)
  {
    cache_write_map(buffer,offset,size);
	return size;
  }

  if(offset + size > inode_length(inode))
  {
	
	lock_acquire(&inode->ilock);
	int old_sectors = bytes_to_sectors(inode->data_length);
	int new_sectors = bytes_to_sectors(offset + size);
	for(int i = old_sectors; i < new_sectors; i++)
	{
	  byte_to_psector(inode,i * BLOCK_SECTOR_SIZE +1);
	}

	inode->data_length = offset + size;
	cache_write(inode->sector,&inode->data_length,0,4);
	lock_release(&inode->ilock);
  }

  while (size > 0) 
    {
      /* Sector to write, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_psector (inode, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually write into this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      cache_write(sector_idx, buffer + bytes_written,
			 		  sector_ofs,chunk_size);

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;
    }

  return bytes_written;
}

/* Disables writes to INODE.
   May be called at most once per inode opener. */
void
inode_deny_write (struct inode *inode) 
{
  inode->deny_write_cnt++;
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
}

/* Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. */
void
inode_allow_write (struct inode *inode) 
{
  ASSERT (inode->deny_write_cnt > 0);
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
  inode->deny_write_cnt--;
}

/* Returns the length, in bytes, of INODE's data. */
off_t
inode_length (const struct inode *inode)
{
  return inode->data_length;
}
