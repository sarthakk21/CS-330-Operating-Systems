#include <types.h>
#include <mmap.h>
#include <fork.h>
#include <v2p.h>
#include <page.h>

/* 
 * You may define macros and other helper functions here
 * You must not declare and use any static/global variables 
 * */
#define _4KB 4096 

void printBinaryWithUnderscores(u64 number) {
    u64 mask = 1ULL << 63; // Start with the leftmost bit
    int bitCount = 0; // Counter for tracking when to insert underscores
    int flag = 0;
    while (mask > 0) {
        if (flag == 0 && bitCount == 16) {
            printk("_"); // Insert the first underscore after 12 bits
            bitCount = 0;
            flag++;
        }
        if (flag > 0 && flag <= 4 && bitCount == 9) {
            printk("_"); // Insert underscores at intervals of 9 bits
            bitCount = 0;
            flag++;
        }

        if (number & mask) {
            printk("1");
        } else {
            printk("0");
        }

        mask >>= 1; // Move to the next bit
        bitCount++;
    }
    printk("\n");
}

long free_page(struct exec_context* current, u64 addr){
    if(current == NULL) return -EINVAL;
    if((addr < MMAP_AREA_START || addr > MMAP_AREA_END) && addr != 0) return -EINVAL;
    
    u64* virtual_pgd = osmap(current -> pgd);
    
    u64 pgd_offset = (addr >> 39) & 0x0000001FF;
    u64 pud_offset = (addr >> 30) & 0x0000001FF;
    u64 pmd_offset = (addr >> 21) & 0x0000001FF;
    u64 pte_offset = (addr >> 12) & 0x0000001FF;
    u64 pfn_offset = (addr) & 0x000000FFF;

    u64 pud_entry;
    u64 pmd_entry;
    u64 pte_entry;
    u64* virtual_pud_t;


    u64* virtual_pgd_t = virtual_pgd + pgd_offset;
    u64 pgd_entry = *(virtual_pgd_t);

    if(pgd_entry & 1 == 1){
        u64* virtual_pud = osmap(pgd_entry >> 12);
        virtual_pud_t = virtual_pud + pud_offset;
        pud_entry = *(virtual_pud_t); 
    }
    else{
        return 0;
    }

    u64* virtual_pmd_t;

    if(pud_entry & 1 == 1){
        u64* virtual_pmd = osmap(pud_entry >> 12);
        virtual_pmd_t = virtual_pmd + pmd_offset;
        pmd_entry = *(virtual_pmd_t); 
    }
    else{
        return 0;
    }

    u64* virtual_pte_t;

    if(pmd_entry & 1 == 1){
        u64* virtual_pte = osmap(pmd_entry >> 12);
        virtual_pte_t = virtual_pte + pte_offset;
        pte_entry = *(virtual_pte_t); 
    }
    else{
        return 0;
    }

    if(pte_entry & 1 == 1){
        if(get_pfn_refcount(pte_entry >> 12) == 1){
            put_pfn(pte_entry >> 12);
            os_pfn_free(USER_REG, ((u64)(*(virtual_pte_t)) >> 12));
        }
        
        *(virtual_pte_t) = 0;
        asm volatile("invlpg (%0)"::"r" (addr));
        return 0;       
    }
    else{
        return 0;
    }
}

long protect_page(struct exec_context* current, u64 addr, int prot){
    if(current == NULL) return -EINVAL;
    if((addr < MMAP_AREA_START || addr > MMAP_AREA_END) && addr != 0) return -EINVAL;
    if(prot != PROT_READ && prot != (PROT_READ | PROT_WRITE)) return -EINVAL;

    u64* virtual_pgd = (u64*)osmap(current -> pgd);

    u64 pgd_offset = (addr >> 39) & 0x0000001FF;
    u64 pud_offset = (addr >> 30) & 0x0000001FF;
    u64 pmd_offset = (addr >> 21) & 0x0000001FF;
    u64 pte_offset = (addr >> 12) & 0x0000001FF;
    u64 pfn_offset = (addr) & 0x000000FFF;

    u64 pud_entry;
    u64 pmd_entry;
    u64 pte_entry;
    u64* virtual_pud_t;


    u64* virtual_pgd_t = virtual_pgd + pgd_offset;
    u64 pgd_entry = *(virtual_pgd_t);
    if(pgd_entry & 1 == 1){
        u64* virtual_pud = osmap(pgd_entry >> 12);
        virtual_pud_t = virtual_pud + pud_offset;
        pud_entry = *(virtual_pud_t); 
    }
    else{
        return 0;
    }

    u64* virtual_pmd_t;

    if(pud_entry & 1 == 1){
        u64* virtual_pmd = osmap(pud_entry >> 12);
        virtual_pmd_t = virtual_pmd + pmd_offset;
        pmd_entry = *(virtual_pmd_t); 
    }
    else{
        return 0;
    }

    u64* virtual_pte_t;

    if(pmd_entry & 1 == 1){
        u64* virtual_pte = osmap(pmd_entry >> 12);
        virtual_pte_t = virtual_pte + pte_offset;
        pte_entry = *(virtual_pte_t); 
    }
    else{
        return 0;
    }

    if(pte_entry & 1 == 1){ 
        
        if(get_pfn_refcount(pte_entry >> 12) > 1){
            if(prot == PROT_READ){
                *(virtual_pte_t) = *(virtual_pte_t) & (u64)(~(1 << 3));
            }
            asm volatile("invlpg (%0)"::"r" (addr));
            return 0;
        }
        if(prot == PROT_READ){
            *(virtual_pte_t) = (*(virtual_pte_t) & ~0xF) | 0x11;
        }
        else{
            *(virtual_pte_t) = (*(virtual_pte_t) & ~0xF) | 0x19;
        }
        asm volatile("invlpg (%0)"::"r" (addr));
        return 0;       
    }
    else{
        return 0;
    }
}

