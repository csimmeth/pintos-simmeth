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
#include "threads/thread.h"

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44
#define INODE_OFS 8

/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */
struct inode_disk
  {
    off_t length;                       /* File size in bytes. */
	uint32_t sec_alloc;
	//block_sector_t sectors[14];
	block_sector_t sectors[125];
	
    unsigned magic;                     /* Magic number. */
    //uint32_t unused[112];               /* Not used. */
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
	uint32_t data_sectors;
	
  };

/* Returns the block device sector that contains byte offset POS
   within INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */
static block_sector_t
byte_to_sector (const struct inode *inode, off_t pos) 
{
  ASSERT (inode != NULL);
  if (pos < inode->data_length)
    return inode->data_start + pos / BLOCK_SECTOR_SIZE;
  else
    return -1;
}

static void
allocate_block(block_sector_t * psector, block_sector_t block, int ofs)
{
  // Check if there is not a sector allocated yet
	// Get a new sector
	
    free_map_allocate (1, psector);
	///printf("result: %i\n",b);
	//printf("found section free: %i\n",*psector);

	// Zero out the new sector
    static char zeros[BLOCK_SECTOR_SIZE];
	cache_create(*psector,zeros);

	// Record the value of the new sector
    cache_write(block,psector,ofs,4);
}

static block_sector_t
byte_to_psector(struct inode *inode, off_t pos,int size)
{
  
  bool grow = false;
  if(bytes_to_sectors(pos+size) > inode->data_sectors)
  {
	uint32_t new_sectors = bytes_to_sectors(pos+size);
    grow = true;
	//printf("data_sectors: %d, old: %d \n",new_sectors,inode->data_sectors);

	inode->data_sectors = new_sectors; 
	cache_write(inode->sector,&inode->data_sectors,4,4);
  }
 
  block_sector_t psector;

  block_sector_t vsector = pos / BLOCK_SECTOR_SIZE;
  //printf("Thread %s \n",thread_name());
  //printf("trying to find vsector %d\n",vsector);
  if(vsector < 12)
  {
	// Calculate the offset
	int ofs = vsector * 4 + INODE_OFS;

	// Read sector directly from inode_disk
	 
	if(grow)
	{
	  allocate_block(&psector,inode->sector,ofs);
	  int temp; 
	  cache_read(inode->sector,&temp,ofs,4);
	  //printf("reading: %d\n",temp);
	  //printf("allocated block %d for inode %d at position %d\n",psector,inode->sector,ofs);
	}
	else
	{
      cache_read(inode->sector,&psector,ofs,4);
	  //printf("read sector %d from inode %d at position %d\n",psector,inode->sector,ofs);
	}

	//  if(psector == 0)
	///	allocate_block(&psector,inode->sector,ofs);

	//printf("got psector %d from vsector %d\n",psector,vsector);

	//allocate_if_null(&psector,inode->sector,ofs);

	return psector;
  }
  else if(vsector < 140)
  {
	int ofs = 48 + INODE_OFS;

	// Get the intermediate block
    block_sector_t iblock; 
	cache_read(inode->sector,&iblock,ofs,4);

	//allocate_block(&iblock,inode->sector,ofs);

	// Read the sector from the iblock
	vsector = vsector - 12;
	ofs = 4 * vsector;
	cache_read(iblock,&psector,ofs,4);

	//allocate_block(&psector,iblock,ofs);
	return psector;
  }
  else if(vsector < 16524)
  {
	// Get the first iblock
	int ofs = 52 + INODE_OFS;
	block_sector_t double_iblock;
	cache_read(inode->sector,&double_iblock,ofs,4);

    allocate_block(&double_iblock,inode->sector,ofs);

	// Calculate where to look in the double block
	vsector = vsector - 140;
	block_sector_t iblock;
    int block_ofs = (vsector / 128) * 4;

	// Get the second iblock
	cache_read(double_iblock,&iblock,block_ofs,4);

	allocate_block(&iblock,double_iblock,block_ofs);

	// Check where to look in the second block
	int sector_ofs = (vsector % 128 ) * 4;
	cache_read(iblock,&psector,sector_ofs,4);

	allocate_block(&psector,iblock,sector_ofs);
    return psector;
  }
  else 
	printf("file too big!");
	PANIC(__FILE__);
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

      size_t sectors = bytes_to_sectors (length);
      disk_inode->length = length; //12 * BLOCK_SECTOR_SIZE;
	  disk_inode->sec_alloc =0;// sectors;
      disk_inode->magic = INODE_MAGIC;

      //cache_create(sector,disk_inode);
	  //success = true;
	  
	  
          //block_write (fs_device, sector, disk_inode);
		  
	  // Set up the free map in advance
	  /*
	  if(sector == 0)
	  {
        static char zeros[BLOCK_SECTOR_SIZE];
		free_map_allocate(1,&disk_inode->sectors[0]);
		cache_create(disk_inode->sectors[0],zeros);
		disk_inode->sec_alloc = sectors;
	  }
	  */
          if (sectors > 0) 
            {
              static char zeros[BLOCK_SECTOR_SIZE];
              size_t i;
              
				//printf("allocating %i sectors\n",sectors);
              for (i = 0; i < sectors; i++) 
			  {
				if(i < 12)
				{
				free_map_allocate(1,&disk_inode->sectors[i]);
				//disk_inode->sectors[i] = disk_inode->sectors[0] + i;
                //block_write (fs_device, disk_inode->start + i, zeros);

				cache_create(disk_inode->sectors[i], zeros);
				}
				else if(i < 140)
				{

				  // Create the indirect node
				  if(i == 12)
				  {
					free_map_allocate(1,&disk_inode->sectors[i]);
				    cache_create(disk_inode->sectors[i], zeros);
				  }
	              int ofs = 48 + INODE_OFS;
				  block_sector_t iblock = disk_inode->sectors[12];

				  // Read the sector from the iblock
				  block_sector_t vsector = i;
				  vsector = vsector - 12;
				  ofs = 4 * vsector;
				  block_sector_t new_block;
				  //Allocate a new block
				  free_map_allocate(1,&new_block);
				  cache_create(new_block,zeros);

				  //Write this to the indirect node
				  cache_write(iblock,&new_block,ofs,4);

				}
			  }
			  disk_inode->sec_alloc =sectors;
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
  cache_read (inode->sector,&inode->data_start,8,4);
  cache_read (inode->sector,&inode->data_length,0,4);
  cache_read (inode->sector, &inode->data_sectors,4,4);
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
          free_map_release (inode->data_start,
                            bytes_to_sectors (inode->data_length)); 
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

  while (size > 0) 
    {
      /* Disk sector to read, starting byte offset within sector. */
//      block_sector_t sector_idx_old = byte_to_sector (inode, offset);
      block_sector_t sector_idx = byte_to_psector (inode, offset,size);

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

  while (size > 0) 
    {
      /* Sector to write, starting byte offset within sector. */
      //block_sector_t sector_idx_old = byte_to_sector (inode, offset);
      block_sector_t sector_idx = byte_to_psector (inode, offset,size);

	  //if(sector_idx != sector_idx_old)
	   // printf("Writing %d, should be %d\n",sector_idx,sector_idx_old);
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
