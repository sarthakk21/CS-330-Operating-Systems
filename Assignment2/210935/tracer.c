#include<context.h>
#include<memory.h>
#include<lib.h>
#include<entry.h>
#include<file.h>
#include<tracer.h>



///////////////////////////////////////////////////////////////////////////
//// 		Start of Trace buffer functionality 		      /////
///////////////////////////////////////////////////////////////////////////

int trace_buffer_write_duplicate(struct file *filep, char *buff, u32 count)
{
    if (!filep || count < 0 || filep->mode == O_READ)
        return -EINVAL;  // Invalid arguments

    if(!buff){
        return -EBADMEM;
    }

    if(count == 0)
        return 0;

    if(!(filep -> trace_buffer -> space)){
        return 0;
    }
    
    struct trace_buffer_info *trace_buffer = filep->trace_buffer;
    
    if (!trace_buffer)
        return -EINVAL;  // Not a trace buffer

    u32 read_off = filep->trace_buffer->read_offset;
    u32 write_off = filep->trace_buffer->write_offset;

    u32 remaining;
    if(read_off >= write_off){
        remaining = read_off - write_off;
    }
    else{
        remaining = 4096 - write_off + read_off;
    }
    
    if(remaining == 0 && (filep->trace_buffer->space)){
        remaining = 4096;
    }

    if(remaining < count){
        count = remaining;
    }

    if(filep->trace_buffer->space){
        for(u32 i = 0 ; i < count ; i++){
            filep -> trace_buffer -> buffer[(write_off + i) % 4096] = buff[i]; 
        }
    }

    if((write_off+count) % 4096 == read_off){
        filep->trace_buffer->space = 0;       //buffer full
    }
    
    filep->trace_buffer->write_offset = (write_off + count) % 4096;

    filep->trace_buffer = trace_buffer;
    return count; 
}

int trace_buffer_read_duplicate(struct file *filep, char *buff, u32 count)
{
    if (!filep || count < 0)
        return -EINVAL;  // Invalid arguments

    if(!buff){
        return -EBADMEM;
    }

    if(filep->mode == O_WRITE){
        return -EINVAL;
    }

    if(count == 0)
        return 0;

    struct trace_buffer_info *trace_buffer = filep->trace_buffer;
    
    if (!trace_buffer)
        return -EINVAL;  // Not a trace buffer

    u32 read_off = filep->trace_buffer->read_offset;
    u32 write_off = filep->trace_buffer->write_offset;
    u32 remaining_to_read;
    if(read_off <= write_off){
        remaining_to_read = write_off - read_off;
    }
    else{
        remaining_to_read = 4096 - read_off + write_off;
    }
    
    if(remaining_to_read == 0 && (filep->trace_buffer->space)){
        return 0;
    }
    
    if(remaining_to_read == 0 && !(filep->trace_buffer->space)){
        remaining_to_read=4096;
    }

    if(remaining_to_read < count){
        count = remaining_to_read;
    }

    for(u32 i = 0 ; i < count ; i++){
        buff[i] = filep -> trace_buffer -> buffer[(read_off + i) % 4096] ; 
    }

    filep->trace_buffer->space = 1;       //buffer empty

    // if((read_off+count) % 4096 == write_off || count!=0){
    //     filep->trace_buffer->space = 1;       //buffer empty
    // }

    filep->trace_buffer->read_offset = (read_off + count) % 4096;
    return count; 
}



