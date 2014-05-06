// This project performs dynamic instruction scheduling using Tomasulo's algorithm.
// The bonus part is also implemented in this code, i.e. for instructions of type 2, a read operation is performed on the cache.

#include<stdio.h>
#include<string.h>
#include<stdlib.h>

// Old Global declarations
int block_bits = 0, sets1=0, sets2=0;

// New Global declarations
int cycle=0, S=0, N=0, block_size=0, eof_reached=0, tag=0, dispatch_free=0, issue_free=0, execute_free=0;
char trace_file[30];  
FILE *fp;
int next_dispatch_prob = 0;

// Trace file read variables
int mem_addr=0, op_type=0, src1_reg=0, src2_reg=0, PC=0, dest_reg=0;

// ROB STATUS MEANINGS
// status 1 means IF stage
// status 2 means ID stage
// status 3 means IS stage
// status 4 means EX stage
// status 5 means WB stage


typedef struct rob{
   int tag; // the destination tag or the renamed register
   int op_type; // Operation type
   int src1_reg;
   int src2_reg;
   int dest_reg;

   int IF_t;
   int ID_t;
   int IS_t;
   int EX_t;
   int WB_t;

   int status; // Status of the instruction, not necessary here
   int src1_state, src2_state;
   int src1_tag, src2_tag;
   int dest_tag;
   int PC; // Program counter
   int mem_addr; // Memory address
   int no_of_cycles;

   struct rob *next;

}rob_node;


typedef struct {
   int tag;
   unsigned dirty;
   unsigned valid;
   unsigned LRU_counter;
} cache_tag;

typedef struct {

   int index_bits;
   int index_mask;
   int tag_mask;

   short unsigned level;
   int reads;
   int read_hits;
   int read_misses;
   int writes;
   int write_hits;
   int write_misses;
   int WB_counter;
   float miss_rate;
   int mem_traffic;
   int LRU_counter;
   float miss_penalty;
   float hit_time;
   float tot_access_time;
   float avg_access_time;
   int assoc;
   int size;

} cache;

typedef struct node {
   int tag;

   struct node *next;   
} list_node;

cache_tag** create_cache(unsigned rows, int columns) {
 
   int i = 0;
   cache_tag **a;  
   a = calloc(rows, sizeof(cache_tag*));
   for(i = 0; i < rows; i++)
      a[i] = calloc(columns, sizeof(cache_tag));   
   return a; 
}

unsigned createMask(unsigned a, unsigned b) {

   unsigned i, r = 0;
   for (i = a; i < b; i++)
       r |= 1 << i;
   return r;
}


typedef struct reg {
   int tag;
   int ready;
} reg_node;


// Global data structures
cache_tag **tag1=NULL, **tag2=NULL;
cache *L1 = NULL, *L2 = NULL;
reg_node *reg_file = NULL;
rob_node *rob_head = NULL, *rob_tail = NULL, *next_dispatch = NULL, *rob_fetch = NULL;
list_node *dispatch_head = NULL, *dispatch_tail = NULL;
list_node *iready_head = NULL, *iready_tail = NULL;
list_node *issue_head = NULL, *issue_tail = NULL;		
list_node *eready_head = NULL, *eready_tail = NULL;
list_node *execute_head = NULL, *execute_tail = NULL;		


// Function prototype declarations
void append_rob(rob_node **);
void append_dispatch();
void append_issue(list_node **);
void append_iready(list_node **);
int  remove_list_node(list_node **, int);
void append_rob_to_list(list_node **, rob_node **);
void append_list_to_list(list_node **, list_node **);
void fetch();
void dispatch();
void issue();
void execute();
void fakeretire();
rob_node* lookup_rob(int);
void empty_list(list_node**);
void append_execute(list_node **);
void append_eready(list_node **);
list_node* find_tail(list_node**);

// cache function definitions
int read(cache*, int, cache_tag**, int);
int evict(int, cache_tag**, int, int);


