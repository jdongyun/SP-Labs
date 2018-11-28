/*
 * mm-implicit.c - an empty malloc package
 *
 * NOTE TO STUDENTS: Replace this header comment with your own header
 * comment that gives a high level description of your solution.
 *
 * @id : 201702071 
 * @name : 정동윤
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "mm.h"
#include "memlib.h"

/* If you want debugging output, use the following macro.  When you hand
 * in, remove the #define DEBUG line. */
#define DEBUG
#ifdef DEBUG
# define dbg_printf(...) printf(__VA_ARGS__)
#else
# define dbg_printf(...)
#endif


/* do not change the following! */
#ifdef DRIVER
/* create aliases for driver tests */
#define malloc mm_malloc
#define free mm_free
#define realloc mm_realloc
#define calloc mm_calloc
#endif /* def DRIVER */

/* single word (4) or double word (8) alignment */
#define ALIGNMENT 8

/* rounds up to the nearest multiple of ALIGNMENT */
#define ALIGN(p) (((size_t)(p) + (ALIGNMENT-1)) & ~0x7)

#define WSIZE 		4
#define DSIZE 		8
#define CHUNKSIZE 	(1<<12) 
//OVERHEAD : Header와 Footer의 크기 합
#define OVERHEAD 	DSIZE

#define MAX(x, y) ((x) > (y)? (x): (y))

//PACK : size, alloc을 bit OR로 한 WORD로 묶음.
//할당 여부를 확인할 땐 0x1과 bit AND하여 확인할 수 있음.
#define PACK(size, alloc)  ((size) | (alloc))

#define GET(p)       (*(unsigned int *)(p))
#define PUT(p, val)  (*(unsigned int *)(p) = (val))

#define GET_SIZE(p)  (GET(p) & ~0x7)
#define GET_ALLOC(p) (GET(p) & 0x1)

#define HDRP(bp)       ((char *)(bp) - WSIZE)
#define FTRP(bp)       ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)

#define NEXT_BLKP(bp)  ((char *)(bp) + GET_SIZE(((char *)(bp) - WSIZE)))
#define PREV_BLKP(bp)  ((char *)(bp) - GET_SIZE(((char *)(bp) - DSIZE)))

#define MAX_FREE 256

static void *extend_heap(size_t words);
static void *coalesce(void *bp);
static void *find_fit(size_t asize);
static void place(void *bp, size_t asize);

static char *heap_listp = 0;
static char *current_block = 0; //First fit 검색 대신, malloc()을 시도한 주소부터 검색하기 위해서 사용
static int free_count = 0; //free()가 일정 횟수 이상 계속되면 current_block을 처음으로 되돌리기 위하여 사용

/*
 * Initialize: return -1 on error, 0 on success.
 */
int mm_init(void) {
	//할당 가능한 주소값을 가져오기
	if((heap_listp = mem_sbrk(4 * WSIZE)) == NULL)
		return -1;

	//그 주소에 해당하는 곳에 0을 넣기(맨 앞은 0)
	PUT(heap_listp, 0);
	//WSIZE(==4)만큼 해당하는 곳에 OVERHEAD(와 할당됨) 넣기(Prologue header 위치)
	PUT(heap_listp + WSIZE, PACK(OVERHEAD, 1));
	//DSIZE(==8)만큼 해당하는 곳에 OVERHEAD(와 할당됨) 넣기(Prologue footer 위치)
	PUT(heap_listp + DSIZE, PACK(OVERHEAD, 1));
	//WSIZE+DSIZE(==12)만큼 해당하는 곳에 할당값 넣기(Epilogue header 위치)
	PUT(heap_listp + WSIZE + DSIZE, PACK(0, 1));
	
	//heap_listp의 ftr과 hdr 사이를 extend_heap 함수로 확장시켜야 하므로 그 위치로 이동
	heap_listp += DSIZE;
	//초기의 current_block은 heap_listp
	current_block = heap_listp;

	//extend_heap의 결과가 NULL이면 실패한 것이므로 -1 반환
	if((extend_heap(CHUNKSIZE / WSIZE)) == NULL)
		return -1;

	return 0;
}

