#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>

#define _4MB (4 * 1024 * 1024)


void *head = NULL;

void *memalloc(unsigned long size) {
    printf("memalloc() called\n");
    unsigned long tempsize;
    if (size % 8 == 0) tempsize = 0;
    else tempsize = 8;
	void *allocated=NULL;
    unsigned long req_size = (size / 8) * 8 + tempsize + 8;
	req_size=req_size<24?24:req_size;
	if(!head){    //if there is no free memory
		unsigned long tempmemsize;
		if (req_size % _4MB == 0) tempmemsize = 0;
		else tempmemsize = _4MB;
		unsigned long mem_req = (req_size / _4MB) * _4MB + tempmemsize;
		unsigned long b=mem_req-req_size;
		void *start=mmap(NULL,mem_req, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, 0, 0);
		if(b>24){
			allocated=start;
			void *chunksfree=allocated+req_size;
			*((unsigned long*)(allocated))=req_size;
			*((unsigned long*)(chunksfree))=b;
			*((void**)(chunksfree+8))=NULL;
			*((void**)(chunksfree+16))=NULL;
			head=chunksfree;
		}
		else{    //b<24 condition
			allocated=start;
			*((unsigned long*)(allocated))=mem_req;
		}
	}
	else{
		void *temp=head;
		while(temp){
			unsigned long thissize=*((unsigned long*)(temp));
			if(thissize>=req_size){     //we have found the suitable free memory chunk
				unsigned long b=thissize-req_size;
				if(b>24){
					allocated=temp;
					void *chunksfree = allocated+req_size;
					*((unsigned long*)(chunksfree))=b;
					*((unsigned long*)(allocated))=req_size;


					// Updating the freelist
					if(!(*((void**)(temp+8)))&&!(*((void**)(temp+16)))){
						head=NULL;
					}
					else if(!(*((void**)(temp+8)))&&(*((void**)(temp+16)))){
						*((void**)((*((void**)(temp+16)))+8))=NULL;
					}
					else if((*((void**)(temp+8)))&&!(*((void**)(temp+16)))){
						*((void**)((*((void**)(temp+8)))+16))=NULL;
						head=*((void**)(temp+8));
					}
					else{
						*((void**)((*((void**)(temp+16)))+8))=*((void**)(temp+8));
						*((void**)((*((void**)(temp+8)))+16))=*((void**)(temp+16));
					}
					
					// inserting new chunks into the freelist
					if(!head){
						head=chunksfree;
						*((unsigned long*)(chunksfree))=b;
						*((void**)(chunksfree+8))=NULL;
						*((void**)(chunksfree+16))=NULL;
					}
					else{
						
						*((void**)(chunksfree+16))=NULL;
						*((void**)(chunksfree+8))=head;
						*((void**)(head+16))=chunksfree;
						head=chunksfree;
					}
				}
				else{
					allocated=temp;
					*((unsigned long*)(allocated))=thissize;


					//Updating the freelist
					if(!(*((void **)((temp) + 8)))&&!(*((void**)(temp+16)))){
						head=NULL;
					}
					else if(!(*((void**)(temp+8)))&&(*((void**)(temp+16)))){
						*((void**)((*((void**)(temp+16)))+8))=NULL;
					}
					else if((*((void**)(temp+8)))&&!(*((void**)(temp+16)))){
						*((void**)(*((void**)(temp+8))+16))=NULL;
						head=*((void**)(temp+8));
					}
					else{
						*((void**)((*((void**)(temp+16)))+8))=*((void**)(temp+8));
						*((void**)((*((void**)(temp+8)))+16))=*((void**)(temp+16));
					}
				}
				break;
			}
			temp=*((void**)(temp+8));
		}
		if(!temp){     //no suitable memory chunk found, so requesting new memory from OS
			unsigned long tempmemsize;
			if (req_size % _4MB == 0) tempmemsize = 0;
			else tempmemsize = _4MB;
			unsigned long mem_req = (req_size / _4MB) * _4MB + tempmemsize;
			unsigned long b=mem_req-req_size;
			void *start=mmap(NULL,mem_req, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, 0, 0);
			if(b>24){
				allocated=start;
				void *chunksfree=allocated+req_size;
				*((unsigned long*)(allocated))=req_size;
				*((unsigned long*)(chunksfree))=b;
				*((void**)(chunksfree+8))=head;      // simple linked list operations
				*((void**)(chunksfree+16))=NULL;
				*((void**)(head+16))=chunksfree;
				head=chunksfree;
			}
			else{
				allocated=start;        //no need to split the chunk
				*((unsigned long*)(allocated))=mem_req;
			}
		}
	}
	return allocated+8;
    // printf("%ld\n",req_size);
}