main(int argc, char **argv) {

   int i=0, j=0;
   float ipc=0;



   L1 = calloc(1,sizeof(cache));
   L1->level=1;
   L2 = calloc(1,sizeof(cache));
   L2->level=2;


// Get the inputs from the command line arguments
   S		 = atoi(argv[1]);
   N		 = atoi(argv[2]);
   block_size    = atoi(argv[3]);
   L1->size      = atoi(argv[4]);
   L1->assoc     = atoi(argv[5]);
   L2->size      = atoi(argv[6]);
   L2->assoc     = atoi(argv[7]);
   strcpy(trace_file,argv[8]);


//===================================================//
// Setting things ready for Cache specific operations
   unsigned temp=0;
  
// Calculate number of sets 
   if (L1->assoc != 0)
      sets1 = L1->size / (block_size * L1->assoc);
   else 
      sets1 = 0;

   if (L2->assoc != 0)
      sets2 = L2->size / (block_size * L2->assoc);
   else 
      sets2 = 0;

// Create a tag matrix before reading from trace file
   if (L1->assoc != 0)
      tag1 = create_cache(sets1,L1->assoc);

   if (L2->size != 0)
      tag2 = create_cache(sets2,L2->assoc);
   else
      tag2 = NULL;

// Below code creates mask according to number of sets and block_size for L1 cache
   temp = block_size;
   while (temp >>= 1)
  	block_bits++;

   temp = sets1;
   while (temp >>= 1)
  	L1->index_bits++;

   L1->index_mask = createMask(0,L1->index_bits);
   L1->tag_mask = createMask(L1->index_bits,32 - block_bits);
   L1->tag_mask >>= L1->index_bits;

// Below code creates mask according to number of sets and block_size for L2 cache
   temp = sets2;
   while (temp >>= 1)
  	L2->index_bits++;

   L2->index_mask = createMask(0,L2->index_bits);
   L2->tag_mask = createMask(L2->index_bits,32 - block_bits);
   L2->tag_mask >>= L2->index_bits;

//===========================================================

// Creating the register file
   reg_file = malloc(128 * sizeof(reg_node));
   for(i = 0; i < 128; i++) 
      reg_file[i].ready = 1;

   dispatch_free = 2 * N;
   issue_free = S;
   execute_free = N;

// Read from the file
   fp = fopen(trace_file, "r");

   do {

      fakeretire();
      execute();
      issue();
      dispatch();
      fetch();
      	

   } while (advance_cycle());


// Printing the cache contents

   if (L1->assoc != 0) {
      printf(" L1 CACHE CONTENTS\n");
      printf("  a. number of accesses : %d\n", L1->reads);
      printf("  b. number of misses : %d", L1->read_misses);

      for (i=0; i<sets1; i++) {
	 printf("\n set %d:\t",i);
	 for (j=0; j < L1->assoc; j++) {
	      printf("   %x ",tag1[i][j].tag);
	      if (tag1[i][j].dirty)
		   printf("D");
	 }
      }
      printf("\n");
      if (L2->assoc == 0)
         printf("\n");
	
   }

   if (L2->assoc != 0) {
      printf("\n L2 CACHE CONTENTS\n");
      printf("  a. number of accesses : %d\n", L2->reads);
      printf("  b. number of misses : %d", L2->read_misses);

      for (i=0; i<sets2; i++) {
	 printf("\n set %d:\t",i);
	 for (j=0; j < L2->assoc; j++) {
	      printf("   %x ",tag2[i][j].tag);
	      if (tag2[i][j].dirty)
		   printf("D");
	 }
      }
      printf("\n");
      printf("\n");
   }

// Printing the final details of instructions
   printf(" CONFIGURATION\n");
   printf("  superscalar bandwidth (N)\t = %d\n", N);
   printf("  dispatch queue size (2*N)\t = %d\n", 2*N);
   printf("  schedule queue size (S)\t = %d\n", S);
   printf(" RESULTS\n");
   printf("  number of instructions = %d\n", tag);
   printf("  number of cycles = %d\n", cycle-1);

// Calculating IPC
   ipc = (float) (tag)/(cycle-1);
   printf("  IPC = %.2f\n", ipc);

}