void copy_pte_to_child(struct exec_context* parent_ctx, struct exec_context* child_ctx, u64 addr){

    u64* virtual_pgd_parent = (u64*)osmap(parent_ctx -> pgd);
    u64* virtual_pgd_child = (u64*)osmap(child_ctx -> pgd);

    u64 pgd_offset = (addr >> 39) & 0x0000001FF;
    u64 pud_offset = (addr >> 30) & 0x0000001FF;
    u64 pmd_offset = (addr >> 21) & 0x0000001FF;
    u64 pte_offset = (addr >> 12) & 0x0000001FF;
    u64 pfn_offset = (addr) & 0x000000FFF;

    u64 pud_entry_parent;
    u64 pud_entry_child;
    u64 pmd_entry_parent;
    u64 pmd_entry_child;
    u64 pte_entry_parent;
    u64 pte_entry_child;
    u64* virtual_pud_t_parent;
    u64* virtual_pud_t_child;    


    u64* virtual_pgd_t_parent = virtual_pgd_parent + pgd_offset;
    u64* virtual_pgd_t_child = virtual_pgd_child + pgd_offset;
    u64 pgd_entry_parent = *(virtual_pgd_t_parent);
    u64 pgd_entry_child = *(virtual_pgd_t_child);

    if(pgd_entry_parent & 1 == 1){
        u64* virtual_pud_parent = osmap((pgd_entry_parent >> 12));
        virtual_pud_t_parent = virtual_pud_parent + pud_offset;
        pud_entry_parent = *(virtual_pud_t_parent);
        if(pgd_entry_child & 1 == 1){
            *(virtual_pgd_t_child) = *(virtual_pgd_t_child) | 0x19;
            asm volatile("invlpg (%0)"::"r" (addr));
            u64* virtual_pud_child = osmap((pgd_entry_child >> 12));
            virtual_pud_t_child = virtual_pud_child + pud_offset;
            pud_entry_child = *(virtual_pud_t_child);
        }
        else{
            u32 pud = os_pfn_alloc(OS_PT_REG);
            *(virtual_pgd_t_child) = pud << 12;
            *(virtual_pgd_t_child) = *(virtual_pgd_t_child) | 0x19;
            pgd_entry_child = *(virtual_pgd_t_child);
            asm volatile("invlpg (%0)"::"r" (addr));
            u64* virtual_pud_child = osmap(pud);
            virtual_pud_t_child = virtual_pud_child + pud_offset;
            pud_entry_child = *(virtual_pud_t_child);
        }
    }
    else{
        *(virtual_pgd_t_child) = 0;
        asm volatile("invlpg (%0)"::"r" (addr));
        return;
    }

    u64* virtual_pmd_t_parent;
    u64* virtual_pmd_t_child;
    

    if(pud_entry_parent & 1 == 1){
        u64* virtual_pmd_parent = osmap((pud_entry_parent >> 12));
        virtual_pmd_t_parent = virtual_pmd_parent + pmd_offset;
        pmd_entry_parent = *(virtual_pmd_t_parent);
        if(pud_entry_child & 1 == 1){
            *(virtual_pud_t_child) = *(virtual_pud_t_child) | 0x19;
            asm volatile("invlpg (%0)"::"r" (addr));
            u64* virtual_pmd_child = osmap((pud_entry_child >> 12));            
            virtual_pmd_t_child = virtual_pmd_child + pmd_offset;
            pmd_entry_child = *(virtual_pmd_t_child);
        } 
        else{
            u32 pmd = os_pfn_alloc(OS_PT_REG);
            *(virtual_pud_t_child) = pmd << 12;
            *(virtual_pud_t_child) = *(virtual_pud_t_child) | 0x19;
            pud_entry_child = *(virtual_pud_t_child);
            asm volatile("invlpg (%0)"::"r" (addr));
            u64* virtual_pmd_child = osmap(pmd);
            virtual_pmd_t_child = virtual_pmd_child + pmd_offset;
            pmd_entry_child = *(virtual_pmd_t_child);
        }
    }
    else{
        *(virtual_pud_t_child) = 0;
        asm volatile("invlpg (%0)"::"r" (addr));
        return;
    }


    u64* virtual_pte_t_parent;
    u64* virtual_pte_t_child;

    if(pmd_entry_parent & 1 == 1){
        u64* virtual_pte_parent = osmap(pmd_entry_parent >> 12);
        virtual_pte_t_parent = virtual_pte_parent + pte_offset;
        pte_entry_parent = *(virtual_pte_t_parent);
        if(pmd_entry_child & 1 == 1){
            *(virtual_pmd_t_child) = *(virtual_pmd_t_child) | 0x19;
            asm volatile("invlpg (%0)"::"r" (addr));
            u64* virtual_pte_child = osmap(pmd_entry_child >> 12);            
            virtual_pte_t_child = virtual_pte_child + pte_offset;
            pte_entry_child = *(virtual_pte_t_child);
        } 
        else{
            u32 pte = os_pfn_alloc(OS_PT_REG);
            *(virtual_pmd_t_child) = pte << 12;
            *(virtual_pmd_t_child) = *(virtual_pmd_t_child) | 0x19;
            pmd_entry_child = *(virtual_pmd_t_child);
            asm volatile("invlpg (%0)"::"r" (addr));
            u64* virtual_pte_child = osmap(pte);
            virtual_pte_t_child = virtual_pte_child + pte_offset;
            pte_entry_child = *(virtual_pte_t_child);
        }
    }
    else{
        *(virtual_pmd_t_child) = 0;
        asm volatile("invlpg (%0)"::"r" (addr));
        return; 
    }

    if(pte_entry_parent & 1 == 1){
        *(virtual_pte_t_parent) = *(virtual_pte_t_parent) & (~(1 << 3));
        *(virtual_pte_t_parent) = *(virtual_pte_t_parent) | 0x11;
        *(virtual_pte_t_child) = *(virtual_pte_t_parent);
        get_pfn(*(virtual_pte_t_child) >> 12);
        asm volatile("invlpg (%0)"::"r" (addr));
    }
    else{
        *(virtual_pte_t_child) = 0;
        asm volatile("invlpg (%0)"::"r" (addr));
        return;
    }

    return;
}

/**
 * mprotect System call Implementation.
 */

