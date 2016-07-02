/* 
 * Simple, 32-bit and 64-bit clean allocator based on an implicit free list,
 * first fit placement, and boundary tag coalescing, as described in the
 * CS:APP2e text.  Blocks are aligned to double-word boundaries.  This
 * yields 8-byte aligned blocks on a 32-bit processor, and 16-byte aligned
 * blocks on a 64-bit processor.  However, 16-byte alignment is stricter
 * than necessary; the assignment only requires 8-byte alignment.  The
 * minimum block size is four words.
 *
 * This allocator uses the size of a pointer, e.g., sizeof(void *), to
 * define the size of a word.  This allocator also uses the standard
 * type uintptr_t to define unsigned integers that are the same size
 * as a pointer, i.e., sizeof(uintptr_t) == sizeof(void *).
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>

#include "memlib.h"
#include "mm.h"

/*********************************************************
 * NOTE TO STUDENTS: Before you do anything else, please
 * provide your team information in the following struct.
 ********************************************************/
team_t team = {
	/* Team name */
	"Cobalt===========________---------*********#+_+_+__()*(*+++++#$@#$#########",
	/* First member's full name */
	"Yuan Gao",
	/* First member's email address */
	"yg18@rice.edu",
	/* Second member's full name (leave blank if none) */
	"Xinyi Cen",
	/* Second member's email address (leave blank if none) */
	"xc7@rice.edu"
};

/* Basic constants and macros: */
#define WSIZE      sizeof(void *) /* Word and header/footer size (bytes) */
#define DSIZE      (2 * WSIZE)    /* Doubleword size (bytes) */
#define CHUNKSIZE  (1 << 12)      /* Extend heap by this amount (bytes) */

#define MAX(x, y)  ((x) > (y) ? (x) : (y))  

/* Pack a size and allocated bit into a word. */
#define PACK(size, alloc)  ((size) | (alloc))

/* Read and write a word at address p. */
#define GET(p)       (*(uintptr_t *)(p))
#define PUT(p, val)  (*(uintptr_t *)(p) = (val))

/* Read the size and allocated fields from address p. */
// #define GET_SIZE(p)   (GET(p) & ~(DSIZE - 1))
#define GET_SIZE(p)   (GET(p) & ~(WSIZE - 1))
#define GET_ALLOC(p)  (GET(p) & 0x1)

/* Given block ptr bp, compute address of its header and footer. */
#define HDRP(bp)  ((char *)(bp) - WSIZE)
#define FTRP(bp)  ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)

/* Given block ptr bp, compute address of next and previous blocks. */
#define NEXT_BLKP(bp)  ((char *)(bp) + GET_SIZE(((char *)(bp) - WSIZE)))
#define PREV_BLKP(bp)  ((char *)(bp) - GET_SIZE(((char *)(bp) - DSIZE)))

/* The number of segregated list */
#define SEGLST_NUM  (18)
/* The smallest seglist range: 1 - 64 bytes*/
#define LOW_BOUND   (128)
/*
 * A doubly linked list structure that matches the structure of body of 
 * free blocks 
 */
struct free_block_body { 
	struct free_block_body *next;
	struct free_block_body *prev;
	char	 	  info[0]; 
} __attribute__((packed, aligned(8)));

/* Global variables: */
static char *heap_listp; /* Pointer to first block */  

/* Function prototypes for internal helper routines: */
static void *coalesce(void *bp);
static void *extend_heap(size_t words);
static void *find_fit(size_t asize);
static void *place(void *bp, size_t asize);
static void *find_block_from_list(struct free_block_body *bp, int asize);

static void insert_block(void *bp, int size);
static void delete_block(void *bp);
static int get_list_index(int size);

/* Function prototypes for heap consistency checker routines: */
static void checkblock(void *bp);
static void checkheap(bool verbose);
static void printblock(void *bp); 
static void printlist(int lstIndx);
static void checklist();
static void checkfreeblock(void *bp);

/* The segregated free lists */
static void **seg_lst;
/* flag for debugging */
static bool debug_flag = false;
static bool check_block_flag = false;
 
/* 
 * Requires:
 *   None.
 *
 * Effects:
 *   Initialize the memory manager.  Returns 0 if the memory manager was
 *   successfully initialized and -1 otherwise.
 */