int advance_cycle() {
   cycle++;
   if (NULL == rob_head)
	return 0;

   return 1;
}


void fakeretire() {

   rob_node *temp_rob = rob_head;
   rob_node *tbd = NULL;

   while (temp_rob != NULL && temp_rob->status == 5) {

// Print the instruction information
         tbd = temp_rob;

// Printing out the rob node that is about to be deleted
         printf(" %d fu{%d} src{%d,%d} dst{%d} IF{%d,%d} ID{%d,%d} IS{%d,%d} EX{%d,%d} WB{%d,%d}\n", tbd->tag, tbd->op_type, tbd->src1_reg, tbd->src2_reg, tbd->dest_reg, tbd->IF_t, tbd->ID_t-tbd->IF_t, tbd->ID_t, tbd->IS_t-tbd->ID_t, tbd->IS_t, tbd->EX_t-tbd->IS_t, tbd->EX_t, tbd->WB_t-tbd->EX_t, tbd->WB_t, 1); 

         rob_head = rob_head->next;
	 if (rob_head == NULL)
	    rob_tail = NULL;
	 free(tbd);   
         temp_rob = rob_head;
   }
}


void execute() {

int end=0;
list_node *temp_execute = execute_head;
list_node *temp_issue = NULL;
rob_node *temp_rob = NULL;
rob_node *temp_rob2 = NULL;

   while (temp_execute != NULL) {

      temp_rob = lookup_rob(temp_execute->tag);
      temp_rob->no_of_cycles--;

// This condition checks for instructions that finish in this particular cycle
      if (temp_rob->no_of_cycles == 0) {

         temp_rob->status = 5;
	 temp_rob->WB_t = cycle;	 
 
// Update Register renaming file and reservation stations only when the destination register is not -1 
 	 if (temp_rob->dest_reg != -1) {
	    // Update the register file
	    if (temp_rob->tag == reg_file[temp_rob->dest_reg].tag)
	       reg_file[temp_rob->dest_reg].ready = 1;

	    // Check every instruction in the issue list that is waiting on this instruction
	    temp_issue = issue_head;
	    while (temp_issue != NULL) {
	       
	       temp_rob2 = lookup_rob(temp_issue->tag);
	       if (temp_rob2->src1_state == 0 && temp_rob2->src1_tag == temp_execute->tag) 
		  temp_rob2->src1_state = 1;

	       if (temp_rob2->src2_state == 0 && temp_rob2->src2_tag == temp_execute->tag) 
		  temp_rob2->src2_state = 1;

	       temp_issue = temp_issue->next;
	    }
	 }

// Remove the node from the execute list and update the execute tail accordingly
         end = remove_list_node(&execute_head, temp_execute->tag); 
	 if (end == 1)
	    execute_tail = find_tail(&execute_head); 
	 execute_free++;
  	 if (execute_head == NULL)
	    execute_tail = NULL;
      } 

      temp_execute = temp_execute->next;
   }
}

// This function returns the rob with the tag given as input
rob_node* lookup_rob(int tag) {
   rob_node *temp = rob_head;

   while(temp != NULL) {
      if (temp->tag == tag)
	 return temp;
      temp = temp->next;
   }
   return NULL;  // If NULL is returned, something is wrong with the logic!
}