long vm_area_mprotect(struct exec_context *current, u64 addr, int length, int prot)
{
    if(length <= 0) return -EINVAL;
    if(!current) return -EINVAL;
    if(!(prot == PROT_READ || (prot == (PROT_READ | PROT_WRITE)))) return -EINVAL;

    if(length % _4KB != 0){
        length = length / _4KB * _4KB + _4KB;
    }
    
    if(current -> vm_area == NULL) return 0;

    struct vm_area* curr = current -> vm_area -> vm_next;
    struct vm_area* prev = NULL;

    struct vm_area* left = NULL;
    struct vm_area* right = NULL;

    while(curr != NULL){
        if(addr == MMAP_AREA_START + _4KB){
            left = current -> vm_area;
            break;
        }
        prev = curr;
        curr = curr -> vm_next;
        
        if(prev -> vm_end > addr){
            left = prev;
            break;
        }
        if(curr != NULL && curr -> vm_end > addr){
            if(curr ->vm_start < addr) left = curr;
            else left = prev;
            break;
        }
        
    }
    if(left == NULL) return 0;

    curr = current -> vm_area -> vm_next;
    prev = NULL;

    while(curr != NULL){
        prev = curr;
        curr = curr -> vm_next;
        
        if(prev -> vm_end > addr + length){
            right = prev;
            break;
        }
        if(curr != NULL && curr -> vm_end > addr + length){
            right = curr;
            break;
        }
        
    }
    
    if(left == right){
        if(left -> access_flags == prot) return 0;
        struct vm_area* new_vm_left = os_alloc(sizeof(struct vm_area));
        struct vm_area* new_vm_right = os_alloc(sizeof(struct vm_area));
        new_vm_right -> vm_start = addr + length;
        new_vm_right -> vm_end = right -> vm_end;
        new_vm_right -> access_flags = right -> access_flags;
        new_vm_right -> vm_next = right -> vm_next;
        new_vm_left -> vm_start = addr;
        new_vm_left -> vm_end = addr + length;
        new_vm_left -> vm_next = new_vm_right;
        new_vm_left -> access_flags = prot;
        left -> vm_end = addr;
        left -> vm_next = new_vm_left;
        stats -> num_vm_area += 2;

        //Changing protection of the PFNs
        int pages = (new_vm_left -> vm_end - new_vm_left -> vm_start) / _4KB;
        for(int i = 0 ; i < pages ; i++){
            protect_page(current, new_vm_left -> vm_start + i * _4KB, prot);
        }
        return 0;
    }

    if(right != NULL && left -> vm_next == right && left -> vm_end == right -> vm_start){
        if(left -> access_flags == prot){
            //Changing protection of the PFNs
            int pages = (addr + length - left -> vm_end) / _4KB;
            for(int i = 0 ; i < pages ; i++){
                protect_page(current, left -> vm_end + i * _4KB, prot);
            }

            left -> vm_end = addr + length;
            right -> vm_start = addr + length;
            return 0;
        }
        if(right -> access_flags == prot){
            //Changing protection of the PFNs
            int pages = (right -> vm_start - addr) / _4KB;
            for(int i = 0 ; i < pages ; i++){
                protect_page(current, addr + i * _4KB, prot);
            }

            right -> vm_start == addr;
            left -> vm_end == addr;
            return 0;
        }
    }

    struct vm_area* temp = left -> vm_next;
    struct vm_area* temp_prev = left;
    while(temp != right){
        temp -> access_flags = prot;
        //Changing protection of the PFNs
        int pages = (temp -> vm_end - temp -> vm_start) / _4KB;
        for(int i = 0 ; i < pages ; i++){
            protect_page(current, temp -> vm_start + i * _4KB, prot);
        }

        if(temp_prev ->  access_flags == temp -> access_flags && temp_prev -> vm_end == temp -> vm_start){
            temp_prev -> vm_end = temp -> vm_end;
            temp_prev -> vm_next = temp -> vm_next;
            os_free(temp, sizeof(struct vm_area));
            temp = temp_prev -> vm_next;
            stats -> num_vm_area --;
            continue;
        }
        temp_prev = temp;
        temp = temp -> vm_next;
    }
    if(right != NULL && temp_prev -> access_flags == temp -> access_flags && temp_prev -> vm_end == temp -> vm_start){
        temp_prev -> vm_end = temp -> vm_end;
        temp_prev -> vm_next = temp -> vm_next;
        os_free(temp,sizeof(struct vm_area));
        stats -> num_vm_area --;
        right = temp_prev;
    }
    if(right == NULL){
        if(left -> vm_end <= addr){
            return 0;
        }

        if(left -> vm_end > addr){
            if(left -> access_flags == prot) return 0;
            struct vm_area* vm_right = os_alloc(sizeof(struct vm_area));
            vm_right -> vm_start = addr;
            vm_right -> vm_end = left -> vm_end;
            vm_right -> access_flags = prot;
            vm_right -> vm_next = left -> vm_next;

            //Changing protection of the PFNs
            int pages = (vm_right -> vm_end - vm_right -> vm_start) / _4KB;
            for(int i = 0 ; i < pages ; i++){
                protect_page(current, vm_right -> vm_start + i * _4KB, prot);
            }

            left -> vm_end = addr;
            left -> vm_next = vm_right;
            stats -> num_vm_area ++;

            //MERGEABILITY 
            if(vm_right -> access_flags == vm_right -> vm_next -> access_flags && vm_right -> vm_end == vm_right -> vm_next -> vm_start){
                vm_right -> vm_end = vm_right -> vm_next -> vm_end;
                struct vm_area* del = vm_right -> vm_next;
                vm_right -> vm_next = vm_right -> vm_next -> vm_next;
                os_free(del,sizeof(struct vm_area));
                stats -> num_vm_area --;
            }

            return 0;
        }
    }
    if(left -> vm_end <= addr && right -> vm_start >= addr + length){
        return 0;
    }
    if(left -> vm_end > addr && right -> vm_start >= addr + length){
        if(left -> access_flags == prot) return 0;
        struct vm_area* vm_right = os_alloc(sizeof(struct vm_area));
        vm_right -> vm_start = addr;
        vm_right -> vm_end = left -> vm_end;
        vm_right -> access_flags = prot;
        vm_right -> vm_next = left -> vm_next;

        //Changing protection of the PFNs
        int pages = (vm_right -> vm_end - vm_right -> vm_start) / _4KB;
        for(int i = 0 ; i < pages ; i++){
            protect_page(current, vm_right -> vm_start + i * _4KB, prot);
        }

        left -> vm_end = addr;
        left -> vm_next = vm_right;
        stats -> num_vm_area ++;
        if(vm_right -> access_flags == vm_right -> vm_next -> access_flags && vm_right -> vm_end == vm_right -> vm_next -> vm_start){
            vm_right -> vm_end = vm_right -> vm_next -> vm_end;
            struct vm_area* del = vm_right -> vm_next;
            vm_right -> vm_next = vm_right -> vm_next -> vm_next;
            os_free(del,sizeof(struct vm_area));
            stats -> num_vm_area --;
        }
        return 0;
    }

    if(left -> vm_end <= addr && right -> vm_start < addr + length){
        if(right -> access_flags == prot) return 0;
        struct vm_area* vm_left = os_alloc(sizeof(struct vm_area));
        vm_left -> vm_end = addr + length;
        vm_left -> vm_start = right -> vm_start;
        vm_left -> access_flags = prot;
        vm_left -> vm_next = right;

        //Changing protection of the PFNs
        int pages = (vm_left -> vm_end - vm_left -> vm_start) / _4KB;
        for(int i = 0 ; i < pages ; i++){
            protect_page(current, vm_left -> vm_start + i * _4KB, prot);
        }

        right -> vm_start = addr + length;
        temp_prev -> vm_next = vm_left;
        stats -> num_vm_area ++;
        if(temp_prev -> access_flags == vm_left -> access_flags && temp_prev -> vm_end == vm_left -> vm_start){
            temp_prev -> vm_end = vm_left -> vm_end;
            temp_prev -> vm_next = vm_left -> vm_next;
            os_free(vm_left, sizeof(struct vm_area));
            stats -> num_vm_area --;
        }
        return 0;
    }
    if(left -> vm_end > addr && right -> vm_start < addr + length){
        if(left -> access_flags == prot && right -> access_flags == prot) return 0;
        if(left -> access_flags == prot){
            struct vm_area* vm_left = os_alloc(sizeof(struct vm_area));
            vm_left -> vm_end = addr + length;
            vm_left -> vm_start = right -> vm_start;
            vm_left -> access_flags = prot;
            vm_left -> vm_next = right;

            //Changing protection of the PFNs
            int pages = (vm_left -> vm_end - vm_left -> vm_start) / _4KB;
            for(int i = 0 ; i < pages ; i++){
                protect_page(current, vm_left -> vm_start + i * _4KB, prot);
            }

            right -> vm_start = addr + length;
            temp_prev -> vm_next = vm_left;
            stats -> num_vm_area ++;
            if(temp_prev -> access_flags == vm_left -> access_flags && temp_prev -> vm_end == vm_left -> vm_start){
                temp_prev -> vm_end = vm_left -> vm_end;
                temp_prev -> vm_next = vm_left -> vm_next;
                os_free(vm_left, sizeof(struct vm_area));
                stats -> num_vm_area --;
            }
            return 0;
        }
        if(right -> access_flags == prot){
            struct vm_area* vm_right = os_alloc(sizeof(struct vm_area));
            vm_right -> vm_start = addr;
            vm_right -> vm_end = left -> vm_end;
            vm_right -> access_flags = prot;
            vm_right -> vm_next = left -> vm_next;

            //Changing protection of the PFNs
            int pages = (vm_right -> vm_end - vm_right -> vm_start) / _4KB;
            for(int i = 0 ; i < pages ; i++){
                protect_page(current, vm_right -> vm_start + i * _4KB, prot);
            }

            left -> vm_end = addr;
            left -> vm_next = vm_right;
            stats -> num_vm_area ++;
            if(vm_right -> access_flags == vm_right -> vm_next -> access_flags && vm_right -> vm_end == vm_right -> vm_next -> vm_start){
                vm_right -> vm_end = vm_right -> vm_next -> vm_end;
                struct vm_area* del = vm_right -> vm_next;
                vm_right -> vm_next = vm_right -> vm_next -> vm_next;
                os_free(del,sizeof(struct vm_area));
                stats -> num_vm_area --;
            }
            return 0;
        }
        struct vm_area* left_side = os_alloc(sizeof(struct vm_area));
        struct vm_area* right_side = os_alloc(sizeof(struct vm_area));

        left_side -> vm_start = addr;
        left_side -> vm_end = left -> vm_end;
        left_side -> access_flags = prot;
        left_side -> vm_next = left -> vm_next;
        right_side -> vm_start = right -> vm_start;
        right_side -> vm_end = addr + length;
        right_side -> access_flags = prot;
        right_side -> vm_next = right;

        //Changing protection of the PFNs
        int pages = (left_side -> vm_end - left_side -> vm_start) / _4KB;
        for(int i = 0 ; i < pages ; i++){
            protect_page(current, left_side -> vm_start + i * _4KB, prot);
        }

        //Changing protection of the PFNs
        pages = (right_side -> vm_end - right_side -> vm_start) / _4KB;
        for(int i = 0 ; i < pages ; i++){
            protect_page(current, right_side -> vm_start + i * _4KB, prot);
        }

        left -> vm_end = addr;
        left -> vm_next = left_side;
        right -> vm_start = addr + length;
        temp_prev -> vm_next = right_side;
        stats -> num_vm_area += 2;
        if(left_side -> access_flags == left_side -> vm_next -> access_flags && left_side -> vm_end == left_side -> vm_next -> vm_start){
            left_side -> vm_end = left_side -> vm_next -> vm_end;
            struct vm_area* del = left_side -> vm_next;
            if(left_side -> vm_next == temp_prev){
                temp_prev = left_side;
            }
            left_side -> vm_next = left_side -> vm_next -> vm_next;
            os_free(del,sizeof(struct vm_area));
            stats -> num_vm_area --;
        }
        if(temp_prev -> access_flags == right_side -> access_flags && temp_prev -> vm_end == right_side -> vm_start){
            temp_prev -> vm_end = right_side -> vm_end;
            temp_prev -> vm_next = right_side -> vm_next;
            os_free(right_side,sizeof(struct vm_area));
            stats -> num_vm_area --;
        }
        return 0;
    }

    return -EINVAL;
}

