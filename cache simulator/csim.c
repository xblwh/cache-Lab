/* 
 * csim.c - A cache simulator that can replay traces from Valgrind
 *     and output statistics such as number of hits, misses, and
 *     evictions.  The replacement policy is LRU.
 *
 * Implementation and assumptions:
 *  1. Each load/store can cause at most one cache miss. (I examined the trace,
 *  the largest request I saw was for 8 bytes).
 *  2. Instruction loads (I) are ignored, since we are interested in evaluating
 *  trans.c in terms of its data cache performance.
 *  3. data modify (M) is treated as a load followed by a store to the same
 *  address. Hence, an M operation can result in two cache hits, or a miss and a
 *  hit plus an possible eviction.
 *
 * The function printSummary() is given to print output.
 * Please use this function to print the number of hits, misses and evictions.
 * This is crucial for the driver to evaluate your work. 
 */
#include <getopt.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <assert.h>
#include <math.h>
#include <limits.h>
#include <string.h>
#include <errno.h>
#include "cachelab.h"

#define ADDRESS_LENGTH 64
#define BUFFER_SIZE	1000

/* Type: Memory address */
typedef unsigned long long int mem_addr_t;

/* Type: Cache line
   LRU is a counter used to implement LRU replacement policy  */
typedef struct cache_line {
    char valid;
    mem_addr_t tag;
    unsigned long long int lru;
} cache_line_t;

typedef cache_line_t* cache_set_t;
typedef cache_set_t* cache_t;

/* Globals set by command line args */
int verbosity = 0; /* print trace if set */
int s = 0; /* set index bits */
int b = 0; /* block offset bits */
int E = 0; /* associativity */
char* trace_file = NULL;

/* Derived from command line args */
int S; /* number of sets */
int B; /* block size */

/* Counters used to record cache statistics */
int miss_count = 0;
int hit_count = 0;
int eviction_count = 0;
unsigned long long int lru_counter = 1;

/* The cache we are simulating */
cache_t cache;  	
mem_addr_t set_index_mask;

/* 
 * initCache - Allocate memory, write 0's for valid and tag and LRU
 * also computes the set_index_mask
 */
void initCache()
{
    int i,j;
    //cache = (cache_set_t*) malloc(sizeof(cache_set_t) * S);
    cache = (cache_t) malloc(sizeof(cache_set_t) * S);
    for (i=0; i<S; i++){
        //cache[i]=(cache_line_t*) malloc(sizeof(cache_line_t) * E);
        cache[i]=(cache_set_t) malloc(sizeof(cache_line_t) * E);
        for (j=0; j<E; j++){
            cache[i][j].valid = 0;
            cache[i][j].tag = 0;
            cache[i][j].lru = 0;
        }
    }

    /* Computes set index mask */
    set_index_mask = (mem_addr_t) (pow(2, s) - 1);
}


/* 
 * freeCache - free allocated memory
 */
void freeCache()
{
    int i;
    if( cache ){
    for (i=0; i<S; i++){
		    if( cache[i] )
			    free( cache[i] );
	    }
	    free( cache );
    }
}


/*
 * refresh lru marker of cache lines
 * Object is cache_set, range is [start, end)
 * */
void ptrRefreshLRU( cache_set_t cache_set, int start, int end, unsigned long long lru_flag )
{
	for( ; start < end; start++){
		if( cache_set[start].valid == 1  &&  cache_set[start].lru <= lru_flag ){
			cache_set[start].lru += lru_counter;
		}
	}
}



/*
 * refresh lru marker of cache lines
 * Object is cache_set[], range is [start, end)
 * */
void arrRefreshLRU( cache_set_t cache_set_arr[], int start, int end, unsigned long long lru_flag )
{
	for( ; start < end; start++){
		if(  cache_set_arr[start]->valid == 1  &&  cache_set_arr[start]->lru <= lru_flag ){
			cache_set_arr[start]->lru += lru_counter;
		}
	}
}



/* 
 * accessData - Access data at memory address addr.
 *   If it is already in cache, increast hit_count
 *   If it is not in cache, bring it in cache, increase miss count.
 *   Also increase eviction_count if a line is evicted.
 */