void issue() {

int end=0;
rob_node *temp_rob = NULL;
list_node *temp_issue = issue_head;
list_node *temp_eready = NULL;
int count=0;

// Traverse through the whole of dispatch list and do the following
   while (temp_issue != NULL) {

      if(check_operand_ready(temp_issue->tag))
  	 append_eready(&temp_issue); // Add the instructions in IS stage to eready list
 
      temp_issue = temp_issue->next;
   }

// As long as there is free space in the issue list, do the following
   temp_eready = eready_head;
   while (temp_eready != NULL && count < N) {

      temp_rob = lookup_rob(temp_eready->tag);
      temp_rob->status = 4;
      temp_rob->EX_t = cycle; // Execution starts at this cycle

// Before appending the instruction to execute list, check if it is of type 2.
// If it is, call the cache module and update the no_of_cycles for this instruction accordingly.
//
      if (block_size != 0) { 
	 int addr = temp_rob->mem_addr >> block_bits;
	 if (2 == temp_rob->op_type)
	    temp_rob->no_of_cycles = read(L1, L1->assoc, tag1, addr);
      }

      append_execute(&temp_eready); // Append node to execute list
      execute_free--;
      end = remove_list_node(&issue_head, temp_eready->tag); // Remove node from dispatch queue
      if (end == 1)
	 issue_tail = find_tail(&issue_head);

      if (issue_head == NULL)
	 issue_tail = NULL;

      issue_free++;
      count++;
      temp_eready = temp_eready->next;
   }

   empty_list(&eready_head); // Empty the issue ready list
   eready_tail = NULL;
}

//==========================================================================================================
//Implementation of cache part [includes read(), write() and evict() ]
//==========================================================================================================

// Do not remove columns parameters even if it is redundant!! 
// For some reason the program is not working when it is removed.
int read(cache *cache, int columns, cache_tag **a, int addr) {
  

   cache->LRU_counter++;
   cache->reads += 1;
   int j=0, found=0, sb_found=0, addr2=0, addr3=0, r=0, c=0, d=0, e=0, next=0, evict_row=0;
   int index_value = addr & cache->index_mask;
   int tag_value = (addr >> cache->index_bits) & cache->tag_mask;
   
// Scan all blocks in that row of index_value and update counters if found
   for(j=0;j<cache->assoc;j++) {
	if (a[index_value][j].tag == tag_value) {
		if (a[index_value][j].valid == 1) {  // valid 1 means a non-empty block
		   found = 1;
		   break;
		}
	}
   }

// If it is a hit, update read_hits and LRU counter and return value accordingly
   if (found) {

	   cache->read_hits += 1;
	   a[index_value][j].LRU_counter = cache->LRU_counter;
           if (cache->level == 1)
	      return 5;
	   else if (cache->level == 2)
	      return 10;
   }

// If it is a miss
   else {
	   cache->read_misses += 1;

           // If a miss happens in L2 cache, increment the memory traffic
	   if (cache->level == 2)
		cache->mem_traffic += 1;

	   for(j=0;j<cache->assoc;j++) {
	      if (a[index_value][j].valid == 0) {
		   found = 1;
		   break;
	      }	

	   }

	   // If an empty block is found, i.e valid == 0
	   if (found) {
		   a[index_value][j].tag = tag_value;
		   a[index_value][j].LRU_counter = cache->LRU_counter;
	   	   a[index_value][j].valid = 1;
   		   a[index_value][j].dirty = 0;

		   //send a read message to cache2 from cache1
		   if (cache->level == 1 && L2->assoc != 0)
   		   	return (read(L2,L2->assoc,tag2,addr)); 
	 	   else
		      return 20; // To tell to the main fn that its a miss and it has to call L2 cache.
	   }

	   else { // All the blocks are occupied
		   j = evict(columns, a, cache->assoc, index_value);
	   
		   if (a[index_value][j].dirty == 1) { // write back write allocate
			// Aggregate tag and index into an address
			cache->WB_counter += 1;
			a[index_value][j].dirty = 0;

		   // Write back to L2 cache only if current cache is L1
			if (cache->level == 1 && L2->assoc != 0) {

				addr2 = (a[index_value][j].tag << cache->index_bits) | index_value;
				write(L2,L2->assoc,tag2,addr2);
 			 	   
			}
		   }
		   a[index_value][j].tag = tag_value;
		   a[index_value][j].LRU_counter = cache->LRU_counter;
	   	   a[index_value][j].valid = 1;

  		   if (cache->level == 1 && L2->assoc != 0)
		      return (read(L2,L2->assoc,tag2,addr));
		   else
		      return 20; // Same reason as previous return of 0
	   }
   }
}