/**
 * mmap system call implementation.
 */

long vm_area_map(struct exec_context *current, u64 addr, int length, int prot, int flags)
{
    // printk("HAHA\n");
    if(stats -> num_vm_area == 128) return -EINVAL;
    if(flags == MAP_FIXED && !addr) return -EINVAL;
    if((addr < MMAP_AREA_START +_4KB || addr > MMAP_AREA_END) && addr != 0) return -EINVAL;
    if(length <= 0 || length > 2*1024*1024) return -EINVAL;
    if(!current) return -EINVAL;
    if(!(prot == PROT_READ || (prot == (PROT_READ | PROT_WRITE)))) return -EINVAL;
    if(!(flags == 0 || flags == MAP_FIXED)) return -EINVAL;

    if(length % _4KB != 0){
        length = length / _4KB *_4KB + _4KB;
    }

    if(!(current -> vm_area)){
        struct vm_area* vma = os_alloc(sizeof(struct vm_area));
        vma -> vm_start = MMAP_AREA_START;
        vma -> vm_end = MMAP_AREA_START + _4KB;
        vma -> access_flags = 0;
        vma -> vm_next = NULL;
        current -> vm_area = vma;
        stats ->num_vm_area ++;
    }

    if(addr == 0){
        struct vm_area* curr = current -> vm_area;
        struct vm_area* prev = NULL;
        if(curr -> vm_next == NULL){     //first allocation
            struct vm_area* new_vm = os_alloc(sizeof(struct vm_area));
            new_vm -> vm_start = curr -> vm_end;
            new_vm -> vm_end = curr -> vm_end + length;
            new_vm -> access_flags = prot;
            curr -> vm_next = new_vm;
            new_vm -> vm_next = NULL;
            stats -> num_vm_area ++;
            return curr -> vm_end;
        }
        


        curr = curr -> vm_next;
        prev = NULL;
        while(curr != NULL){
            prev = curr;
            curr = curr -> vm_next;
            if(curr != NULL && curr -> vm_start - prev -> vm_end >= length){

                if(prev -> access_flags == prot && prev -> vm_end + length == curr -> vm_start
                 && curr -> access_flags == prot){
                    unsigned long old_start = prev -> vm_end;
                    prev -> vm_end = curr -> vm_end;
                    prev -> vm_next = curr -> vm_next;
                    os_free(curr, sizeof(struct vm_area));
                    stats -> num_vm_area --;
                    return old_start;
                }

                if(prev -> access_flags == prot){
                    unsigned long old_start = prev -> vm_end;
                    prev -> vm_end = prev -> vm_end + length;
                    return old_start;
                }

                if(curr -> access_flags == prot && curr -> vm_start - prev -> vm_end == length){
                    curr -> vm_start = curr -> vm_start - length;
                    return curr -> vm_start;
                }

                struct vm_area* new_vm = os_alloc(sizeof(struct vm_area));
                new_vm -> vm_start = prev -> vm_end;
                new_vm -> vm_end = prev -> vm_end + length;
                new_vm -> access_flags = prot;
                prev -> vm_next = new_vm;
                new_vm -> vm_next = curr;
                stats -> num_vm_area ++;
                return prev -> vm_end;
            }
        }
        if(curr == NULL){    //empty space found at the end

            if(prev -> access_flags == prot){
                unsigned long old_start = prev -> vm_end;
                prev -> vm_end = prev -> vm_end + length;
                return old_start;
            }

            struct vm_area* new_vm = os_alloc(sizeof(struct vm_area));
            new_vm -> vm_start = prev -> vm_end;
            new_vm -> vm_end = prev -> vm_end + length;
            new_vm -> access_flags = prot;
            prev -> vm_next = new_vm;
            new_vm -> vm_next = NULL;
            stats -> num_vm_area ++;
            return prev -> vm_end;
        }
    }
    if(flags == MAP_FIXED){
        struct vm_area* curr = current -> vm_area;
        struct vm_area* prev = NULL;

        if(curr -> vm_next == NULL){     //first allocation
            struct vm_area* new_vm = os_alloc(sizeof(struct vm_area));
            new_vm -> vm_start = addr;
            new_vm -> vm_end = addr + length;
            new_vm -> access_flags = prot;
            curr -> vm_next = new_vm;
            new_vm -> vm_next = NULL;
            stats -> num_vm_area ++;
            return new_vm -> vm_start;
        }

        curr = curr -> vm_next;
        prev = NULL;

        while(curr != NULL){
            prev = curr;
            curr = curr -> vm_next;
            if(curr != NULL && curr -> vm_start >= addr){
                
                if(prev -> vm_end > addr) return -EINVAL;
                if(addr + length > curr -> vm_start) return -EINVAL;

                if((prev -> access_flags == prot && prev -> vm_end == addr) 
                && (curr -> access_flags == prot && curr -> vm_start == addr + length)){

                    unsigned long old_start = prev -> vm_end;
                    prev -> vm_end = curr -> vm_end;
                    prev -> vm_next = curr -> vm_next;
                    os_free(curr, sizeof(struct vm_area));
                    stats -> num_vm_area --;
                    return old_start;
                }

                if(prev -> access_flags == prot && prev -> vm_end == addr){
                    unsigned long old_start = prev -> vm_end;
                    prev -> vm_end = prev -> vm_end + length;
                    return old_start;
                }

                if(curr -> access_flags == prot && curr -> vm_start == addr + length){
                    curr -> vm_start = curr -> vm_start - length;
                    return curr -> vm_start;
                }

                struct vm_area* new_vm = os_alloc(sizeof(struct vm_area));
                new_vm -> vm_start = addr;
                new_vm -> vm_end = addr + length;
                new_vm -> access_flags = prot;
                prev -> vm_next = new_vm;
                new_vm -> vm_next = curr;
                stats -> num_vm_area ++;
                return addr;
            }
        }
        if(curr == NULL){    //empty space found at the end

            if(prev -> access_flags == prot && prev -> vm_end ==addr){
                unsigned long old_start = prev -> vm_end;
                prev -> vm_end = prev -> vm_end + length;
                return old_start;
            }

            struct vm_area* new_vm = os_alloc(sizeof(struct vm_area));
            new_vm -> vm_start = addr;
            new_vm -> vm_end = addr + length;
            new_vm -> access_flags = prot;
            prev -> vm_next = new_vm;
            new_vm -> vm_next = NULL;
            stats -> num_vm_area ++;
            return addr;
        }        
    }


    struct vm_area* curr = current -> vm_area;
    struct vm_area* prev = NULL;

    if(curr -> vm_next == NULL){     //first allocation
        struct vm_area* new_vm = os_alloc(sizeof(struct vm_area));
        new_vm -> vm_start = addr;
        new_vm -> vm_end = addr + length;
        new_vm -> access_flags = prot;
        curr -> vm_next = new_vm;
        new_vm -> vm_next = NULL;
        stats -> num_vm_area ++;
        return new_vm -> vm_start;
    }

    curr = curr -> vm_next;
    prev = NULL;

    while(curr != NULL){
        prev = curr;
        curr = curr -> vm_next;
        if(curr != NULL && curr -> vm_start >= addr){
            
            if(prev -> vm_end > addr || addr + length > curr -> vm_start){
                curr = current -> vm_area;
                prev = NULL;
                if(curr -> vm_next == NULL){     //first allocation
                    struct vm_area* new_vm = os_alloc(sizeof(struct vm_area));
                    new_vm -> vm_start = curr -> vm_end;
                    new_vm -> vm_end = curr -> vm_end + length;
                    new_vm -> access_flags = prot;
                    curr -> vm_next = new_vm;
                    new_vm -> vm_next = NULL;
                    stats -> num_vm_area ++;
                    return curr -> vm_end;
                }
                


                curr = curr -> vm_next;
                prev = NULL;
                while(curr != NULL){

                    prev = curr;
                    curr = curr -> vm_next;
                    if(curr != NULL && curr -> vm_start - prev -> vm_end >= length){

                        if(prev -> access_flags == prot && prev -> vm_end + length == curr -> vm_start
                        && curr -> access_flags == prot){
                            unsigned long old_start = prev -> vm_end;
                            prev -> vm_end = curr -> vm_end;
                            prev -> vm_next = curr -> vm_next;
                            os_free(curr, sizeof(struct vm_area));
                            stats -> num_vm_area --;
                            return old_start;
                        }

                        if(prev -> access_flags == prot){
                            unsigned long old_start = prev -> vm_end;
                            prev -> vm_end = prev -> vm_end + length;
                            return old_start;
                        }

                        if(curr -> access_flags == prot && curr -> vm_start - prev -> vm_end == length){
                            curr -> vm_start = curr -> vm_start - length;
                            return curr -> vm_start;
                        }

                        struct vm_area* new_vm = os_alloc(sizeof(struct vm_area));
                        new_vm -> vm_start = prev -> vm_end;
                        new_vm -> vm_end = prev -> vm_end + length;
                        new_vm -> access_flags = prot;
                        prev -> vm_next = new_vm;
                        new_vm -> vm_next = curr;
                        stats -> num_vm_area ++;
                        return prev -> vm_end;
                    }
                }
                if(curr == NULL){    //empty space found at the end

                    if(prev -> access_flags == prot){
                        unsigned long old_start = prev -> vm_end;
                        prev -> vm_end = prev -> vm_end + length;
                        return old_start;
                    }

                    struct vm_area* new_vm = os_alloc(sizeof(struct vm_area));
                    new_vm -> vm_start = prev -> vm_end;
                    new_vm -> vm_end = prev -> vm_end + length;
                    new_vm -> access_flags = prot;
                    prev -> vm_next = new_vm;
                    new_vm -> vm_next = NULL;
                    stats -> num_vm_area ++;
                    return prev -> vm_end;
                }
            }

            if((prev -> access_flags == prot && prev -> vm_end == addr) 
            && (curr -> access_flags == prot && curr -> vm_start == addr + length)){

                unsigned long old_start = prev -> vm_end;
                prev -> vm_end = curr -> vm_end;
                prev -> vm_next = curr -> vm_next;
                os_free(curr, sizeof(struct vm_area));
                stats -> num_vm_area --;
                return old_start;
            }

            if(prev -> access_flags == prot && prev -> vm_end == addr){
                unsigned long old_start = prev -> vm_end;
                prev -> vm_end = prev -> vm_end + length;
                return old_start;
            }

            if(curr -> access_flags == prot && curr -> vm_start == addr + length){
                curr -> vm_start = curr -> vm_start - length;
                return curr -> vm_start;
            }

            struct vm_area* new_vm = os_alloc(sizeof(struct vm_area));
            new_vm -> vm_start = addr;
            new_vm -> vm_end = addr + length;
            new_vm -> access_flags = prot;
            prev -> vm_next = new_vm;
            new_vm -> vm_next = curr;
            stats -> num_vm_area ++;
            return addr;
        }
    }
    if(curr == NULL){    //empty space found at the end

        if(prev -> access_flags == prot && prev -> vm_end ==addr){
            unsigned long old_start = prev -> vm_end;
            prev -> vm_end = prev -> vm_end + length;
            return old_start;
        }

        struct vm_area* new_vm = os_alloc(sizeof(struct vm_area));
        new_vm -> vm_start = addr;
        new_vm -> vm_end = addr + length;
        new_vm -> access_flags = prot;
        prev -> vm_next = new_vm;
        new_vm -> vm_next = NULL;
        stats -> num_vm_area ++;
        return addr;

    } 

    return -EINVAL;
}