int is_valid_mem_range(unsigned long buff, u32 count, int access_bit) 
{
    struct exec_context *ctx = get_current_ctx();
	if(!buff || count < 0 ){
		return -EBADMEM;

	}
	if (buff >= ctx->mms[MM_SEG_CODE].start && buff + (unsigned long)count < ctx->mms[MM_SEG_CODE].next_free){
		if((ctx->mms[MM_SEG_CODE].access_flags & access_bit) == access_bit){
			return 0;
		}
		else {
			return -EBADMEM;
		}

	}

    if (buff >= ctx->mms[MM_SEG_RODATA].start && buff + (unsigned long)count < ctx->mms[MM_SEG_RODATA].next_free){
		if((ctx->mms[MM_SEG_RODATA].access_flags & access_bit) == access_bit){
			return 0;
		}
		else {
			return -EBADMEM;
		}

	}

    if (buff >= ctx->mms[MM_SEG_DATA].start && buff + (unsigned long)count < ctx->mms[MM_SEG_DATA].next_free){
		if((ctx->mms[MM_SEG_DATA].access_flags & access_bit) == access_bit){
			return 0;
		}
		else {
			return -EBADMEM;
		}

	}

    if (buff >= ctx->mms[MM_SEG_STACK].start && buff + (unsigned long)count < ctx->mms[MM_SEG_STACK].end){
		if((ctx->mms[MM_SEG_STACK].access_flags & access_bit) == access_bit){
			return 0;
		}
		else {
			return -EBADMEM;
		}

	}

    while(ctx->vm_area->vm_next != NULL){
		if(buff >= ctx->vm_area->vm_start && buff + (unsigned long)count < ctx->vm_area->vm_end){
			if((ctx->vm_area->access_flags & access_bit) == access_bit){
				return 0;
			}
		}
		ctx->vm_area = ctx->vm_area->vm_next;
	}
     
	if(buff >= ctx->vm_area->vm_start && buff + (unsigned long)count < ctx->vm_area->vm_end){
		if((ctx->vm_area->access_flags & access_bit) == access_bit){
					return 0;
		}
	}
	return -EBADMEM;
}




long trace_buffer_close(struct file *filep)
{
	if (!filep || filep->type != TRACE_BUFFER || !(filep -> trace_buffer) || !(filep -> trace_buffer -> buffer))
        return -EINVAL;  // Invalid arguments or not a trace buffer

    if(filep -> trace_buffer -> buffer){
        os_page_free(USER_REG,filep -> trace_buffer -> buffer);
        filep -> trace_buffer -> buffer = NULL;
    }

    // Free fileops
    if (filep->fops) {
        os_free(filep->fops, sizeof(struct fileops));
        filep->fops = NULL;
    }

    // Free trace buffer info
    if (filep->trace_buffer) {
        os_free(filep->trace_buffer, sizeof(struct trace_buffer_info));
        filep->trace_buffer = NULL;
    }

    // Free file object
    os_free(filep, sizeof(struct file));

    return 0;  // Success
}



int trace_buffer_read(struct file *filep, char *buff, u32 count)
{
    if (!filep || count < 0)
        return -EINVAL;  // Invalid arguments

    if(!buff){
        return -EBADMEM;
    }

    if(filep->mode == O_WRITE){
        return -EINVAL;
    }
    
    if (is_valid_mem_range((unsigned long)buff, count, 2)!=0)
        return -EBADMEM;

    if(count == 0)
        return 0;

    struct trace_buffer_info *trace_buffer = filep->trace_buffer;
    
    if (!trace_buffer)
        return -EINVAL;  // Not a trace buffer


    u32 read_off = filep->trace_buffer->read_offset;
    u32 write_off = filep->trace_buffer->write_offset;
    u32 remaining_to_read;
    if(read_off <= write_off){
        remaining_to_read = write_off - read_off;
    }
    else{
        remaining_to_read = 4096 - read_off + write_off;
    }
    
    if(remaining_to_read == 0 && (filep->trace_buffer->space)){
        return 0;
    }
    
    if(remaining_to_read == 0 && !(filep->trace_buffer->space)){
        remaining_to_read=4096;
    }

    if(remaining_to_read < count){
        count = remaining_to_read;
    }

    for(u32 i = 0 ; i < count ; i++){
        buff[i] = filep -> trace_buffer -> buffer[(read_off + i) % 4096] ; 
    }

    filep->trace_buffer->space = 1;       //buffer empty

    // if((read_off+count) % 4096 == write_off || count!=0){
    //     filep->trace_buffer->space = 1;       //buffer empty
    // }
    
    filep->trace_buffer->read_offset = (read_off + count) % 4096;
    
    return count; 
}