int write(cache *cache, int columns, cache_tag **a, int addr) {
   
   cache->LRU_counter++;
   cache->writes += 1;
   int j, found=0, addr2 = 0;
   int index_value = addr & cache->index_mask;
   int tag_value = (addr >> cache->index_bits) & cache->tag_mask;

// Scan all blocks in that row of index_value and update counters if found
   for(j=0;j<cache->assoc;j++) {
	if (a[index_value][j].tag == tag_value) {
		if (a[index_value][j].valid == 1) {  // valid 1 means a non-empty block
		   found = 1;
		   break;
		}
	}
   }

// If it is a hit, update read_hits and LRU counter
   if (found) {
	   cache->write_hits += 1;
	   a[index_value][j].LRU_counter = cache->LRU_counter;
  	   a[index_value][j].dirty = 1;

	   return 1;
   }

// If it is a miss
   else {
	   cache->write_misses += 1;

	   if (cache->level == 2)
		cache->mem_traffic += 1;

	   for(j=0;j<cache->assoc;j++) {
	      if (a[index_value][j].valid == 0) {
		   found = 1;
		   break;
	      }	

	   }

	   // If an empty block is found, i.e valid == 0
	   if (found) {
		   a[index_value][j].tag = tag_value;
		   a[index_value][j].dirty = 1;
		   a[index_value][j].LRU_counter = cache->LRU_counter;
	   	   a[index_value][j].valid = 1;

		   //send a read message to L2 to bring in the missing block
		   if (cache->level == 1)
			read(L2,L2->assoc,tag2,addr);

		   return 0; // To tell to the main fn that its a miss and it has to call L2 cache.

	   }

	   else { // All the blocks are occupied
		   j = evict(columns, a, cache->assoc, index_value);
	   
		   if (a[index_value][j].dirty == 1 && a[index_value][j].valid == 1) {
			   // Aggregate tag and index into an address
			   cache->WB_counter += 1;
			   a[index_value][j].dirty = 0;
			   if (cache->level == 1 && L2->assoc != 0) {

				addr2 = (a[index_value][j].tag << cache->index_bits) | index_value;
				write(L2,L2->assoc,tag2,addr2);				   
			   }
		   }

		   a[index_value][j].tag = tag_value;
		   a[index_value][j].dirty = 1;
		   a[index_value][j].LRU_counter = cache->LRU_counter;
	   	   a[index_value][j].valid = 1;

		   if (cache->level == 1 && L2->assoc != 0)
 			read(L2,L2->assoc,tag2,addr);

		   return 0; // Same reason as previous return of 0

		}
	}
}


int evict(int columns, cache_tag **a, int assoc, int index_value ) {
   unsigned j=0, selected_block, temp;
   selected_block = j;

   temp = a[index_value][j].LRU_counter;

   // To find the block with minimum LRU_counter
   for(j=0;j<assoc;j++) {
      if (a[index_value][j].LRU_counter < temp) {
 	 temp = a[index_value][j].LRU_counter;
	 selected_block = j;
      }
   }

   return selected_block;
}


//==============================================================================================================
// End of cache part
//==============================================================================================================

// This function simple returns true if both the operand states of an instruction in rob is 1
int check_operand_ready(int tag) {

   rob_node *temp = rob_head;
   while (temp != NULL && temp->tag != tag)
      temp = temp->next;

   if (temp == NULL) // This means its not found in rob, which is wrong!!
      printf("\n Something is gravely wrong!!");
   else if (temp->src1_state == 1 && temp->src2_state == 1)
      return 1;
   else 
      return 0;
}


void append_execute(list_node **temp) {

    if (NULL == execute_head) {
	 append_list_to_list(&execute_head, temp);
	 execute_tail = execute_head;
    }
    else {
	 append_list_to_list(&execute_tail->next, temp);
	 execute_tail = execute_tail->next;
    }

}