/*
 * malloc
 */
void *malloc (size_t size) {
	
	size_t asize;
	size_t extendsize;
	char *bp;

	if(size == 0)
		return NULL;

	if(size <= DSIZE)
		asize = 2 * DSIZE;
	else
		asize = DSIZE * ((size + (DSIZE) + (DSIZE - 1)) / DSIZE);

	if((bp = find_fit(asize)) != NULL) {
		place(bp, asize);
		current_block = bp;
		return bp;
	}

	extendsize = MAX(asize, CHUNKSIZE);
	if((bp = extend_heap(extendsize / WSIZE)) == NULL)
		return NULL;
	place(bp, asize);
	current_block = bp;
	return current_block;
}

/*
 * free
 */
void free (void *ptr) {
	if(ptr == 0) return;
	size_t size = GET_SIZE(HDRP(ptr));

	PUT(HDRP(ptr), PACK(size, 0));
	PUT(FTRP(ptr), PACK(size, 0));

	coalesce(ptr);

	free_count++; //free() 횟수 증가

	//free_count의 횟수가 일정 한도에 다다르면
	//current_block을 맨 처음으로 가리키고 free_count 초기화
	if(free_count == MAX_FREE) {
		current_block = heap_listp;
		free_count = 0;
	}
}

/*
 * realloc - you may want to look at mm-naive.c
 */
void *realloc(void *oldptr, size_t size)
{
	size_t oldsize;
	void *newptr;

	/* If size == 0 then this is just free, and we return NULL. */
	if(size == 0) {
		free(oldptr);
		return 0;
	}

	/* If oldptr is NULL, then this is just malloc. */
	if(oldptr == NULL) {
		return malloc(size);
	}

	newptr = malloc(size);

	/* If realloc() fails the original block is left untouched  */
	if(!newptr) {
		return 0;
	}

	/* Copy the old data. */
	//mm-naive.c와 달라진 점 - 블록 방식이기 때문에 블록을 이용한 size를 가져와야 하기 때문임.
	oldsize = GET_SIZE(HDRP(oldptr));
	if(size < oldsize) oldsize = size;
	memcpy(newptr, oldptr, oldsize);

	/* Free the old block. */
	free(oldptr);

	return newptr;
}

/*
 * calloc - you may want to look at mm-naive.c
 * This function is not tested by mdriver, but it is
 * needed to run the traces.
 */
void *calloc (size_t nmemb, size_t size) {
	return NULL;
}


/*
 * Return whether the pointer is in the heap.
 * May be useful for debugging.
 */
static int in_heap(const void *p) {
    return p < mem_heap_hi() && p >= mem_heap_lo();
}

/*
 * Return whether the pointer is aligned.
 * May be useful for debugging.
 */
static int aligned(const void *p) {
    return (size_t)ALIGN(p) == (size_t)p;
}

/*
 * mm_checkheap
 */
void mm_checkheap(int verbose) {
}

static void *extend_heap(size_t words) {
	char *bp;
	size_t size;

	//words가 짝수인지 판단해 홀수이면 한 칸 크게 할당
	size = (words % 2) ? (words+1) * WSIZE : words * WSIZE;

	//메모리 할당이 제대로 되지 않으면 NULL 반환
	if ((long)(bp = mem_sbrk(size)) == -1)
		return NULL;

	//bp에 대해서 새로운 할당되지 않은 Prologue header-footer, 
	//할당된 Epilogue header 생성
	PUT(HDRP(bp), PACK(size, 0));
	PUT(FTRP(bp), PACK(size, 0));
	PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1));

	//합병 가능한지 확인
	return coalesce(bp);
}

