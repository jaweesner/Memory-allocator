#include "mem.h"
#include <assert.h>
#include <stdio.h>
#include <sys/mman.h>
#include <fcntl.h>

#define MAGIC 1234567

typedef struct stnode_t node_t;
//Free memory structure
struct stnode_t {
	int size;
	node_t *next;
};

//header strcture
typedef struct {
	int size;
	int valCheck; //this should always be the magic number (validation check)
}header;

//Global Variables
int memHasInit = 0;
int policyType = 0;
node_t* head = NULL;

/*
Policy types:
P_BESTFIT  (1)
P_WORSTFIT (2)
P_FIRSTFIT (3)

Get a chunk of memory from the OS to use for all other allocation. Will only be done once per program run.
*/

int Mem_Init(int region_size, int policy) {
	//check valid user input
	if (region_size <= 0 || policy > 3 || policy < 1 || memHasInit != 0) {
		return -1;
	}
	int page = getpagesize();
	//make the region size divisible by page size
	if (region_size % page != 0) {
		region_size += page - (region_size % page);
	}
	//Initiate the mmap'ed memory. Stolen unabashedly from class readings and man7.org.
	// open the /dev/zero device
	int fd = open("/dev/zero", O_RDWR);
	head = mmap(NULL, region_size, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
	if (head == MAP_FAILED) {
		perror("mmap");
		return -1;
	}
	close(fd);
	//Define the first node of the memory list
	head->size = region_size - sizeof(node_t);
	head->next = NULL;
	

	//set the globals now that we have successfully set aside the memory.
	policyType = policy;
	memHasInit = 1;
	return 0;

}

/*
* Splits the given free memory node into an allocated memory segment and a memory node segment
* Does not return any values, but it sets the given node pointer to the new node location
* Sets the header values for the allocated memory as well
* ptr to allocated memory (sans header) will be node-size always
* next value should not change
*/
void split(node_t** nodep, int size) {
	if (*nodep == NULL) {
		return;
	}
	node_t * p = *nodep; //pointer to start of memory
	*nodep = (node_t*)(((char *)*nodep) + size + sizeof(header));  //change the pointer to someplace different
	(*nodep)->size = (p->size - size - sizeof(header)); //move the struct fields over
	(*nodep)->next = p->next;
	header* ph = (header *)p; //set the pointer to the beginning of memory to be a pointer to a header

							  //set header values
	ph->size = size;
	ph->valCheck = MAGIC;
}


/*
*helper function to find the location at the end of the given node
*
*/
void* endOfNode(node_t* nodep) {
	void* endNode;
	endNode = (char*)nodep + nodep->size + sizeof(node_t);
	return endNode;
}



/*
Mem_Alloc takes as input the size in bytes of the object to be allocated and returns a pointer to the start of that object.
The function returns NULL if there is not enough contiguous free space within region_size allocated by Mem_Init to satisfy this request.
Mem_Alloc() should return 4 - byte aligned chunks of memory.
*/
void* Mem_Alloc(int size) {

	//If memory has never been allocated, or policy type has not been defined, quit
	assert(memHasInit == 1);
	assert(policyType > 0 && policyType < 4);
	//don't allow sizes <=0
	assert(size > 0);

	//pointer to return, node pointer for iteration
	void* ptr;

	//if size is not divisible by 4, make it so 
	//Header will always be divisible by 4, so adding will not change allocated size
	//mmap returns page aligned memory (which is also 4 byte aligned)
	if (size % 4 != 0) {
		size += 4 - (size % 4);
	}

	//allocate memory 

	//If there is only one node, don't worry about policy, just allocate
	if (head != NULL && head->next == NULL) {
		//if the requested amount doesn't fit return NULL
		if (size + sizeof(header) > head->size) {
			return NULL;
		}

		//split the single node into allocated space
		split(&head, size);
		ptr = (char *)head - size; //pointer to allcated memory
		return ptr;
	}
	//Otherwise, if there are multiple nodes we must consider policy
	//1:best 2:worst 3:first

	node_t* nodeIt = head;  //set up ptr for iteration
	node_t* nodePrev = NULL; //set up previous node var to maintain linkage
	int match; //flag if there is a node large enough to match request
	node_t* nodeMinmax = head; //holds the current node with min/max value
	node_t* nodePrevMinmax = NULL; //holds the node previous to the one wih min/max value

	switch (policyType) {
	case P_BESTFIT:
		//find the biggest node
		if (size + sizeof(header) <= nodeIt->size) {
			match = 1;
		}
		else {
			match = 0;
		}
		while (nodeIt != NULL) {
			if ((nodeIt->size > nodeMinmax->size) && (size + sizeof(header) <= nodeIt->size)) {
				nodeMinmax = nodeIt;
				nodePrevMinmax = nodePrev;
				match = 1;
			}
			//increment
			nodePrev = nodeIt;
			nodeIt = nodeIt->next;
		}
		//if there was not a match, return null
		if (!match) {
			return NULL; //if we hit this, no memory exists big enough for request 
		}

		split(&nodeMinmax, size); //split given node. 
		ptr = (char *)nodeMinmax - size; //pointer to allocated memory

										 //update the list pointers (head/prev node)
		if (nodePrevMinmax == NULL) {
			head = nodeMinmax;
		}
		else {
			nodePrevMinmax->next = nodeMinmax;
		}
		return ptr;

	case P_WORSTFIT:
		//find the smallest node that is big enough for the memory
		if (size + sizeof(header) <= nodeIt->size) {
			match = 1;
		}
		else {
			match = 0;
		}
		while (nodeIt != NULL) {
			if ((nodeIt->size < nodeMinmax->size) && (size + sizeof(header) <= nodeIt->size)) {
				nodeMinmax = nodeIt;
				nodePrevMinmax = nodePrev;
				match = 1;
			}
			//increment
			nodePrev = nodeIt;
			nodeIt = nodeIt->next;
		}
		//if there was not a match, return null
		if (!match) {
			return NULL; //if we hit this, no memory exists big enough for request 
		}

		split(&nodeMinmax, size); //split given node. 
		ptr = (char *)nodeMinmax - size; //pointer to allocated memory

										 //update the list pointers (head/prev node)
		if (nodePrevMinmax == NULL) {
			head = nodeMinmax;
		}
		else {
			nodePrevMinmax->next = nodeMinmax;
		}
		return ptr;

	case P_FIRSTFIT:
		while (nodeIt != NULL) {
			//act on the first node found
			if (size + sizeof(header) >= nodeIt->size) {
				split(&nodeIt, size); //split given node. 
				ptr = (char *)nodeIt - size; //pointer to allocated memory

											 //update the list pointers (head/prev node)
				if (nodePrev == NULL) {
					head = nodeIt;
				}
				else {
					nodePrev->next = nodeIt;
				}
				return ptr;
			}
			nodePrev = nodeIt;
			nodeIt = nodeIt->next;
		}
		return NULL; //if we hit this, no memory exists big enough for request

	}
	return NULL; //should never hit this
}

/*
Inserts new node in order, sorting on pointer location
*/
void insertNode(node_t* inNode) {
    node_t *prev, *nextNode;
    prev = NULL;
    nextNode = head;
	//go through list until we find correct position
        while(nextNode != NULL && nextNode>=inNode){
            prev = nextNode;
            nextNode = nextNode->next;
        }
		//if through entire list, add to end of list
        if(nextNode==NULL){
            prev->next = inNode;
        } else{
			//if previous is null, that means our new node is the new head
            if(prev==NULL) {
				inNode->next = head;
				head = inNode;
			//else insert in middle list
            } else {
				inNode->next = prev->next;
				prev->next = inNode;
            }            
        }   
    
}
/*
Merges given node to other nodes in list, if appropriate
only one merge is done at any time. and the inNode pointer may be changed to reflect the new node
returns 1 if node was merged, 0 is not merged
*/
int mergeNode(node_t** inNode) {
	int merge = 0;
	node_t *prevN, *nextN, *curr;
	prevN = NULL;
	curr = *inNode; //readability variable

	//save an iteration to check if the next node merges first
	if ((int)curr - curr->size == (int)curr->next) {
		//add node sizes together
		curr->size = curr->size + curr->next->size;
		//update node
		curr->next = curr->next->next;
		return 1;
	}

	//if it didn't merge, iterate through to get the previous node
	nextN = head;
	while (nextN != curr) {
		prevN = nextN;
		nextN = nextN->next;
	}

	//merge if apprpriate
	if ((int)prevN == (int)curr - curr->size) {
		prevN->size = curr->size + prevN->size;
		prevN->next = curr->next;
		*inNode = prevN;
		return 1;
	}

	//if we didn't merge at all, return 0
	return 0;
}
/*
Mem_Free() frees the memory object that ptr points to.
If ptr is NULL, then no operation is performed.
The function returns 0 on success and -1 if the ptr was not allocated by Mem_Alloc().
*/
int Mem_Free(void *ptr) {

	//if given pointer is null, do nothing
	if (ptr == NULL) return -1;
	//If memory has never been allocated and head doesn't exist, error
	assert(memHasInit == 1 || head != NULL);

	//Use valCheck match to confirm ptr is from my malloc
	header* val = (header*)ptr - 1; //ptr to valcheck int
	if (val->valCheck != MAGIC) {
		return -1;
	}

	//turn the given ptr into a free-list node
	node_t * nptr = (node_t *)((char*)ptr - sizeof(header));
	nptr->size = val->size + sizeof(header);
	nptr->next = NULL;

	//insert node, in order based on the ptr location, into list
	insertNode(nptr);
	//iterate over the free list, if any free sections touch the newly freed section, merge
	//continue looking for merge opportunities until we merge into the largest chunk possible
	int merge = 1;
	while (merge) {
		merge = mergeNode(&nptr);	
	}
	return 0;
}

/*
Debugging routine for your own use. Have it print the regions of free memory to the screen.
*/
void Mem_Dump() {
	//exit if list does not exist
	if (head == NULL) {
		printf("head==NULL\n");
		return;
	}
	node_t * headTmp = head;
	printf("Node Location: %d\n", (int)headTmp);
	printf("Size of FreeSpace %d\n", (int)headTmp->size);
	printf("Location of next Node %d \n\n", (int)headTmp->next);


	while (headTmp->next != NULL) {
		headTmp = headTmp->next;
		printf("Node Location: %d\n", (int)headTmp);
		printf("Size of FreeSpace %d\n", (int)headTmp->size);
		printf("Location of next Node %d \n\n", (int)headTmp->next);
	}
	printf("********************************************** \n");
	return;
}