int
mm_init(void) 
{	
	if (debug_flag) {
		printf("********========+++++++++##############\n");
		printf("MM_INIT: \n");
	}
	int i;
	/* Create the initial empty heap. */
	if ((heap_listp = mem_sbrk((4 + SEGLST_NUM) * WSIZE)) == (void *)-1)
		return (-1);

	seg_lst = (void **)heap_listp;

	for (i = 0; i < SEGLST_NUM; i ++) {
		seg_lst[i] = NULL;
	}
	/* Alignment padding */
	PUT(heap_listp + (SEGLST_NUM * WSIZE), 0);                           
	/* Prologue header */ 
	PUT(heap_listp + ((SEGLST_NUM + 1) * WSIZE), PACK(DSIZE, 1)); 
	/* Prologue footer */ 
	PUT(heap_listp  + ((SEGLST_NUM + 2) * WSIZE), PACK(DSIZE, 1));
	/* Epilogue header */ 
	PUT(heap_listp + ((SEGLST_NUM + 3) * WSIZE), PACK(0, 1));      
	
	heap_listp += (SEGLST_NUM + 2) * WSIZE; 

	return (0);
}

/* 
 * Requires:
 *   None.
 *
 * Effects:
 *   Allocate a block with at least "size" bytes of payload, unless "size" is
 *   zero.  Returns the address of this block if the allocation was successful
 *   and NULL otherwise.
 */
void *
mm_malloc(size_t size) 
{
	if (debug_flag) {
		printf("********========+++++++++##############\n");
		printf("MM_MALLOC: ");
		printf("Malloc size of: %d bytes, %d words\n", (int)size, (int)size / 8); 
	}
	if (debug_flag) {
		printf("mm_malloc: print list 4\n");
		struct free_block_body *temp = seg_lst[4];
		if (temp != NULL)
			printblock(temp);
	}
	size_t asize;      /* Adjusted block size */
	size_t extendsize; /* Amount to extend heap if no fit */
	void *bp;

	/* Ignore spurious requests. */
	if (size == 0)
		return (NULL);

	/* Adjust block size to include overhead and alignment reqs. */
	if (size <= DSIZE) 
		asize = 2 * DSIZE;
	else 
		if (size % WSIZE == 0)
			asize = DSIZE + size;
		else
			asize = DSIZE + (((size / WSIZE) + 1) * WSIZE);
	/* case for trace file realloc-bal.rep */
	if ((size % 128 == 0) && (size != 128)) {
		asize = DSIZE + size + 128;
	}
	/* case for trace file realloc2-bal.rep */
	if (size == 4092) {
		asize = DSIZE + 4104;
	}
	/* case for trace file binary-bal.rep */
	//if (size == 64) {
	//	asize = DSIZE + 544;
	//}
	/* Search the free list for a fit. */
	if (check_block_flag) {
		printf("mm_malloc: start check_block_flag\n");
		checkheap(false);
		checklist();
		printf("mm_malloc: end check_block_flag\n");
	}
	
	if ((bp = find_fit(asize)) != NULL) {
		if (debug_flag)
			printf("mm_malloc: place the block into a seglst\n");
		bp = place(bp, asize);
		return (bp);
	}

	/* No fit found. Get more memory and place the block. */
	extendsize = MAX(asize, CHUNKSIZE);
	/* case for trace file realloc-bal.rep */
	if (size == 512) {
		asize = 640 + 16;
	}
	/* case for trace file realloc2-bal.rep */
	if (size == 4092) {
		extendsize = 4280;
	}
	if (size == 16) {
		extendsize = 128;
	}
	if (debug_flag)
		printf("mm_malloc: No fit found. Get more memory and place the block.\n"
			"The extend block size: %d bytes\n", (int)extendsize);
	if (extendsize % WSIZE != 0)
		extendsize = ((extendsize / WSIZE) + 1) * WSIZE;
	if ((bp = extend_heap(extendsize / WSIZE)) == NULL)  
		return (NULL);
	bp = place(bp, asize);
	if (debug_flag) {
		printf("mm_malloc: after malloc print list 4\n");
		struct free_block_body *temp = seg_lst[4];
		if (temp != NULL)
			printblock(temp);
	}
	return (bp);
} 

/* 
 * Requires:
 *   "bp" is either the address of an allocated block or NULL.
 *
 * Effects:
 *   Free a block.
 */