/**
 * munmap system call implemenations
 */

long vm_area_unmap(struct exec_context *current, u64 addr, int length)
{
    if(!current) return -EINVAL;
    if(length <= 0) return -EINVAL;

    if(length % _4KB != 0){
        length = length / _4KB * _4KB + _4KB;
    }
    
    if(current -> vm_area == NULL) return 0;

    struct vm_area* curr = current -> vm_area -> vm_next;
    struct vm_area* prev = NULL;

    struct vm_area* left = NULL;
    struct vm_area* right = NULL;

    while(curr != NULL){
        if(addr == MMAP_AREA_START + _4KB){
            left = current -> vm_area;
            break;
        }
        prev = curr;
        curr = curr -> vm_next;
        
        if(prev -> vm_end > addr){
            left = prev;
            break;
        }
        if(curr != NULL && curr -> vm_end > addr){
            if(curr ->vm_start < addr) left = curr;
            else left = prev;
            break;
        }
        
    }
    if(left == NULL) return 0;

    curr = current -> vm_area -> vm_next;
    prev = NULL;

    while(curr != NULL){
        prev = curr;
        curr = curr -> vm_next;
        
        if(prev -> vm_end > addr + length){
            right = prev;
            break;
        }
        if(curr != NULL && curr -> vm_end > addr + length){
            right = curr;
            break;
        }
        
    }

    if(left == right){
        struct vm_area* new_vm = os_alloc(sizeof(struct vm_area));
        new_vm -> vm_start = addr + length;
        new_vm -> vm_end = right -> vm_end;
        left -> vm_end = addr;
        new_vm -> access_flags = left -> access_flags;
        new_vm -> vm_next = right -> vm_next;
        left -> vm_next = new_vm;

        //FREEING THE PFNS
        int pages = (new_vm -> vm_start - left -> vm_end) / _4KB;
        for(int i = 0 ; i < pages ; i++){
            free_page(current, left -> vm_end + i * _4KB);
        }

        stats -> num_vm_area ++;
        return 0;
    }

    u64 vm_count = 0;
    struct vm_area* temp = left -> vm_next;
    
    while(temp != right){
        struct vm_area* del = temp;
        //FREEING THE PFNS
        int pages = (temp -> vm_end - temp -> vm_start) / _4KB;
        for(int i = 0 ; i < pages ; i++){
            free_page(current, temp -> vm_start + i * _4KB);
        }
        
        temp = temp -> vm_next;
        os_free(del,sizeof(struct vm_area));
        vm_count++ ;
    }
    if(right == NULL){
        if(left -> vm_end <= addr){
            left -> vm_next = NULL;
            stats -> num_vm_area = stats -> num_vm_area - vm_count;
            return 0;
        }
        if(left -> vm_end > addr){

            //FREEING THE PFNS
            int pages = (left -> vm_end - addr) / _4KB;
            for(int i = 0 ; i < pages ; i++){
                free_page(current, addr + i * _4KB);
            }

            left -> vm_end = addr;
            left -> vm_next = NULL;
            stats -> num_vm_area = stats -> num_vm_area - vm_count;
            return 0;
        }
    }
    if(left -> vm_end <= addr && right -> vm_start >= addr + length){
        left -> vm_next = right;
        stats -> num_vm_area = stats -> num_vm_area - vm_count;
        return 0;
    }
    if(left -> vm_end > addr && right -> vm_start >= addr + length){

        //FREEING THE PFNS
        int pages = (left -> vm_end - addr) / _4KB;
        for(int i = 0 ; i < pages ; i++){
            free_page(current, addr + i * _4KB);
        }

        left -> vm_end = addr;
        left -> vm_next = right;
        stats -> num_vm_area = stats -> num_vm_area - vm_count;
        return 0;
    }
    if(left -> vm_end <= addr && right -> vm_start < addr + length){

        //FREEING THE PFNS
        int pages = (addr + length - right -> vm_start) / _4KB;
        for(int i = 0 ; i < pages ; i++){
            free_page(current, right -> vm_start + i * _4KB);
        }

        right -> vm_start = addr + length;
        left -> vm_next = right;
        stats -> num_vm_area = stats -> num_vm_area - vm_count;
        return 0;
    }
    if(left -> vm_end > addr && right -> vm_start < addr + length){

        int pages = (addr + length - right -> vm_start) / _4KB;
        for(int i = 0 ; i < pages ; i++){
            free_page(current, right -> vm_start + i * _4KB);
        }

        //FREEING THE PFNS
        pages = (left -> vm_end - addr) / _4KB;
        for(int i = 0 ; i < pages ; i++){
            free_page(current, addr + i * _4KB);
        }

        left -> vm_end = addr;
        right -> vm_start = addr + length;
        left -> vm_next = right;
        stats -> num_vm_area = stats -> num_vm_area - vm_count;
        return 0;
    }
    

    return -EINVAL;
}