int trace_buffer_write(struct file *filep, char *buff, u32 count)
{
    if (!filep || count < 0 || filep->mode == O_READ)
        return -EINVAL;  // Invalid arguments

    if(!buff){
        return -EBADMEM;
    }

    if (is_valid_mem_range((unsigned long)buff, count, 1)!=0)
        return -EBADMEM;

    if(count == 0)
        return 0;

    if(!(filep -> trace_buffer -> space)){
        return 0;
    }
    
    struct trace_buffer_info *trace_buffer = filep->trace_buffer;
    
    if (!trace_buffer)
        return -EINVAL;  // Not a trace buffer

    u32 read_off = filep->trace_buffer->read_offset;
    u32 write_off = filep->trace_buffer->write_offset;

    u32 remaining;
    if(read_off >= write_off){
        remaining = read_off - write_off;
    }
    else{
        remaining = 4096 - write_off + read_off;
    }
    
    if(remaining == 0 && (filep->trace_buffer->space)){
        remaining = 4096;
    }

    if(remaining < count){
        count = remaining;
    }

    if(filep->trace_buffer->space){
        for(u32 i = 0 ; i < count ; i++){
            filep -> trace_buffer -> buffer[(write_off + i) % 4096] = buff[i]; 
        }
    }
    if((write_off+count) % 4096 == read_off){
        filep->trace_buffer->space = 0;       //buffer full
    }
    
    filep->trace_buffer->write_offset = (write_off + count) % 4096;

    filep->trace_buffer = trace_buffer;
    return count; 
}

int sys_create_trace_buffer(struct exec_context *current, int mode)
{
	//in case of invalid mode as an input
	if (mode != O_READ && mode != O_WRITE && mode != O_RDWR)
        return -EINVAL;


	//first free file descriptor in the files array
    int fd = -1;  //initialising with -1
    for (int i = 0; i < MAX_OPEN_FILES; i++) {
        if (!(current->files[i])) {         //empty file descriptor
            fd = i;                         //assigning the buffer to that descriptor
            break;
        }
    }

    if (fd == -1)
        return -EINVAL;  // No free file descriptor available
	

	// Allocate a file object (struct file)
    struct file *trace_file = (struct file *)os_alloc(sizeof(struct file));
    if (!trace_file)
        return -ENOMEM;  // Memory allocation failed


	// Allocate a trace buffer object (struct trace_buffer_info)
    struct trace_buffer_info *trace_buffer = (struct trace_buffer_info *)os_alloc(sizeof(struct trace_buffer_info));
    if (!trace_buffer) {
        os_free(trace_file, sizeof(struct file));
        return -ENOMEM;  // Memory allocation failed
    }

	// Initialize the trace buffer object
    trace_buffer->buffer = (char*)os_page_alloc(USER_REG);
    trace_buffer->read_offset = 0;   // Initialize read offset to 0
	trace_buffer->write_offset = 0;  // Initialize write offset to 0
    trace_buffer->space = 1;         // space = 1 means there is space otherwise buffer full


	// Allocate file pointers object (struct fileops)
    struct fileops *fops = (struct fileops *)os_alloc(sizeof(struct fileops));
    if (!fops) {
        os_free(trace_file, sizeof(struct file));
        os_free(trace_buffer, sizeof(struct trace_buffer_info));
        return -ENOMEM;  // Memory allocation failed
    }

    //intitialise the file pointers object
    fops->read = trace_buffer_read;
    fops->write = trace_buffer_write;
    fops->close = trace_buffer_close;
    fops->lseek = NULL;

	// Initialize the file object
    trace_file->type = TRACE_BUFFER;
    trace_file->mode = mode;
    trace_file->offp = 0;
    trace_file->ref_count = 1;
    trace_file->inode = NULL;
    trace_file->trace_buffer = trace_buffer;
    trace_file->fops = fops;
	
	// Update the file descriptor in the current process's file descriptor array
    current->files[fd] = trace_file;

    // Return the allocated file descriptor number on success
    return fd;

}

///////////////////////////////////////////////////////////////////////////
//// 		Start of strace functionality 		      	      /////
///////////////////////////////////////////////////////////////////////////