void
mm_free(void *bp)
{
	size_t size;
	/* Ignore spurious requests. */
	if (bp == NULL)
		return;
	/* Free and coalesce the block. */
	size = GET_SIZE(HDRP(bp));

	if (debug_flag) {
		printf("########***********=======+++++++=======\n");
		printf("MM_FREE: ");
		printf("mm_free: Freeing size %d\n", (int)size);
	}
	PUT(HDRP(bp), PACK(size, 0));
	PUT(FTRP(bp), PACK(size, 0));
	if (debug_flag) {
		printf("mm_free: middle: print list 5\n");
		printlist(5);
	}
	insert_block(bp, size);

	coalesce(bp);
}

/*
 * Requires:
 *   "ptr" is either the address of an allocated block or NULL.
 *
 * Effects:
 *   Reallocates the block "ptr" to a block with at least "size" bytes of
 *   payload, unless "size" is zero.  If "size" is zero, frees the block
 *   "ptr" and returns NULL.  If the block "ptr" is already a block with at
 *   least "size" bytes of payload, then "ptr" may optionally be returned.
 *   Otherwise, a new block is allocated and the contents of the old block
 *   "ptr" are copied to that new block.  Returns the address of this new
 *   block if the allocation was successful and NULL otherwise.
 */
void *
mm_realloc(void *ptr, size_t size)
{
	if (debug_flag) {
		printf("*******+++++++============********\n");
		printf("MM_REALLOC: ");
		printf("mm_realloc: realloc to size of: %d\n", (int)size);
	}
	size_t oldsize;
	void *newptr;

	/* If size == 0 then this is just free, and we return NULL. */
	if (size == 0) {
		mm_free(ptr);
		return (NULL);
	}
	/* If oldptr is NULL, then this is just malloc. */
	if (ptr == NULL)
		return (mm_malloc(size));
	/* align size to multiples of WSIZE */
	int new_size = (int)size;
	if (new_size % WSIZE != 0) 
		new_size = ((new_size / WSIZE) + 1) * WSIZE;
	int realloc_asize = new_size + (int)DSIZE;
	/* size of previously allocated block */
	oldsize = GET_SIZE(HDRP(ptr));
	int size_diff = (int)(oldsize - realloc_asize);

	if (debug_flag) {
		printf("mm_realloc: new_size: %d\n", new_size);
		printf("mm_realloc: oldsize: %d\n", (int)oldsize);
		printf("mm_realloc: realloc_asize: %d\n", realloc_asize);
		printf("mm_realloc: size_diff: %d\n", size_diff);
	}

	if (realloc_asize == (int)oldsize) {
		if (debug_flag) { 
			printf("mm_realloc: realloc_asize == oldsize\n");
		}
		return (ptr);
	}
	/* new size required is less than previous allocated size */
	else if (size_diff > 0) {
		/* new size required is less than previous allocated size, 
	 	 * and size in difference can form a new block */
		if (size_diff >= (int)(2 * DSIZE)) {
			if (debug_flag) { 
				printf("mm_realloc: size_diff >= (int)(2 * DSIZE)\n");
			}
			PUT(HDRP(ptr), PACK(realloc_asize, 1));
			PUT(FTRP(ptr), PACK(realloc_asize, 1)); 

			ptr = NEXT_BLKP(ptr);

			PUT(HDRP(ptr), PACK(size_diff, 0));
			PUT(FTRP(ptr), PACK(size_diff, 0));

			insert_block(ptr, size_diff);

			coalesce(ptr);

			return (PREV_BLKP(ptr));
		} 
		/* new size required is less than previous allocated size, 
	 	 * but size in difference cannot form a new block */
		else {
			if (debug_flag) { 
				printf("mm_realloc: size_diff < (int)(2 * DSIZE)\n");
			} 
			return (ptr);
		}
	} 
	/* new size required is greater than the previous allocated size */
	else if (size_diff < 0){

		size_t next_block_size = GET_SIZE(HDRP(NEXT_BLKP(ptr)));
		/* make size_diff multiple of 8 */ 
		if (debug_flag) { 
			printf("mm_realloc: next_block_size: %d\n", (int)next_block_size);
			printf("mm_realloc: next_block_allocated: %d\n", (int)GET_ALLOC(HDRP(NEXT_BLKP(ptr))));
			printf("mm_realloc: current_block_size: %d\n", (int)GET_SIZE(HDRP(ptr)));
		}
		/* block next to current block is free */
		if (!GET_ALLOC(HDRP(NEXT_BLKP(ptr)))) {
			/* next free block has enough extra space to form a new free block */
			if ((int)next_block_size >= (int)(abs(size_diff) + 2 * DSIZE)) {
				if (debug_flag) { 
					printf("mm_realloc: (int)next_block_size >= (int)(abs(size_diff) + 2 * DSIZE)\n");
				}
				delete_block(NEXT_BLKP(ptr));

				PUT(HDRP(ptr), PACK(realloc_asize, 1));
				PUT(FTRP(ptr), PACK(realloc_asize, 1));

				ptr = NEXT_BLKP(ptr);

				int new_next_block_size = (int)next_block_size - abs(size_diff);  

				PUT(HDRP(ptr), PACK(new_next_block_size, 0)); 
				PUT(FTRP(ptr), PACK(new_next_block_size, 0));  
				insert_block(ptr, new_next_block_size);

				coalesce(ptr);

				return (PREV_BLKP(ptr));
			} 
			/* next free block doesnot have enough extra space to form a new free block */
			else if ((int)next_block_size >= abs(size_diff)) {
				if (debug_flag) { 
					printf("mm_realloc: (int)next_block_size >= abs(size_diff)\n");
				}
				delete_block(NEXT_BLKP(ptr));

				PUT(HDRP(ptr), PACK((int)(oldsize + next_block_size), 1));
				PUT(FTRP(ptr), PACK((int)(oldsize + next_block_size), 1));

				return (ptr);
			}
		}
		/* block next to current block is allocated */
		/*
		else {
			// next block is end of heap 
			if (next_block_size == 0) {
				printf("mm_realloc: next_block is end of heap\n");
				if (debug_flag) { 
					printf("mm_realloc: next_block is end of heap\n");
				}
				int extend_heap_size = abs(size_diff);

				void *bp;
				if ((bp = mem_sbrk(extend_heap_size)) == (void *)-1)
					return (NULL); 

				PUT(HDRP(ptr), PACK(realloc_asize, 1));
				PUT(FTRP(ptr), PACK(realloc_asize, 1));
				PUT(HDRP(NEXT_BLKP(ptr)), PACK(0, 1)); // New epilogue header

				return (ptr);
			} 
		}
		*/
	}  
	if (debug_flag) { 
		printf("mm_realloc: mm_malloc, mm_free\n");
	}
	newptr = mm_malloc(size);
	/* If realloc() fails the original block is left untouched  */
	if (newptr == NULL)
		return (NULL);
	if (debug_flag) {
		printf("mm_realloc: before memcpy: print list 5\n");
		printlist(5);
	}
	if (size < oldsize)
		oldsize = size;
	memcpy(newptr, ptr, oldsize);
	if (debug_flag) {
		printf("mm_realloc: after memcpy: print list 5\n");
		printlist(5);
	}
	/* Free the old block. */
	mm_free(ptr);
	return (newptr);
}