void append_eready(list_node **temp) {

    if (NULL == eready_head) {
	 append_list_to_list(&eready_head, temp);
	 eready_tail = eready_head;
    }
    else {
	 append_list_to_list(&eready_tail->next, temp);
	 eready_tail = eready_tail->next;
    }

}



void dispatch() {

list_node *temp_dispatch = dispatch_head;
list_node *temp_iready = NULL;
rob_node *temp_rob = NULL;
rob_node *temp_rob2 = NULL;
int count=0, end=0;

// Traverse through the whole of dispatch list and do the following
   while (temp_dispatch != NULL) {

      temp_rob2 = lookup_rob(temp_dispatch->tag);
// One cycle fetch latency for IF state
      if (1 == temp_rob2->status) {

         temp_rob2->ID_t = cycle;
         temp_rob2->status = 2;
         temp_dispatch = temp_dispatch->next;
      }
  
      else if (2 == temp_rob2->status) {
  	 append_iready(&temp_dispatch); // Add the instructions in ID stage to iready list
         temp_dispatch = temp_dispatch->next;
      }
   }

// As long as there is free space in the issue list, do the following
   temp_iready = iready_head;
   while (issue_free) {

      if (temp_iready != NULL && count < S) {

         // Update ROB 
    	 temp_rob = lookup_rob(temp_iready->tag);
	 temp_rob->status = 3;
 	 temp_rob->IS_t = cycle;

 	 // Renaming source and destination operands        
         if (reg_file[temp_rob->src1_reg].ready == 1 || temp_rob->src1_reg == -1 || temp_rob->status == 5) {
	    temp_rob->src1_state = 1;
	    temp_rob->src1_tag = temp_rob->src1_reg;
	 }
   	 else {
	    temp_rob->src1_state = 0;
	    temp_rob->src1_tag = reg_file[temp_rob->src1_reg].tag;
	 }
 
         if (reg_file[temp_rob->src2_reg].ready == 1 || temp_rob->src2_reg == -1 || temp_rob->status == 5) {
	    temp_rob->src2_state = 1; 
	    temp_rob->src2_tag = temp_rob->src2_reg;
	 }
   	 else {
	    temp_rob->src2_state = 0;
	    temp_rob->src2_tag = reg_file[temp_rob->src2_reg].tag;
	 }

	 // Rename destination only if it is not -1
         if (temp_rob->dest_reg != -1) {
	    reg_file[temp_rob->dest_reg].ready = 0;
	    reg_file[temp_rob->dest_reg].tag = temp_rob->tag;   
	    
	 }

	 append_issue(&temp_iready); // Append node to issue list
         issue_free--;

         end = remove_list_node(&dispatch_head, temp_iready->tag); // Remove node from dispatch queue
	 if (end == 1)
	    dispatch_tail = find_tail(&dispatch_head);

         if (dispatch_head == NULL)
	    dispatch_tail = NULL;

         dispatch_free++;
	 count++;
     	 temp_iready = temp_iready->next;
      }
      else
	 break;
   }
   empty_list(&iready_head); // Empty the issue ready list
   iready_tail = NULL;
}


void append_issue(list_node **temp) {

    if (NULL == issue_head) {
	 append_list_to_list(&issue_head, temp);
	 issue_tail = issue_head;
    }
    else {
	 append_list_to_list(&issue_tail->next, temp);
	 issue_tail = issue_tail->next;
    }

}


void append_iready(list_node **temp) {

    if (NULL == iready_head) {
	 append_list_to_list(&iready_head, temp);
	 iready_tail = iready_head;
    }
    else {
	 append_list_to_list(&iready_tail->next, temp);
	 iready_tail = iready_tail->next;
    }

}