int get_syscall_param_count(int syscall_num) {
    switch (syscall_num) {
        case SYSCALL_EXIT: return 1;
        case SYSCALL_GETPID: return 0;
        case SYSCALL_EXPAND: return 2;
        case SYSCALL_SHRINK: return 1;     //
        case SYSCALL_ALARM: return 1;      //
        case SYSCALL_SLEEP: return 1;
        case SYSCALL_SIGNAL: return 2;
        case SYSCALL_CLONE: return 2;
        case SYSCALL_FORK: return 0;
        case SYSCALL_STATS: return 0;
        case SYSCALL_CONFIGURE: return 1;
        case SYSCALL_PHYS_INFO: return 0;
        case SYSCALL_DUMP_PTT: return 1;
        case SYSCALL_CFORK: return 0;
        case SYSCALL_MMAP: return 4;
        case SYSCALL_MUNMAP: return 2;
        case SYSCALL_MPROTECT: return 3;
        case SYSCALL_PMAP: return 1;
        case SYSCALL_VFORK: return 0;
        case SYSCALL_GET_USER_P: return 0;
        case SYSCALL_GET_COW_F: return 0;
        case SYSCALL_OPEN: return 2;
        case SYSCALL_READ: return 3;
        case SYSCALL_WRITE: return 3;
        case SYSCALL_DUP: return 1;
        case SYSCALL_DUP2: return 2;
        case SYSCALL_CLOSE: return 1;
        case SYSCALL_LSEEK: return 3;
        case SYSCALL_FTRACE: return 4;
        case SYSCALL_TRACE_BUFFER: return 1;
        case SYSCALL_START_STRACE: return 2;
        case SYSCALL_END_STRACE: return 0;
        case SYSCALL_READ_STRACE: return 3;
        case SYSCALL_STRACE: return 2;
        case SYSCALL_READ_FTRACE: return 3;
        case SYSCALL_GETPPID: return 0;

        default:
            return -1;  // Invalid syscall number
    }
}


int perform_tracing(u64 syscall_num, u64 param1, u64 param2, u64 param3, u64 param4)
{
    struct exec_context* current = get_current_ctx();
    if(!(current -> st_md_base))
        return 0;
    
    if(!(current -> st_md_base -> is_traced))
        return 0;

    if(syscall_num == 38 || syscall_num == 40 || syscall_num == 37) return 0;

    if(current -> st_md_base -> tracing_mode == FILTERED_TRACING){
        struct strace_info *curr_syscall = current -> st_md_base -> next;
        while((curr_syscall) && curr_syscall -> syscall_num != syscall_num){
            curr_syscall = curr_syscall -> next;
        }
        if(!curr_syscall)
            return 0;
    }
    int num_params = get_syscall_param_count(syscall_num);
    if(num_params == -1){
        return 0;
    }
    struct file* trace_buffer = current -> files[current -> st_md_base -> strace_fd];

    u64 * temp_buffer = os_page_alloc(USER_REG);

    temp_buffer[0] = syscall_num;
    if(num_params >= 1){
        temp_buffer[1] = param1;
    }
    if(num_params >= 2){
        temp_buffer[2] = param2;
    }
    if(num_params >= 3){
        temp_buffer[3] = param3;
    }
    if(num_params == 4){
        temp_buffer[4] = param4;
    }
    
    trace_buffer_write_duplicate(trace_buffer, (char*) temp_buffer, num_params*8 + 8);
    os_page_free(USER_REG,temp_buffer);

    return 0;
}