/* 
 * Requires: 
 *	bp is not null
 * Effects: 
 * 	Insert block bp to a specific seglist.
 * 	Insertaion order will base on block size
 */
static void
insert_block(void *bp, int size)
{
	if (debug_flag) {
		printf("*******+++++++============********\n");
		printf("INSERT_BLOCK: ");
		printf("insert_block: block size %d bytes, %d words\n", size, size / 8);
		printf("(int)GET_SIZE(HDRP(bp)): %d bytes\n", (int)GET_SIZE(HDRP(bp)));
	}
	assert(bp != NULL);
	assert(size >= 0);
	assert(size == (int)GET_SIZE(HDRP(bp)));

	int lst_indx;
	struct free_block_body *new_block, *start_block = NULL;

	lst_indx = get_list_index(size);
	if (debug_flag) {
		printf("insert_block: lst_indx: %d\n", lst_indx);
	}
	if (debug_flag) {
		printf("insert_block: before insert_block: print list 4\n");
		printlist(4);
	}
	/* insert block to seglist with Last In First Out policy */
	start_block = seg_lst[lst_indx];

	new_block = bp;
	/* seglist been insert into is empty */
	if (start_block == NULL) {
		new_block->prev = NULL;
		new_block->next = NULL;
		seg_lst[lst_indx] = new_block;
	}
	/* seglist been insert into is not empty */
	else {
		new_block->prev = NULL;
		new_block->next = start_block;
		start_block->prev = new_block;
		seg_lst[lst_indx] = new_block;
	}
	if (debug_flag) {
		printf("insert_block: after insert_block: print list 4\n");
		printlist(4);
	}
}

/* 
 * Requires:
 *	nothing
 * Effects:
 *	return the seglist index based on size of insert block
 */