void accessData(mem_addr_t addr)
{
    int i;
    mem_addr_t set_index = (addr >> b) & set_index_mask;
    mem_addr_t tag = addr >> (s+b);

    cache_set_t cache_set = cache[set_index];
    cache_set_t cache_free_subset[E];
    cache_set_t cache_valid_subset[E];
    int valid_index = 0;	//record the number of valid cache line
    int free_index = 0;	//record the number of free cache line
    int hit_flag = 0;	//if access hit, hit_flag equals to 1

    memset( cache_free_subset, 0, sizeof(cache_free_subset) );
    memset( cache_valid_subset, 0, sizeof(cache_valid_subset) );

    //printf("set_index = %lld, tag = %lld\n", set_index, tag );
    

    //find cache line
    for(i=0;  i<E;  i++){

    	if( cache_set[i].valid == 1 ){
		cache_valid_subset[valid_index++] = &(cache_set[i]);	//record valid cache line

		if( cache_set[i].tag == tag ){	// access hit
			if( verbosity == 1 ) printf("hit ");

			hit_flag = 1;
			hit_count ++;

			unsigned long long lru_hit_cache = cache_set[i].lru;
			cache_set[i].lru = 0;	//refresh lru marker of hitted cache line

			//refresh hit markers of other cache lines, based LRU algorithm
			arrRefreshLRU( cache_valid_subset, 0, valid_index-1, lru_hit_cache );
			ptrRefreshLRU( cache_set, i+1, E, lru_hit_cache );

			break;
		}
	}
	else{
		cache_free_subset[free_index++] = &(cache_set[i]);
	}
    }

   

    if( hit_flag == 0 && free_index > 0 ){	//access miss
	if( verbosity == 1 ) printf("miss ");

    	miss_count ++;
	arrRefreshLRU( cache_valid_subset, 0, valid_index, 0 );

	cache_free_subset[0]->valid = 1;
	cache_free_subset[0]->lru = 0;
	cache_free_subset[0]->tag = tag;
    }
    else if( hit_flag == 0  &&  free_index == 0 ){	//access miss and eviction
	if( verbosity == 1 ) printf("miss eviction ");

	miss_count++;
	eviction_count ++;

    	for( i = 0;  i < E;  i++ ){
		if( cache_set[i].lru == E-1 ){
			cache_set[i].lru = 0;
			cache_set[i].tag = tag;
			ptrRefreshLRU( cache_set, 0, i, 0 );
			ptrRefreshLRU( cache_set, i+1, E, 0 );
		}
	}
    }

}


/*
 * replayTrace - replays the given trace file against the cache 
 */
void replayTrace(char* trace_fn)
{
    const char* OPERATIONS  = "MLS";
    char str_address[ ADDRESS_LENGTH ];
    int i;
    int modify_flag = 0;

    char buf[BUFFER_SIZE];
    mem_addr_t addr=0;
    unsigned int len=0;
    FILE* trace_fp = fopen(trace_fn, "r");

    while( fgets( buf, BUFFER_SIZE-1, trace_fp ) ){	//read one line per time, or use fscanf
	modify_flag = 0;
	char *ptr = buf;
	while( *ptr != '\0' && !strchr(OPERATIONS, *ptr) )
		ptr++;

	if( *ptr != '\0' && strchr(OPERATIONS, *ptr) ){
		if( verbosity == 1 ) {
			for(int i = 0;  ptr[i] != '\0';  i++)	//remove '\n' 
				if( ptr[i] == '\n' )
					ptr[i] = '\0';
			printf("%s ", ptr);
		}
		if( *ptr == 'M' ) modify_flag = 1;

		addr = 0;
		ptr += 2;	//find the memory address
		for(len = 0;  *ptr != ',';  len++, ptr++)
			str_address[len] = *ptr;

		for( i = 0;  i < len;  i++){	//string to integer
			addr <<= 4;
			if(str_address[i] >= '0' && str_address[i] <= '9') 
			       addr |= str_address[i] - '0';
			else
				addr |= (str_address[i] - 'a' + 10);
		}

		accessData( addr );
		if( modify_flag == 1 ) accessData( addr );
		if( verbosity == 1 ) printf("\n");
	}

    }

    fclose(trace_fp);
}

/*
 * printUsage - Print usage info
 */
void printUsage(char* argv[])
{
    printf("Usage: %s [-hv] -s <num> -E <num> -b <num> -t <file>\n", argv[0]);
    printf("Options:\n");
    printf("  -h         Print this help message.\n");
    printf("  -v         Optional verbose flag.\n");
    printf("  -s <num>   Number of set index bits.\n");
    printf("  -E <num>   Number of lines per set.\n");
    printf("  -b <num>   Number of block offset bits.\n");
    printf("  -t <file>  Trace file.\n");
    printf("\nExamples:\n");
    printf("  linux>  %s -s 4 -E 1 -b 4 -t traces/yi.trace\n", argv[0]);
    printf("  linux>  %s -v -s 8 -E 2 -b 4 -t traces/yi.trace\n", argv[0]);
    exit(0);
}

/*
 * main - Main routine 
 */
int main(int argc, char* argv[])
{
    char c;

    while( (c=getopt(argc,argv,"s:E:b:t:vh")) != -1){
        switch(c){
        case 's':
            s = atoi(optarg);
            break;
        case 'E':
            E = atoi(optarg);
            break;
        case 'b':
            b = atoi(optarg);
            break;
        case 't':
            trace_file = optarg;
            break;
        case 'v':
            verbosity = 1;
            break;
        case 'h':
            printUsage(argv);
            exit(0);
        default:
            printUsage(argv);
            exit(1);
        }
    }

    /* Make sure that all required command line args were specified */
    if (s == 0 || E == 0 || b == 0 || trace_file == NULL) {
        printf("%s: Missing required command line argument\n", argv[0]);
        printUsage(argv);
        exit(1);
    }

    /* Compute S, E and B from command line args */
    S = (unsigned int) pow(2, s);
    B = (unsigned int) pow(2, b);
 
    /* Initialize cache */
    initCache();

#ifdef DEBUG_ON
    printf("DEBUG: S:%u E:%u B:%u trace:%s\n", S, E, B, trace_file);
    printf("DEBUG: set_index_mask: %llu\n", set_index_mask);
#endif
 
    replayTrace(trace_file);

    /* Free allocated memory */
    freeCache();

    printf("hits:%d misses:%d evictions:%d\n", hit_count, miss_count, eviction_count);
    return 0;
}