int remove_list_node(list_node **head, int tag) {

   int end=0;
   list_node *temp = *head;
   list_node *tbd = NULL;

   if (tag == (*head)->tag) { // The node to be removed is the head node

      tbd = temp;   
      *head = (*head)->next;
      free(tbd);
   }

   else {

      while (temp->next != NULL && (temp->next)->tag != tag) // pl check this later
	 temp = temp->next;

      tbd = temp->next;
      if (tbd != NULL) {
         if (tbd->next == NULL)
            end = 1;   
      }
      temp->next = (temp->next)->next;
      free(tbd);
   }
   return end;

}

list_node* find_tail(list_node **head){

   list_node *temp = *head;

   while (temp->next != NULL)
      temp = temp->next;

   return temp;
}

void empty_list(list_node **head) {

   list_node *temp = NULL;

   while ((*head) != NULL) {
      temp = *head;
      *head = (*head)->next;
      free(temp);

   }
   *head = NULL;
}


// This function appends a list node to the 'head' queue
void append_list_to_list(list_node **head, list_node **next) {

   list_node *newnode = malloc(sizeof(list_node));
   
   newnode->tag = (*next)->tag;

   newnode->next = *head;
   *head = newnode;

}



void fetch() {
  
   int i=0, count=0;
   char line[100];

   for (i=0; i<N; i++) {
      if (0 == eof_reached) {
	 fgets(line, 100, fp);
         if (feof(fp)){
 	    eof_reached = 1;	
	    break;
         }
	 sscanf(line, "%x %d %d %d %d %x", &PC, &op_type, &dest_reg, &src1_reg, &src2_reg, &mem_addr);
         if (NULL == rob_head) {	
		append_rob(&rob_head);
		rob_tail = rob_head;
                next_dispatch = rob_head;
		rob_fetch = rob_head;
         }
         else {
		append_rob(&rob_tail->next);
	        rob_tail = rob_tail->next;
	 }
      }
      else 
 	 break;
   }

   if (next_dispatch_prob == 1 && next_dispatch->next != NULL) {

      next_dispatch_prob = 0;
      next_dispatch = next_dispatch->next;

   }

   while (dispatch_free && next_dispatch_prob == 0) {
      if (next_dispatch != NULL && count < N) {
	 append_dispatch();
         dispatch_free--;
	 count++;

         if (next_dispatch->next != NULL)
     	    next_dispatch = next_dispatch->next;
         else {
            next_dispatch_prob = 1;
	    break;
	 }
      }
      else
	 break;
   }

}


void append_rob(rob_node **head){

// Create new rob_node and assign the necessary
   rob_node *newnode = malloc(sizeof(rob_node));

   newnode->tag = tag;
   newnode->status = 1; // Instructions are in IF state during append_rob
   newnode->op_type = op_type;
   newnode->dest_reg = dest_reg;
   newnode->src1_reg = src1_reg;
   newnode->src2_reg = src2_reg;

   if (src1_reg == -1)
      newnode->src1_state = 1; 
   else
      newnode->src1_state = reg_file[src1_reg].ready;

   if (src2_reg == -1)
      newnode->src2_state = 1; 
   else
      newnode->src2_state = reg_file[src2_reg].ready;

   newnode->mem_addr = mem_addr;
   newnode->PC = PC;

 
   if (op_type == 0)
      newnode->no_of_cycles = 1;
   else if (op_type == 1)
      newnode->no_of_cycles = 2;
   else if (op_type == 2)
      newnode->no_of_cycles = 5;

   newnode->next = *head;
   *head = newnode;
   tag++;
}


void append_dispatch() {

    if (dispatch_head == NULL) {
	 append_rob_to_list(&dispatch_head, &next_dispatch);
	 dispatch_tail = dispatch_head;
    }
    else {
	 append_rob_to_list(&dispatch_tail->next, &next_dispatch);
	 dispatch_tail = dispatch_tail->next;
    }

}


// This function appends a rob node to the dispatch queue
void append_rob_to_list(list_node **head, rob_node **next) {

   list_node *newnode = malloc(sizeof(list_node));
   
   newnode->tag = (*next)->tag;
   newnode->next = *head;
   (*next)->IF_t = cycle;
   *head = newnode;

// other fields of list_node need not be populated at this time

}