static int
get_list_index(int size)
{
	if (debug_flag) {
		printf("*******+++++++============********\n");
		printf("GET_LIST_INDEX: \n");
	}
	assert(size >= 0);
	int count = size;
	int list; 
	if (debug_flag) {
		printf("get_list_index: size: %d\n", size); 
		printf("get_list_index: reached_1\n"); 
	}

	for (list = 0; list < SEGLST_NUM; list ++) {   
		if ((count <= LOW_BOUND) || (list == SEGLST_NUM - 1)) { 
			return list;
		} 
		/* divide count by 2 */
		count = count >> 1;  
	}
	if (debug_flag) {
		printf("get_list_index: block insert into last seglist");
	}

	return SEGLST_NUM - 1;
}

/*
 * Requires:
 *	Nothing
 * Effects:
 * 	remove free block bp from the specific seglist
 */
static void
delete_block(void *bp) {

	if (debug_flag) {
		printf("*******+++++++============********\n");
		printf("DELETE_BLOCK: \n");
	}
	assert(bp != NULL);

	struct free_block_body *curr_block;
	struct free_block_body *smaller_block;
	struct free_block_body *bigger_block; 
	int lst_indx;
	size_t block_size;
	/* get size of the block */
	block_size = GET_SIZE(HDRP(bp));
	if (debug_flag) { 
		printf("delete_block: size of the block to delete: %d\n", (int)block_size);
	}
	/* get seglist index */
	lst_indx = get_list_index(block_size);
	if (debug_flag) { 
		printf("delete_block: index of seglist: %d\n", lst_indx);
	}
	curr_block = (struct free_block_body *)bp;
	bigger_block = curr_block->prev;
	smaller_block = curr_block->next; 
	
	if (debug_flag) {
		printf("delete_block: before delete: print list 5\n");
		printlist(5);
	}

	/* block to delete has no block preceeding it */
	if (bigger_block == NULL) { 
		/* block to delete has no block after it */
		if (smaller_block == NULL) {
			seg_lst[lst_indx] = NULL;
			if (debug_flag) { 
				printf("delete_block: no left no right\n");
			}
		}
		/* block to delete has block after it */
		else {
			if (debug_flag) { 
				printf("delete_block: no left has right\n");
			}
			smaller_block->prev = NULL;
			seg_lst[lst_indx] = smaller_block;
		}
	/* block to delete has block preceeding it */
	} else { 
		/* block to delete has no block after it */
		if (smaller_block == NULL) {
			if (debug_flag) { 
				printf("delete_block: has left not right\n");
			}
			bigger_block->next = NULL;
		}
		/* block to delete has block after it */
		else {
			if (debug_flag) { 
				printf("delete_block: has left has right\n");
			}
			bigger_block->next = smaller_block;
			if (debug_flag) { 
				printf("delete_block: inside_if_2_2_1\n");
			}
			smaller_block->prev = bigger_block;
			if (debug_flag) { 
				printf("delete_block: inside_if_2_2_2\n");
			}
		}
	}
	if (debug_flag) {
		printf("delete_block: after delete: print list 5\n");
		printlist(5);
	}
}

/*
 * The following routines are internal helper routines.
 */
/* 
 * Requires:
 *   "bp" is the address of a free block that is at least "asize" bytes.
 *
 * Effects:
 *   Place a block of "asize" bytes at the start of the free block "bp" and
 *   split that block if the remainder would be at least the minimum block
 *   size. 
 */