int sys_strace(struct exec_context *current, int syscall_num, int action)
{
    if(!current)
        return -EINVAL;

    if(action != ADD_STRACE && action !=REMOVE_STRACE)
        return -EINVAL;

    if(!(current -> st_md_base)){
        current -> st_md_base = os_alloc(sizeof(struct strace_head));

        // how to initialise this strace head //
        current -> st_md_base -> is_traced = 1;
        current -> st_md_base -> count = 0;
        current -> st_md_base -> next = NULL;
        current -> st_md_base -> last = NULL;

    }

    // if(!(current -> st_md_base -> is_traced))
    //     return -EINVAL;

    if(action == ADD_STRACE){
        if(current -> st_md_base -> count >= MAX_STRACE)
            return -EINVAL;

        struct strace_info *curr_syscall = current -> st_md_base -> next;
        while((curr_syscall) && curr_syscall -> syscall_num != syscall_num){
            curr_syscall = curr_syscall -> next;
        }
        if(curr_syscall){
            return -EINVAL;
        }
        struct strace_info* new_syscall = os_alloc(sizeof(struct strace_info));
        new_syscall -> syscall_num = syscall_num;
        new_syscall -> next = NULL;
        if(current -> st_md_base -> next){
            current -> st_md_base -> last -> next = new_syscall;
            current -> st_md_base -> last = new_syscall;
        }
        else{
            current -> st_md_base -> last = new_syscall;
            current -> st_md_base -> next = new_syscall;
        }
        current -> st_md_base -> count++;
        
    }
    else if(action == REMOVE_STRACE){
        if(!(current -> st_md_base -> next)){
            return -EINVAL;
        }
        struct strace_info *curr_syscall = current -> st_md_base -> next;
        struct strace_info *prev_syscall = NULL;
        while((curr_syscall) && curr_syscall -> syscall_num != syscall_num){
            prev_syscall = curr_syscall;
            curr_syscall = curr_syscall -> next;
        }
        if(!curr_syscall){
            return -EINVAL;
        }
        else{
            if(prev_syscall == NULL){
                current -> st_md_base -> next = curr_syscall -> next;
                if(curr_syscall -> next == NULL){
                    current -> st_md_base -> last = NULL;
                }
                os_free(curr_syscall,sizeof(struct strace_info));
            }
            else{
                prev_syscall -> next = curr_syscall -> next;
                if(curr_syscall -> next == NULL){
                    current -> st_md_base -> last = prev_syscall;
                }
                os_free(curr_syscall,sizeof(struct strace_info));
            }
            current -> st_md_base -> count--;
        }
    }
	return 0;
}

int sys_read_strace(struct file *filep, char *buff, u64 count)
{
    if(!filep)
        return -EINVAL;

    if(filep -> trace_buffer -> read_offset == filep -> trace_buffer -> write_offset){
        return 0;
    }
    
    int bytes_read=0;
    while(count--){
        if(filep -> trace_buffer -> read_offset == filep -> trace_buffer -> write_offset){
            return bytes_read;
        }

        trace_buffer_read_duplicate(filep, buff + bytes_read, 8);
        bytes_read+=8;
        u64 num_params = get_syscall_param_count(((u64*)buff)[bytes_read/8-1]);
        for(int i=0;i<num_params;i++){
            trace_buffer_read_duplicate(filep, buff + bytes_read, 8);
            bytes_read+=8;
        }
    }

	return bytes_read;
}

int sys_start_strace(struct exec_context *current, int fd, int tracing_mode)
{
    if (tracing_mode != FULL_TRACING && tracing_mode != FILTERED_TRACING)
        return -EINVAL;        // Invalid tracing mode

    if(!current)               //Invalid context
        return -EINVAL;

    if (!(current->st_md_base)){
		current->st_md_base = os_alloc(sizeof(struct strace_head));     //Allocating memory
        current->st_md_base->count = 0;
        current->st_md_base->is_traced = 1;
        current->st_md_base->strace_fd = fd;
        current->st_md_base->tracing_mode = tracing_mode;
        current->st_md_base->next = NULL;                  //no first syscall
        current->st_md_base->last = NULL;                  //no last syscall
    }
    else{
        current->st_md_base->strace_fd = fd;
        current->st_md_base->tracing_mode = tracing_mode;
    }

    return 0;


}

int sys_end_strace(struct exec_context *current)
{
    if (!current){
		return -EINVAL;
	}

    if (!(current->st_md_base)){
		return -EINVAL;
	}

    struct strace_info *curr = current->st_md_base->next; //first syscall

    while (curr) {
            struct strace_info *temp = curr;
            curr = curr->next;                            //iterate over all syscalls
            os_free(temp, sizeof(struct strace_info));    //free them one by one
    }

    os_free(current->st_md_base, sizeof(struct strace_head));   //free the head
    current -> st_md_base = NULL;
    return 0;
}