static void *coalesce(void *bp) {
	//이전 블록, 다음 블록의 할당 여부 저장
	size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(bp)));
	size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));

	size_t size = GET_SIZE(HDRP(bp));

	if(prev_alloc && next_alloc) { 
		//이전 블록 할당 && 다음 블록 미할당
		//블록 병합 없이 current block set 후 current block return
		current_block = bp;
		return current_block;

	} else if(prev_alloc && !next_alloc) {
		//이전 블록 할당 && 다음 블록 미할당
		//다음 블록과 병합

		//크기를 다음 블록의 Header에 저장된 값(다음 블록의 size)과 더함
		size += GET_SIZE(HDRP(NEXT_BLKP(bp)));

		//현재 블록의 Header, Footer에 새로운 size 설정
		PUT(HDRP(bp), PACK(size, 0));
		PUT(FTRP(bp), PACK(size, 0));

	} else if(!prev_alloc && next_alloc) {
		//이전 블록 미할당 && 다음 블록 할당
		//이전 블록과 병합한 뒤 새로운 bp set

		//크기를 이전 블록의 Header에 저장된 값(이전 블록의 size)과 더함
		size += GET_SIZE(HDRP(PREV_BLKP(bp)));

		//이전 블록의 Header에 새로운 size 설정
		PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));

		//현재 블록의 Footer에 새로운 size 설정
                PUT(FTRP(bp), PACK(size, 0));

		//bp를 이전 블록의 Header의 위치로 변경
		bp = PREV_BLKP(bp);

	} else {
		//이전 블록 미할당 && 다음 블록 미할당
		//이전-현재-다음 블록을 모두 병합한 뒤
		//새로운 bp set
		
		//크기를 이전 블록의 Header와 다음 블록의 Footer에 저장된 값(size)과 더함
		size += GET_SIZE(HDRP(PREV_BLKP(bp)));
		size += GET_SIZE(FTRP(NEXT_BLKP(bp)));

		//이전 블록의 Header와 다음 블록의 Footer에 새로운 size 설정
		PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
	       	PUT(FTRP(PREV_BLKP(bp)), PACK(size, 0));

		//bp를 이전 블록의 Header의 위치로 변경
		bp = PREV_BLKP(bp);
	}
	//current block set
	current_block = bp;
	return current_block;
}

/*
 * find_fit - current_block부터의 검색을 이용해 적절한 가용 블록 찾기
 */
static void *find_fit(size_t asize) {
	void *bp = current_block; //bp 초기화

	//bp의 Header에 저장된 size가 0보다 크면 자리가 있는 것이므로
	while(GET_SIZE(HDRP(bp)) > 0) {
		//이 때, 미할당된 블록인지 확인하고
		//이 블록이 asize보다 크거나 같은지(할당 가능 사이즈 여부) 확인
		if(!GET_ALLOC(HDRP(bp)) && (asize <= GET_SIZE(HDRP(bp)))) {
			return bp;
		}
		bp = NEXT_BLKP(bp); //다음 블록으로 넘어감
	}
	//가용 블록 찾기 실패
	return NULL;
}

/*
 * place - 요청한 크기만큼 블록 분할
 */
static void place(void *bp, size_t asize) {
	if(GET_SIZE(HDRP(bp)) < asize + DSIZE + OVERHEAD) {
		PUT(HDRP(bp), PACK(GET_SIZE(HDRP(bp)), 1));
		PUT(FTRP(bp), PACK(GET_SIZE(HDRP(bp)), 1));
	} else {
		size_t remainsize = GET_SIZE(HDRP(bp)) - asize;
		PUT(HDRP(bp), PACK(asize, 1));
		PUT(FTRP(bp), PACK(asize, 1));
		PUT(HDRP(NEXT_BLKP(bp)), PACK(remainsize, 0));
		PUT(FTRP(NEXT_BLKP(bp)), PACK(remainsize, 0));
	}
}