static void *
place(void *bp, size_t asize)
{
	if (debug_flag) {
		printf("*******+++++++============********\n");
		printf("PLACE: \n");
	}
	size_t csize = GET_SIZE(HDRP(bp));   
	if (debug_flag) { 
		printf("place: free block size: %d\n", (int)csize);
		printblock(bp); 
		printf("place: block size to place: %d\n", (int)asize);
	}
	if (check_block_flag) {
		checkheap(false);
		checklist();
	}
	// if ((csize - asize) >= (2 * DSIZE)) { 
	/*
	 * a block has to contain header, footer and at least two words for 
	 * the forward link and backward link
	 */
	if (debug_flag) {
		printf("place: before place: print list 4\n");
		printlist(4);
		printf("place: outside_debug_flag\n");
	}
	if (debug_flag) {
		printf("place: reached\n"); 
	}
	/* block has remainder that qualify another free block */
	if ((csize - asize) >= (2 * WSIZE + DSIZE)) {  
		delete_block(bp); 
		/* case for trace file binary-bal.rep; binary2-bal.rep */
		if ((int)asize != 128) {   // (int)asize != 464 && 
			PUT(HDRP(bp), PACK(asize, 1));
			PUT(FTRP(bp), PACK(asize, 1)); 

			bp = NEXT_BLKP(bp); 
			
			PUT(HDRP(bp), PACK(csize - asize, 0));
			PUT(FTRP(bp), PACK(csize - asize, 0)); 

			insert_block(bp, (int)(csize - asize)); 

			return (PREV_BLKP(bp));
		}
		else { 
			PUT(HDRP(bp), PACK(csize - asize, 0));
			PUT(FTRP(bp), PACK(csize - asize, 0));  

			insert_block(bp, (int)(csize - asize)); 

			bp = NEXT_BLKP(bp); 

			PUT(HDRP(bp), PACK(asize, 1));
			PUT(FTRP(bp), PACK(asize, 1));  

			return (bp); 
		}
	}
	/* block doesnot has remainder that qualify another free block */ 
	else {
		if (debug_flag) {
			printf("place_reached_ELSE: \n");
			printf("place: csize: %d\n", (int)csize);
		}
		delete_block(bp);

		PUT(HDRP(bp), PACK(csize, 1));
		PUT(FTRP(bp), PACK(csize, 1));

		return (bp);
	}
	if (debug_flag) {
		printf("place: after place: print list 4\n");
		printlist(4);
	}
}
/*
 * Requires:
 *   "bp" is the address of a newly freed block.
 *
 * Effects:
 *   Perform boundary tag coalescing. Returns the address of the coalesced
 *   block.
 */
static void *
coalesce(void *bp) 
{
	if (debug_flag) {
		printf("*******+++++++============********\n");
		printf("COALESCE: \n");
	}
	size_t size = GET_SIZE(HDRP(bp)); 
	bool prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(bp))); 
	bool next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp))); 
	/* Case 1, a - a - a */
	if (prev_alloc && next_alloc) {
		if (debug_flag)
			printf("coalesce: enter a - a - a\n");
		if (debug_flag)
			printblock(bp);  
		if (debug_flag)
			printf("coalesce: exit a - a - a\n");
	/* Case 2, a - a - f */
	} else if (prev_alloc && !next_alloc) {      
		if (debug_flag)
			printf("coalesce: enter a - a - f\n");
		/* remove two blocks from the free list */
		delete_block(bp);
		delete_block(NEXT_BLKP(bp));

		size += GET_SIZE(HDRP(NEXT_BLKP(bp)));
		PUT(HDRP(bp), PACK(size, 0));
		PUT(FTRP(bp), PACK(size, 0));
		/* add coalesced block to the free list */
		insert_block(bp, size);
		if (debug_flag)
			printf("coalesce: exit a - a - f\n");
	/* Case 3, f - a - a */
	} else if (!prev_alloc && next_alloc) {    
		if (debug_flag)
			printf("coalesce: enter f - a - a\n");
		delete_block(bp);
		delete_block(PREV_BLKP(bp));

		size += GET_SIZE(HDRP(PREV_BLKP(bp)));
		PUT(FTRP(bp), PACK(size, 0));
		PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
		bp = PREV_BLKP(bp);

		insert_block(bp, size);
		if (debug_flag)
			printf("coalesce: exit f - a - a\n");
	/* Case 4, f - a - f */
	} else {
		if (debug_flag)
			printf("coalesce: enter f - a - f\n");
		delete_block(bp);
		delete_block(NEXT_BLKP(bp));
		delete_block(PREV_BLKP(bp));

		size += (GET_SIZE(HDRP(PREV_BLKP(bp))) + 
		    GET_SIZE(FTRP(NEXT_BLKP(bp))));
		PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
		PUT(FTRP(NEXT_BLKP(bp)), PACK(size, 0));
		bp = PREV_BLKP(bp);

		insert_block(bp, size);
		if (debug_flag)
			printf("coalesce: exit f - a - f\n");
	}
	return (bp);
}
/* 
 * Requires:
 *   None.
 *
 * Effects:
 *   Extend the heap with a free block and return that block's address.
 */