///////////////////////////////////////////////////////////////////////////
//// 		Start of ftrace functionality 		      	      /////
///////////////////////////////////////////////////////////////////////////


long do_ftrace(struct exec_context *ctx, unsigned long faddr, long action, long nargs, int fd_trace_buffer)
{
    if(!ctx)
        return -EINVAL;
    if(nargs<0)
        return -EINVAL;
    if(action != ADD_FTRACE && action != REMOVE_FTRACE && action != ENABLE_FTRACE && action != DISABLE_FTRACE && action != ENABLE_BACKTRACE && action != DISABLE_BACKTRACE)
        return -EINVAL;

    if(action == ADD_FTRACE){
        if(ctx -> ft_md_base -> count >= FTRACE_MAX)
            return -EINVAL;
        struct ftrace_info *curr_func_call = ctx -> ft_md_base -> next;
        while((curr_func_call) && curr_func_call -> faddr != faddr){
            curr_func_call = curr_func_call -> next;
        }
        if(curr_func_call){
            return -EINVAL;
        }
        struct ftrace_info* new_func_call = os_alloc(sizeof(struct ftrace_info));
        new_func_call -> faddr = faddr;
        new_func_call -> next = NULL;
        new_func_call -> num_args = nargs;
        new_func_call -> fd = fd_trace_buffer;
        new_func_call -> capture_backtrace = 0;

        if(ctx -> ft_md_base -> next){
            ctx -> ft_md_base -> last -> next = new_func_call;
            ctx -> ft_md_base -> last = new_func_call;
        }
        else{
            ctx -> ft_md_base -> last = new_func_call;
            ctx -> ft_md_base -> next = new_func_call;
        }
        ctx -> ft_md_base -> count++;
        return 0;
    }

    if(action == REMOVE_FTRACE){
        if(!(ctx -> ft_md_base -> next)){
            return -EINVAL;
        }
        struct ftrace_info *curr_func_call = ctx -> ft_md_base -> next;
        struct ftrace_info *prev_func_call = NULL;
        while((curr_func_call) && curr_func_call -> faddr != faddr){
            prev_func_call = curr_func_call;
            curr_func_call = curr_func_call -> next;
        }
        if(!curr_func_call){
            return -EINVAL;
        }
        else{
            if(prev_func_call == NULL){
                if(((u8*)(faddr))[0] == INV_OPCODE){
                    ((u8*)(faddr))[0] = curr_func_call -> code_backup[0];
                    ((u8*)(faddr))[1] = curr_func_call -> code_backup[1];
                    ((u8*)(faddr))[2] = curr_func_call -> code_backup[2];
                    ((u8*)(faddr))[3] = curr_func_call -> code_backup[3];
                }
                ctx -> ft_md_base -> next = curr_func_call -> next;
                if(curr_func_call -> next == NULL){
                    ctx -> ft_md_base -> last = NULL;
                }
                os_free(curr_func_call,sizeof(struct ftrace_info));
            }
            else{
                if(((u8*)(faddr))[0] == INV_OPCODE){
                    ((u8*)(faddr))[0] = curr_func_call -> code_backup[0];
                    ((u8*)(faddr))[1] = curr_func_call -> code_backup[1];
                    ((u8*)(faddr))[2] = curr_func_call -> code_backup[2];
                    ((u8*)(faddr))[3] = curr_func_call -> code_backup[3];
                }
                prev_func_call -> next = curr_func_call -> next;
                if(curr_func_call -> next == NULL){
                    ctx -> ft_md_base -> last = prev_func_call;
                }
                os_free(curr_func_call,sizeof(struct ftrace_info));
            }
            ctx -> ft_md_base -> count--;
        }
        return 0;
    }

    if(action == ENABLE_FTRACE){
        if(!(ctx -> ft_md_base -> next)){
            return -EINVAL;
        }
        struct ftrace_info *curr_func_call = ctx -> ft_md_base -> next;
        while((curr_func_call) && curr_func_call -> faddr != faddr){
            curr_func_call = curr_func_call -> next;
        }
        if(!curr_func_call){
            return -EINVAL;
        }

        if(((u8*)(faddr))[0] != INV_OPCODE){
            curr_func_call -> code_backup[0] = ((u8*)faddr)[0];
            curr_func_call -> code_backup[1] = ((u8*)faddr)[1];
            curr_func_call -> code_backup[2] = ((u8*)faddr)[2];
            curr_func_call -> code_backup[3] = ((u8*)faddr)[3];

            ((u8*)(faddr))[0] = INV_OPCODE;
            ((u8*)(faddr))[1] = INV_OPCODE;
            ((u8*)(faddr))[2] = INV_OPCODE;
            ((u8*)(faddr))[3] = INV_OPCODE;
        }
        else{
            return 0;
        }

        return 0;
    }

    if(action == DISABLE_FTRACE){
        if(!(ctx -> ft_md_base -> next)){
            return -EINVAL;
        }
        struct ftrace_info *curr_func_call = ctx -> ft_md_base -> next;
        while((curr_func_call) && curr_func_call -> faddr != faddr){
            curr_func_call = curr_func_call -> next;
        }
        if(!curr_func_call){
            return -EINVAL;
        }

        if(((u8*)(faddr))[0] == INV_OPCODE){
            ((u8*)(faddr))[0] = curr_func_call -> code_backup[0];
            ((u8*)(faddr))[1] = curr_func_call -> code_backup[1];
            ((u8*)(faddr))[2] = curr_func_call -> code_backup[2];
            ((u8*)(faddr))[3] = curr_func_call -> code_backup[3];
        }
        else{
            return 0;
        }

        return 0;
    }

    if(action == ENABLE_BACKTRACE){
        if(!(ctx -> ft_md_base -> next)){
            return -EINVAL;
        }
        struct ftrace_info *curr_func_call = ctx -> ft_md_base -> next;
        while((curr_func_call) && curr_func_call -> faddr != faddr){
            curr_func_call = curr_func_call -> next;
        }
        if(!curr_func_call){
            return -EINVAL;
        }

        if(((u8*)(faddr))[0] != INV_OPCODE){
            curr_func_call -> code_backup[0] = ((u8*)faddr)[0];
            curr_func_call -> code_backup[1] = ((u8*)faddr)[1];
            curr_func_call -> code_backup[2] = ((u8*)faddr)[2];
            curr_func_call -> code_backup[3] = ((u8*)faddr)[3];

            ((u8*)(faddr))[0] = INV_OPCODE;
            ((u8*)(faddr))[1] = INV_OPCODE;
            ((u8*)(faddr))[2] = INV_OPCODE;
            ((u8*)(faddr))[3] = INV_OPCODE;
        }

        curr_func_call -> capture_backtrace = 1;
        return 0;
    }

    if(action == DISABLE_BACKTRACE){
        if(!(ctx -> ft_md_base -> next)){
            return -EINVAL;
        }
        struct ftrace_info *curr_func_call = ctx -> ft_md_base -> next;
        while(!(curr_func_call) && curr_func_call -> faddr != faddr){
            curr_func_call = curr_func_call -> next;
        }
        if(!curr_func_call){
            return -EINVAL;
        }

        if(((u8*)(faddr))[0] == INV_OPCODE){
            ((u8*)(faddr))[0] = curr_func_call -> code_backup[0];
            ((u8*)(faddr))[1] = curr_func_call -> code_backup[1];
            ((u8*)(faddr))[2] = curr_func_call -> code_backup[2];
            ((u8*)(faddr))[3] = curr_func_call -> code_backup[3];
        }

        curr_func_call -> capture_backtrace = 0;
        return 0;
    }

    return -EINVAL;
}