/**
 * Function will invoked whenever there is page fault for an address in the vm area region
 * created using mmap
 */

long vm_area_pagefault(struct exec_context *current, u64 addr, int error_code)
{
    if(current == NULL) return -EINVAL;
    if((addr < MMAP_AREA_START || addr > MMAP_AREA_END) && addr != 0) return -EINVAL;
    if(error_code != 0x4 && error_code != 0x6 && error_code !=0x7) return -EINVAL;
    if(current -> vm_area == NULL) return -EINVAL;
    struct vm_area* vm_area_head = current -> vm_area -> vm_next;
    u32 check = 0;
    while(vm_area_head != NULL){
        if(vm_area_head -> vm_start <= addr && vm_area_head -> vm_end > addr){
            check = 1;
            break;
        }
        vm_area_head = vm_area_head -> vm_next;
    } 
    if(check == 0) return -EINVAL;
    if(check == 1){
        if(error_code == 0x6 && vm_area_head -> access_flags == PROT_READ){
            return -EINVAL;
        }
        if(error_code == 0x7){
            if(vm_area_head -> access_flags == PROT_READ){
                return -EINVAL;
            }
            else{
                handle_cow_fault(current, addr, vm_area_head -> access_flags);
                return 1;
            }
        }
    }

    u64* virtual_pgd = (u64*)osmap(current -> pgd);

    u64 pgd_offset = (addr >> 39) & 0x0000001FF;
    u64 pud_offset = (addr >> 30) & 0x0000001FF;
    u64 pmd_offset = (addr >> 21) & 0x0000001FF;
    u64 pte_offset = (addr >> 12) & 0x0000001FF;
    u64 pfn_offset = (addr) & 0x000000FFF;

    u64 pud_entry;
    u64 pmd_entry;
    u64 pte_entry;
    u64* virtual_pud_t;


    u64* virtual_pgd_t = virtual_pgd + pgd_offset;
    u64 pgd_entry = *(virtual_pgd_t);
    if(pgd_entry & 1 == 1){
        u64* virtual_pud = (u64*)osmap(pgd_entry >> 12);
        virtual_pud_t = virtual_pud + pud_offset;
        pud_entry = *(virtual_pud_t); 
    }
    else{
        u32 pud = os_pfn_alloc(OS_PT_REG);
        *(virtual_pgd_t) = pud << 12;
        *(virtual_pgd_t) = *(virtual_pgd_t) | 0x19;
        pgd_entry = *(virtual_pgd_t);
        asm volatile("invlpg (%0)"::"r" (addr));
        u64* virtual_pud = osmap(pud);
        virtual_pud_t = virtual_pud + pud_offset;
        pud_entry = *(virtual_pud_t); 
    }

    u64* virtual_pmd_t;

    if(pud_entry & 1 == 1){
        u64* virtual_pmd = (u64*)osmap(pud_entry >> 12);
        virtual_pmd_t = virtual_pmd + pmd_offset;
        pmd_entry = *(virtual_pmd_t); 
    }
    else{
        u32 pmd = os_pfn_alloc(OS_PT_REG);
        *(virtual_pud_t) = pmd << 12;
        *(virtual_pud_t) = *(virtual_pud_t) | 0x19;
        pud_entry = *(virtual_pud_t);
        asm volatile("invlpg (%0)"::"r" (addr));
        u64* virtual_pmd = osmap(pmd);
        virtual_pmd_t = virtual_pmd + pmd_offset;
        pmd_entry = *(virtual_pmd_t); 
    }

    u64* virtual_pte_t;

    if(pmd_entry & 1 == 1){
        u64* virtual_pte = (u64*)osmap(pmd_entry >> 12);
        virtual_pte_t = virtual_pte + pte_offset;
        pte_entry = *(virtual_pte_t); 
    }
    else{
        u32 pte = os_pfn_alloc(OS_PT_REG);
        *(virtual_pmd_t) = pte << 12;
        *(virtual_pmd_t) = *(virtual_pmd_t) | 0x19;
        pmd_entry = *(virtual_pmd_t);
        asm volatile("invlpg (%0)"::"r" (addr));
        u64* virtual_pte = osmap(pte);
        virtual_pte_t = virtual_pte + pte_offset;
        pte_entry = *(virtual_pte_t); 
    }

    if(pte_entry & 1 == 1) return 1;
    else{
        u32 pfn = os_pfn_alloc(USER_REG);
        *(virtual_pte_t) = pfn << 12;
        *(virtual_pte_t) = *(virtual_pte_t) | 0x11 | ((error_code & 2) << 2);
        pte_entry = *(virtual_pte_t);
        asm volatile("invlpg (%0)"::"r" (addr));
        u64 virtual_pfn = osmap(pfn);
        return 1;

    }
}