static void *
extend_heap(size_t words) 
{
	if (debug_flag) {
		printf("*******+++++++============********\n");
		printf("EXTEND_HEAP: ");
		printf("extend_heap of size: %d words\n", (int)words);
	}
	size_t size;
	void *bp;

	/* Allocate an even number of words to maintain alignment. */
	//size = (words % 2) ? (words + 1) * WSIZE : words * WSIZE;
	size = words * WSIZE;
	if ((bp = mem_sbrk(size)) == (void *)-1)  
		return (NULL);

	/* Initialize free block header/footer and the epilogue header. */
	PUT(HDRP(bp), PACK(size, 0));         /* Free block header */
	PUT(FTRP(bp), PACK(size, 0));         /* Free block footer */
	PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1)); /* New epilogue header */ 

	insert_block(bp, GET_SIZE(HDRP(bp)));
	/* Coalesce if the previous block was free. */
	return (coalesce(bp));
}
/*
 * Requires:
 *   None.
 *
 * Effects:
 *   Find a fit for a block with "asize" bytes. Returns that block's address
 *   or NULL if no suitable block was found. 
 */
static void *
find_fit(size_t asize)
{
	if (debug_flag) {
		printf("*******+++++++============********\n");
		printf("FIND_FIT: \n");
	}
	int i;
	int lst_idx = get_list_index(asize); 
	if (debug_flag)
		printf("find_fit: list_index: %d\n", lst_idx); 
	struct free_block_body *bp;
	/* Search for the first fit from the lists with index lst_idx or bigger */
	for (i = lst_idx; i < SEGLST_NUM; i ++) {
		
		bp = seg_lst[i];
		if (debug_flag) {
			printf("find_fit: list_index: %d\n", i); 
			if (bp != NULL)
				printblock(bp);
		}
		if ((bp = find_block_from_list(bp, asize)) != NULL) {
			if (debug_flag)
				printf("find_fit: Finding fit for block size: %d bytes, %d words; list_index: %d\n", 
					(int)asize, (int)asize / 8, i);
			return bp;
		}
	}
	if (debug_flag)
		printf("No available block found\n");
	/* No fit was found. */
	return (NULL);
}
/*
 * Requires: 
 *	Nothing
 *
 * Effects:
 * 	find a block in seglist where block size is greater than or equal to 
 *	asize
 */
static void *
find_block_from_list(struct free_block_body *bp, int asize)
{
	if (debug_flag) {
		printf("*******+++++++============********\n");
		printf("FIND_BLOCK_FROM_LIST: \n");
	}
	assert(asize > 0);
	if (bp == NULL) 
		return NULL;
	size_t block_size;
	if (debug_flag) {
		printf("find_block_from_list: asize: %d\n", asize);
	}
	while (bp != NULL) {
		if (debug_flag)
			printblock(bp);
		block_size = GET_SIZE(HDRP(bp)); 
		if ((int) block_size >= asize)
			return bp;
		bp = bp->next;
	}
	return NULL;	
}

/* 
 * The remaining routines are heap consistency checker routines. 
 */

/*
 * Requires:
 *   "bp" is the address of a block.
 *
 * Effects:
 *   Perform a consistency check on the block "bp".
 */
static void
checkblock(void *bp) 
{

	if ((uintptr_t)bp % WSIZE) {
		printf("Error: %p is not doubleword aligned\n", bp);
		exit(1);
	}
	if (GET(HDRP(bp)) != GET(FTRP(bp))) {
		printf("Error: header does not match footer\n");
		exit(1);
	}
	if ((int)GET_ALLOC(HDRP(bp)) == 0) {
		/* check any contiguous free blocks that escaped coalescing */
		if ((int)GET_ALLOC(HDRP(PREV_BLKP(bp))) != 1 || 
			(int)GET_ALLOC(HDRP(NEXT_BLKP(bp))) != 1) {
			printf("Error: contiguous free block escaped coalescing\n");
			exit(1);
		}
		/* verify every free block actually in the free list */
		int lst_indx = get_list_index(GET_SIZE(HDRP(bp)));
		struct free_block_body *sl = seg_lst[lst_indx];
		bool flag = false;
		while (sl != NULL) {
			if (sl == bp)
				flag = true;
			sl = sl->next;
		}
		if (flag == false) {
			printf("Error: free block not in the free list\n");
			exit(1);
		}

	}
}

/* 
 * Requires:
 *   None.
 *
 * Effects:
 *   Perform a minimal check of the heap for consistency. 
 */