int memfree(void *ptr) {
    printf("memfree() called\n");
	if(!ptr) return -1;
	int right_flag=0,left_flag=0;    //which memories are free in neighbour
	unsigned long right_size,left_size;     //sizes of those
	void*start=ptr-8;      //pointing to start of current chunk
	unsigned long size=*((unsigned long *)(start));      //size of current chunk
	void* end=start+size;     //end of current chunk
	void*temp=head;
	while(temp){
		if(right_flag==0){
			if(temp==end){
				right_flag=1;    //right memory free
				right_size=*((unsigned long*)(end));
				
			}
		}
		if(left_flag==0){
			unsigned long temp_size= *((unsigned long *)(temp));
			void*temp_end=temp+temp_size;
			if(temp_end==start){
				left_flag=1;    //left memory free
				left_size=temp_size;
			}
			
		}
		if(left_flag==1&&right_flag==1){
			break;
		}
		temp=*((void**)(temp+8));
	}

	if(right_flag){    //right neighbour free
		void *right_start=end;
		if(!(*((void**)(right_start+8)))&&!(*((void**)(right_start+16)))){
			head=NULL;
		}
		else if(!(*((void**)(right_start+8)))&&(*((void**)(right_start+16)))){
			*((void**)((*((void**)(right_start+16)))+8))=NULL;
		}
		else if((*((void**)(right_start+8)))&&!(*((void**)(right_start+16)))){
			head=(*((void**)(right_start+8)));
			*((void**)((*((void**)(right_start+8)))+16))=NULL;
		}
		else{
			*((void**)((*((void**)(right_start+16)))+8))=(*((void**)(right_start+8)));
			*((void**)((*((void**)(right_start+8)))+16))=(*((void**)(right_start+16)));
		}
	}
	if(left_flag){    //left neighbour free
		void *left_start=start-left_size;
		if(!(*((void**)(left_start+8)))&&!(*((void**)(left_start+16)))){
			head=NULL;
		}
		else if(!(*((void**)(left_start+8)))&&(*((void**)(left_start+16)))){
			*((void**)((*((void**)(left_start+16)))+8))=NULL;
		}
		else if((*((void**)(left_start+8)))&&!(*((void**)(left_start+16)))){
			head=*((void**)(left_start+8));
			*((void**)((*((void**)(left_start+8)))+16))=NULL;
		}
		else{
			*((void**)((*((void**)(left_start+16)))+8))=(*((void**)(left_start+8)));
			*((void**)((*((void**)(left_start+8)))+16))=(*((void**)(left_start+16)));
		}
	}
	else if(!left_flag&&!right_flag){   //no neighbour free
		unsigned long tot_size=size;
		
		if(!head){
			head=start;
			*((unsigned long*)(start))=tot_size;
			*((void**)(start+8))=NULL;
			*((void**)(start+16))=NULL;
		}
		else{
			*((unsigned long*)(start))=tot_size;
			*((void**)(start+8))=head;
			*((void**)(start+16))=NULL;
			*((void**)(head+16))=start;
			head=start;
		}
	}
    return 0;
}