/**
 * cfork system call implemenations
 * The parent returns the pid of child process. The return path of
 * the child process is handled separately through the calls at the 
 * end of this function (e.g., setup_child_context etc.)
 */

long do_cfork(){
    u32 pid;
    struct exec_context *new_ctx = get_new_ctx();
    struct exec_context *ctx = get_current_ctx();
     /* Do not modify above lines
     * 
     * */   
     /*--------------------- Your code [start]---------------*/
     


    //COPYING THE CONTENTS
    new_ctx -> ppid = ctx -> pid;
    new_ctx -> type = ctx -> type;
    new_ctx -> state = ctx -> state;
    new_ctx -> used_mem = ctx -> used_mem;
    new_ctx -> os_stack_pfn = ctx -> os_stack_pfn;
    new_ctx -> os_rsp = ctx -> os_rsp;
    new_ctx -> regs = ctx -> regs;
    new_ctx -> pending_signal_bitmap = ctx -> pending_signal_bitmap;
    new_ctx -> ticks_to_sleep = ctx -> ticks_to_sleep;
    new_ctx -> alarm_config_time = ctx -> alarm_config_time;
    new_ctx -> ticks_to_alarm = ctx -> ticks_to_alarm;
    new_ctx -> ctx_threads = ctx -> ctx_threads;


    for(int i = 0; i < MAX_MM_SEGS; i++){
        new_ctx->mms[i] = ctx->mms[i];
    }

    for(int i = 0; i < CNAME_MAX; i++){
        new_ctx->name[i] = ctx->name[i];
    }

    for(int i = 0; i < MAX_SIGNALS; i++){
        new_ctx->sighandlers[i] = ctx->sighandlers[i];
    }

    for(int i = 0; i < MAX_OPEN_FILES; i++){
        new_ctx->files[i] = ctx->files[i];
    }

    pid = new_ctx -> pid;

    struct vm_area* vm_area_parent = ctx -> vm_area;
    struct vm_area* vm_area_child = NULL;
    new_ctx -> vm_area = NULL;

    // printk("parent vma = %x\n", ctx -> vm_area);
    // printk("child vma = %x\n", new_ctx -> vm_area);

    if(vm_area_parent != NULL){

        //DUMMY CREATION
        struct vm_area* vm_area_child_dummy = os_alloc(sizeof(struct vm_area));
        vm_area_child_dummy -> vm_start = vm_area_parent -> vm_start;
        vm_area_child_dummy -> vm_end = vm_area_parent -> vm_end;
        vm_area_child_dummy -> access_flags = vm_area_parent -> access_flags;
        vm_area_child_dummy -> vm_next = NULL;
        vm_area_child = vm_area_child_dummy; 

        new_ctx -> vm_area = vm_area_child;

        vm_area_parent = vm_area_parent -> vm_next;
        //COPYING THE VMAs
        while(vm_area_parent != NULL){
            struct vm_area* vm_area_new_child = os_alloc(sizeof(struct vm_area));
            vm_area_new_child -> vm_start = vm_area_parent -> vm_start;
            vm_area_new_child -> vm_end = vm_area_parent -> vm_end;
            vm_area_new_child -> access_flags = vm_area_parent -> access_flags;
            vm_area_child -> vm_next = vm_area_new_child;

            vm_area_child = vm_area_child -> vm_next;
            vm_area_parent = vm_area_parent -> vm_next;
        }

        vm_area_child -> vm_next = NULL;
    }

    // printk("parent vma = %x\n", ctx -> vm_area -> vm_next);
    // printk("child vma = %x\n", new_ctx -> vm_area -> vm_next);

    new_ctx -> pgd = os_pfn_alloc(OS_PT_REG);
    if(new_ctx -> pgd == 0) return -1;

    vm_area_parent = ctx -> vm_area;

    while(vm_area_parent != NULL){
        int pages = (vm_area_parent -> vm_end - vm_area_parent -> vm_start) / _4KB;
        for(int i = 0 ; i < pages ; i++){
            copy_pte_to_child(ctx, new_ctx, (vm_area_parent -> vm_start) + (i*_4KB));
        }

        vm_area_parent = vm_area_parent -> vm_next;
    }

    for(u64 i = 0; i < 3; i++){
        u64 pages = (ctx -> mms[i].next_free - ctx -> mms[i].start) / _4KB;
        for(u64 j = 0 ; j < pages ; j++){
            copy_pte_to_child(ctx, new_ctx, (ctx -> mms[i].start) + (j * _4KB));
        }
    }
    
            
    u64 pages = (ctx -> mms[3].end - ctx -> mms[3].start) / _4KB;
    for(u64 i = 0 ; i < pages ; i++){
        copy_pte_to_child(ctx, new_ctx, (ctx -> mms[3].start) + (i * _4KB));
    }

     /*--------------------- Your code [end] ----------------*/
    
     /*
     * The remaining part must not be changed
     */
    copy_os_pts(ctx->pgd, new_ctx->pgd);
    do_file_fork(new_ctx);
    setup_child_context(new_ctx);
    return pid;
}