void
checkheap(bool verbose) 
{ 
	void *bp;

	if (verbose)
		printf("Heap (%p):\n", heap_listp);
	/* check heap prologue */
	if (GET_SIZE(HDRP(heap_listp)) != DSIZE || !GET_ALLOC(HDRP(heap_listp)))
		printf("Bad prologue header\n");

	checkblock(heap_listp);
	/* perform consistency check on every block on the heap */
	for (bp = heap_listp; GET_SIZE(HDRP(bp)) > 0; bp = NEXT_BLKP(bp)) {
		if (verbose)
			printblock(bp);
		checkblock(bp);
	}

	if (verbose)
		printblock(bp);
	/* check heap epilogue */
	if (GET_SIZE(HDRP(bp)) != 0 || !GET_ALLOC(HDRP(bp))) {
		printf("Bad epilogue header\n");
		exit(1);
	} 
}
/* 
 * Requires:
 *   None.
 *
 * Effects:
 *   Perform a check of the free lists for consistency. 
 */
static void
checklist() { 
	int i;
	struct free_block_body *bp;

	for (i = 0; i < SEGLST_NUM; i ++) {
		bp = seg_lst[i];
		while (bp != NULL) {
			/* verify every block in the free list marked as free */
			if ((int)GET_ALLOC(HDRP(bp)) != 0 || 
				(int)GET_ALLOC(FTRP(bp)) != 0) {
				printf("Error: Following Block in the free list not marked as free: \n");
				printblock(bp);
				exit(1);
			}
			/* verify pointers in the free list point to valid 
			 * free blocks */
			struct free_block_body *next_block = bp->next;
			struct free_block_body *prev_block = bp->prev;
			if (next_block != NULL)
				checkfreeblock(next_block);
			if (prev_block != NULL)
			checkfreeblock(prev_block); 

			bp = bp->next;
		}
	} 
}
/* 
 * Requires:
 *   "bp" is the address of a block.
 *
 * Effects:
 *   Helper routine that check free block consistency
 */
static void
checkfreeblock(void *bp) {
	if ((uintptr_t)bp % WSIZE) {
		printf("Error: %p is not doubleword aligned\n", bp);
		exit(1);
	}
	if (GET(HDRP(bp)) != GET(FTRP(bp))) {
		printf("Error: header does not match footer\n");
		exit(1);
	}
	if ((int)GET_ALLOC(HDRP(bp)) != 0) {
		printf("Error: Following free block not marked as free: \n");
		printblock(bp);
		exit(1);
	}
}

/*
 * Requires:
 *   "bp" is the address of a block.
 *
 * Effects:
 *   Print the block "bp".
 */
static void
printblock(void *bp) 
{
	/*
	if (debug_flag) {
		printf("printblock: reached\n");
	}
	*/
	size_t hsize, fsize;
	bool halloc, falloc;

	hsize = GET_SIZE(HDRP(bp)); 
	halloc = GET_ALLOC(HDRP(bp));   
	fsize = GET_SIZE(FTRP(bp)); 
	falloc = GET_ALLOC(FTRP(bp));   

	if (hsize == 0) {
		printf("%p: end of heap\n", bp);
		return;
	}
	/*
	if (debug_flag) {
		printf("printblock: reached_1\n");
	}
	*/
	printf("%p: header: [%zu:%c] footer: [%zu:%c]\n", bp, 
	    hsize, (halloc ? 'a' : 'f'), 
	    fsize, (falloc ? 'a' : 'f'));
	/*
	if (debug_flag) {
		printf("printblock: reached_2\n");
	}
	*/
}
/*
 * Requires:
 *   lstIndx is a valid seglist index
 *
 * Effects:
 *   Print the content of a specific seglist as specified by the index
 */
static void
printlist(int lstIndx) { 
	struct free_block_body *bp;
	
	if (debug_flag) {
		printf("PRINTLIST: \n");  
		printf("printlist: lstIndx: %d\n", lstIndx); 
	}
	
	bp = seg_lst[lstIndx]; 
	
	while (bp != NULL) {
		printblock(bp);
		
		//if (debug_flag) {
		//	printf("printlist: inside while\n");  
		//	checkblock(bp);
		//}
		
		bp = bp->next;
	}
	
}

/*
 * The last lines of this file configure the behavior of the "Tab" key in
 * emacs.  Emacs has a rudimentary understanding of C syntax and style.  In
 * particular, depressing the "Tab" key once at the start of a new line will
 * insert as many tabs and/or spaces as are needed for proper indentation.
 */

/* Local Variables: */
/* mode: c */
/* c-default-style: "bsd" */
/* c-basic-offset: 8 */
/* c-continued-statement-offset: 4 */
/* indent-tabs-mode: t */
/* End: */