//Fault handler
long handle_ftrace_fault(struct user_regs *regs)
{   
    struct exec_context* ctx = get_current_ctx();
    unsigned long faddress = regs -> entry_rip;

    //if we dont have to trace anything i.e. if the function is not added by ADD_FTRACE
    if(!(ctx -> ft_md_base -> next)){
        regs->entry_rsp -= 8;
        *((u64*)regs->entry_rsp) = regs->rbp;
        regs->rbp = regs->entry_rsp;
        regs->entry_rip += 4;

        return 0;
    }
    struct ftrace_info *curr_func_call = ctx -> ft_md_base -> next;
    while((curr_func_call) && curr_func_call -> faddr != faddress){
        curr_func_call = curr_func_call -> next;
    }
    if(!curr_func_call){
        regs->entry_rsp -= 8;
        *((u64*)regs->entry_rsp) = regs->rbp;
        regs->rbp = regs->entry_rsp;
        regs->entry_rip += 4;
        return 0;
    }



    struct file* buffer_file = ctx -> files[curr_func_call -> fd];
    u64 nargs = (curr_func_call -> num_args);
    u64* temp_buffer = (u64*)os_page_alloc(USER_REG);
    temp_buffer[0] = nargs +1; 
    temp_buffer[1] = faddress;

    
    u64 param1, param2, param3, param4, param5;

    if(nargs >= 1){
        param1 = regs -> rdi;
        temp_buffer[2] = param1;
    }
    if(nargs >= 2){
        param2 = regs -> rsi;
        temp_buffer[3] = param2;
    }
    if(nargs >= 3){
        param3 = regs -> rdx;
        temp_buffer[4] = param3;
    }
    if(nargs >= 4){
        param4 = regs -> rcx;
        temp_buffer[5] = param4;
    }
    if(nargs == 5){
        param5 = regs -> r8;
        temp_buffer[6] = param5;
    }
    
    u64 x = nargs+2;
    if(curr_func_call -> capture_backtrace == 0){
        temp_buffer[0] = x-1;
        trace_buffer_write_duplicate(buffer_file, (char*)(temp_buffer), 8*(x));
        os_page_free(USER_REG,temp_buffer);
        regs->entry_rsp -= 8;
        *((u64*)regs->entry_rsp) = regs->rbp;
        regs->rbp = regs->entry_rsp;
        regs->entry_rip += 4;
        return 0;
    }
    temp_buffer[x++]=faddress;

    if(*((u64*)regs -> entry_rsp) == END_ADDR){
        temp_buffer[0] = x-1;
        trace_buffer_write_duplicate(buffer_file, (char*)(temp_buffer), 8*(x));
        os_page_free(USER_REG,temp_buffer);
        regs->entry_rsp -= 8;
        *((u64*)regs->entry_rsp) = regs->rbp;
        regs->rbp = regs->entry_rsp;
        regs->entry_rip += 4;
        return 0;
    }
        
    else{
        temp_buffer[x++]=*((u64*)(regs -> entry_rsp));
    }

    u64 rbp_temp = (regs -> rbp);
    while(*((u64*)(rbp_temp + 8)) != END_ADDR){
        temp_buffer[x++] = *((u64*)(rbp_temp + 8));
        rbp_temp = *((u64*)(rbp_temp));
    }

    temp_buffer[0] = x-1;
    trace_buffer_write_duplicate(buffer_file, (char*)(temp_buffer), 8*(x));

    regs->entry_rsp -= 8;
    *((u64*)regs->entry_rsp) = regs->rbp;
    regs->rbp = regs->entry_rsp;
    regs->entry_rip += 4;
    os_page_free(USER_REG,temp_buffer);
    return 0;
}


int sys_read_ftrace(struct file *filep, char *buff, u64 count)
{
    
    if(!filep)
        return -EINVAL;

    if(filep -> trace_buffer -> read_offset == filep -> trace_buffer -> write_offset && filep -> trace_buffer -> space == 1){
        return 0;
    }
    
    int bytes_read = 0;
    u64* temp_buffer = (u64*)os_page_alloc(USER_REG);
    while(count--){
        
        if(filep -> trace_buffer -> read_offset == filep -> trace_buffer -> write_offset  && filep -> trace_buffer -> space == 1 ){
            filep -> trace_buffer -> space == 0;
            return bytes_read;
        }

        trace_buffer_read_duplicate(filep, (char*)temp_buffer, 8);

        u64 num_params = ((u64*)temp_buffer)[0];
        

        for(u64 i=0;i<num_params;i++){
            trace_buffer_read_duplicate(filep, buff + bytes_read, 8);
            bytes_read+=8;
        }
    }
    

    os_page_free(USER_REG,temp_buffer);

	return bytes_read;
}