/* Cow fault handling, for the entire user address space
 * For address belonging to memory segments (i.e., stack, data) 
 * it is called when there is a CoW violation in these areas. 
 *
 * For vm areas, your fault handler 'vm_area_pagefault'
 * should invoke this function
 * */

long handle_cow_fault(struct exec_context *current, u64 vaddr, int access_flags)
{

    //PAGE TABLE WALK
    u64* virtual_pgd = (u64*)osmap(current -> pgd);

    u64 pgd_offset = (vaddr >> 39) & 0x0000001FF;
    u64 pud_offset = (vaddr >> 30) & 0x0000001FF;
    u64 pmd_offset = (vaddr >> 21) & 0x0000001FF;
    u64 pte_offset = (vaddr >> 12) & 0x0000001FF;
    u64 pfn_offset = (vaddr) & 0x000000FFF;

    u64 pud_entry;
    u64 pmd_entry;
    u64 pte_entry;
    u64* virtual_pud_t;


    u64* virtual_pgd_t = virtual_pgd + pgd_offset;
    u64 pgd_entry = *(virtual_pgd_t);
    if(pgd_entry & 1 == 1){
        u64* virtual_pud = (u64*)osmap(pgd_entry >> 12);
        virtual_pud_t = virtual_pud + pud_offset;
        pud_entry = *(virtual_pud_t); 
    }
    else{
        return -1; 
    }

    u64* virtual_pmd_t;

    if(pud_entry & 1 == 1){
        u64* virtual_pmd = (u64*)osmap(pud_entry >> 12);
        virtual_pmd_t = virtual_pmd + pmd_offset;
        pmd_entry = *(virtual_pmd_t); 
    }
    else{
        return -1; 
    }

    u64* virtual_pte_t;

    if(pmd_entry & 1 == 1){
        u64* virtual_pte = (u64*)osmap(pmd_entry >> 12);
        virtual_pte_t = virtual_pte + pte_offset;
        pte_entry = *(virtual_pte_t); 
    }
    else{
        return -1;
    }

    if(pte_entry & 1 == 1){
            //IF MORE THAN PROCESS REFER TO THE SAME PFN
            if(get_pfn_refcount(pte_entry >> 12) > 1){
            u32 new_pfn = os_pfn_alloc(USER_REG);
            put_pfn(pte_entry >> 12);
            memcpy((char*)osmap(new_pfn), (char*)osmap(pte_entry >> 12), _4KB);
            *(virtual_pte_t) = (new_pfn << 12) | (*(virtual_pte_t) & 0xFFF);

            if(access_flags == (PROT_READ | PROT_WRITE)){
                *(virtual_pte_t) = ((*(virtual_pte_t)) & ~0xF) | 0x19;
            }
            else{
                *(virtual_pte_t) = ((*(virtual_pte_t)) & ~0xF) | 0x11;
            }
            asm volatile("invlpg (%0)"::"r" (vaddr));

        }
        else{
            if(access_flags == (PROT_READ | PROT_WRITE)){
                *(virtual_pte_t) = ((*(virtual_pte_t)) & ~0xF) | 0x19;
            }
            else{
                *(virtual_pte_t) = ((*(virtual_pte_t)) & ~0xF) | 0x11;
            }
            asm volatile("invlpg (%0)"::"r" (vaddr));
        }
        return 1;
    }
    else{
        
        return -1;

    }
}
