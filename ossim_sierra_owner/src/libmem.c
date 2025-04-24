/*
 * Copyright (C) 2025 pdnguyen of HCMC University of Technology VNU-HCM
 */

/* Sierra release
 * Source Code License Grant: The authors hereby grant to Licensee
 * personal permission to use and modify the Licensed Source Code
 * for the sole purpose of studying while attending the course CO2018.
 */

// #ifdef MM_PAGING
/*
 * System Library
 * Memory Module Library libmem.c 
 */

 #include "string.h"
 #include "mm.h"
 #include "syscall.h"
 #include "libmem.h"
 #include <stdlib.h>
 #include <stdio.h>
 #include <pthread.h>
 
 static pthread_mutex_t mmvm_lock = PTHREAD_MUTEX_INITIALIZER;
 
 /*enlist_vm_freerg_list - add new rg to freerg_list
  *@mm: memory region
  *@rg_elmt: new region
  *
  */
 int enlist_vm_freerg_list(struct mm_struct *mm, struct vm_rg_struct *rg_elmt) // thêm rg mới vào free list
 {
   if (!mm || !mm->mmap || !rg_elmt)
     return -1;
   struct vm_rg_struct *rg_node = mm->mmap->vm_freerg_list;
 
   if (rg_elmt->rg_start >= rg_elmt->rg_end)
     return -1;
 
   if (rg_node != NULL)
     rg_elmt->rg_next = rg_node;
 
   /* Enlist the new region */
   mm->mmap->vm_freerg_list = rg_elmt;
 
   return 0;
 }
 
 /*get_symrg_byid - get mem region by region ID
  *@mm: memory region
  *@rgid: region ID act as symbol index of variable
  *
  */
 struct vm_rg_struct *get_symrg_byid(struct mm_struct *mm, int rgid) //lấy rg thông qua id và bảng kí hiệu
 {
   if (rgid < 0 || rgid > PAGING_MAX_SYMTBL_SZ)
     return NULL;
 
   return &mm->symrgtbl[rgid];
 }
 
 /*__alloc - allocate a region memory
  *@caller: caller
  *@vmaid: ID vm area to alloc memory region
  *@rgid: memory region ID (used to identify variable in symbole table)
  *@size: allocated size
  *@alloc_addr: address of allocated memory region
  *
  */
 int __alloc(struct pcb_t *caller, int vmaid, int rgid, int size, int *alloc_addr)
 {
   pthread_mutex_lock(&mmvm_lock);  //Lock trước khi truy cập vùng nhớ
 
   struct vm_area_struct *cur_vma = get_vma_by_num(caller->mm, vmaid);
   if (!cur_vma) {
     pthread_mutex_unlock(&mmvm_lock);
     return -1;
   }
 
   struct vm_rg_struct rgnode;
 
   // Bước 1: Thử tìm vùng nhớ trống đã có sẵn
   if (get_free_vmrg_area(caller, vmaid, size, &rgnode) == 0) {
     caller->mm->symrgtbl[rgid] = rgnode;
     *alloc_addr = rgnode.rg_start;
 
     pthread_mutex_unlock(&mmvm_lock);  //Unlock trước khi trả về
     return 0;
   }
 
   // Bước 2: Nếu không có sẵn → tăng sbrk, ánh xạ trang mới vào RAM
   int aligned_size = PAGING_PAGE_ALIGNSZ(size);
   int old_sbrk = cur_vma->sbrk;
 
   struct sc_regs regs = {
     .a1 = SYSMEM_INC_OP,
     .a2 = cur_vma->vm_id,
     .a3 = aligned_size
   };
 
  //  if (__sys_memmap(caller, &regs) < 0) {
  //    pthread_mutex_unlock(&mmvm_lock);
  //    return -1;
  //  }
 
   cur_vma->sbrk += aligned_size;
   cur_vma->vm_end = cur_vma->sbrk;
 
   if (vm_map_ram(caller, old_sbrk, old_sbrk + aligned_size,
                  old_sbrk, aligned_size / PAGING_PAGESZ, &rgnode) < 0) {
     pthread_mutex_unlock(&mmvm_lock);
     return -1;
   }
 
   rgnode.rg_start = old_sbrk;
   rgnode.rg_end = old_sbrk + size;
   caller->mm->symrgtbl[rgid] = rgnode;
   *alloc_addr = rgnode.rg_start;
 
   if (aligned_size > size) {
     struct vm_rg_struct *remain = malloc(sizeof(struct vm_rg_struct));
     remain->rg_start = old_sbrk + size;
     remain->rg_end = old_sbrk + aligned_size;
     remain->rg_next = cur_vma->vm_freerg_list;
     cur_vma->vm_freerg_list = remain;
   }
 
   pthread_mutex_unlock(&mmvm_lock);  //Unlock sau khi hoàn thành
 
   return 0;
 }
 
 
 
 /*__free - remove a region memory
  *@caller: caller
  *@vmaid: ID vm area to alloc memory region
  *@rgid: memory region ID (used to identify variable in symbole table)
  *@size: allocated size
  *
  */
 int __free(struct pcb_t *caller, int vmaid, int rgid)
 {
   pthread_mutex_lock(&mmvm_lock);  // Lock trước khi thao tác bộ nhớ
 
   if (rgid < 0 || rgid > PAGING_MAX_SYMTBL_SZ) {
     pthread_mutex_unlock(&mmvm_lock);
     return -1;
   }
 
   struct vm_rg_struct *rgnode = get_symrg_byid(caller->mm, rgid);
   if (!rgnode) {
     pthread_mutex_unlock(&mmvm_lock);
     return -1;
   }
 
   struct vm_rg_struct *freed_region = malloc(sizeof(struct vm_rg_struct));
   if (!freed_region) {
     pthread_mutex_unlock(&mmvm_lock);
     return -1;
   }
 
   freed_region->rg_start = rgnode->rg_start;
   freed_region->rg_end = rgnode->rg_end;
   freed_region->rg_next = NULL;
 
   rgnode->rg_start = 0;
   rgnode->rg_end = 0;
   // Reset lại entry trong bảng ký hiệu
  caller->mm->symrgtbl[rgid].rg_start = 0;
  caller->mm->symrgtbl[rgid].rg_end = 0;
  caller->mm->symrgtbl[rgid].rg_next = NULL;

 
   struct vm_area_struct *cur_vma = get_vma_by_num(caller->mm, vmaid);
   if (!cur_vma) {
     free(freed_region);
     pthread_mutex_unlock(&mmvm_lock);
     return -1;
   }
 
   enlist_vm_freerg_list(cur_vma, freed_region);
 
   pthread_mutex_unlock(&mmvm_lock);  // Unlock sau khi hoàn thành
 
   return 0;
 }
 
 
 /*liballoc - PAGING-based allocate a region memory
  *@proc:  Process executing the instruction
  *@size: allocated size
  *@reg_index: memory region ID (used to identify variable in symbole table)
  */
 int liballoc(struct pcb_t *proc, uint32_t size, uint32_t reg_index)
 {
   if (!proc || size == 0 || reg_index >= PAGING_MAX_SYMTBL_SZ) 
     {
         return -1; // Trả về lỗi nếu tham số không hợp lệ
     }
 
     int addr;
     int ret = __alloc(proc, 0, reg_index, size, &addr); // Dùng VMAID mặc định là 0
 
     if (ret == 0) {
       #ifdef IODUMP
         printf("===== PHYSICAL MEMORY AFTER ALLOCATION =====\n");
         printf("PID=%d - Region=%d - Address=%08x - Size=%d byte\n", proc->pid, reg_index, addr, size);
       #ifdef PAGETBL_DUMP
         print_pgtbl(proc, 0, -1);
       #endif
         for (int pgn = 0; pgn < PAGING_MAX_PGN; pgn++) {
             uint32_t pte = proc->mm->pgd[pgn];
             if (PAGING_PAGE_PRESENT(pte)) {
                 int fpn = PAGING_FPN(pte);
                 printf("Page Number: %d -> Frame Number: %d\n", pgn, fpn);
             }
         }
         printf("================================================================\n");
       #endif
         return addr;
       }      
 }
 
 /*libfree - PAGING-based free a region memory
  *@proc: Process executing the instruction
  *@size: allocated size
  *@reg_index: memory region ID (used to identify variable in symbole table)
  */
 
  int libfree(struct pcb_t *proc, uint32_t reg_index)
  {
    if (!proc || reg_index >= PAGING_MAX_SYMTBL_SZ) 
    {
      return -1; // Kiểm tra hợp lệ
    }
  
    /* By default using vmaid = 0 */
    int ret = __free(proc, 0, reg_index);
  
    if (ret == 0) {
  #ifdef IODUMP
      printf("===== PHYSICAL MEMORY AFTER DEALLOCATION =====\n");
      printf("PID=%d - Region=%d\n", proc->pid, reg_index);
  #ifdef PAGETBL_DUMP
      print_pgtbl(proc, 0, -1); // In toàn bộ page table
  #endif
     for (int pgn = 0; pgn < PAGING_MAX_PGN; pgn++) {
       uint32_t pte = proc->mm->pgd[pgn];
       if (PAGING_PAGE_PRESENT(pte)) {
           int fpn = PAGING_FPN(pte);
           printf("Page Number: %d -> Frame Number: %d\n", pgn, fpn);
       }
     }
      printf("================================================================\n");
  #endif
    }
  
    return ret;
  }
  
 
 /*pg_getpage - get the page in ram
  *@mm: memory region
  *@pagenum: PGN
  *@framenum: return FPN
  *@caller: caller
  *
  */
 int pg_getpage(struct mm_struct *mm, int pgn, int *fpn, struct pcb_t *caller) //Hàm này có nhiệm vụ đảm bảo rằng một trang ảo (page), 
 //được đánh số bởi pgn (Page Number), đang hiện diện trong bộ nhớ vật lý (RAM). 
 //Nếu nó đang bị hoán đổi ra đĩa (swap), hàm sẽ đưa nó quay lại RAM, và cập nhật lại thông tin trong Page Table (bảng trang – tức pgd).
 {
   uint32_t pte = mm->pgd[pgn];
 
   if (!PAGING_PAGE_PRESENT(pte))  //xem trang đã có mặt trong RAM hay bị swap ra ngoài, nếu bị swap ra thì hàm cần thực hiện tiếp
   { /* Page is not online, make it actively living */
     int vicpgn, swpfpn;   
     //int vicfpn;
     //uint32_t vicpte;
 
     int tgtfpn = PAGING_PTE_SWP(pte);//the target frame storing our variable 
 
     /* TODO: Play with your paging theory here */
     /* Find victim page */
     find_victim_page(caller->mm, &vicpgn);  //Tìm trang nạn nhân trong RAM cần được thay thế
 
     /* Get free frame in MEMSWP */
     MEMPHY_get_freefp(caller->active_mswp, &swpfpn);  //Cấp phát 1 frame trống trong vùng nhớ phụ để chứa nội dung trang bị thay thế
 
     /* TODO: Implement swap frame from MEMRAM to MEMSWP and vice versa*/
 
     /* TODO copy victim frame to swap 
      * SWP(vicfpn <--> swpfpn)
      * SYSCALL 17 sys_memmap 
      * with operation SYSMEM_SWP_OP
      */
     struct sc_regs regs;  //Swap nạn nhân ra đĩa dùng syscall
     regs.a1 = SYSMEM_SWP_OP;
     regs.a2 = vicpgn;    // RAM frame bị đẩy đi
     regs.a3 = swpfpn;    // Đẩy vào đâu trong SWAP
     __sys_memmap(caller, &regs);
 
     /* SYSCALL 17 sys_memmap */
 
     /* TODO copy target frame form swap to mem 
      * SWP(tgtfpn <--> vicfpn)
      * SYSCALL 17 sys_memmap
      * with operation SYSMEM_SWP_OP
      */
     /* TODO copy target frame form swap to mem 
     //regs.a1 =...
     //regs.a2 =...
     //regs.a3 =..
     */
     regs.a1 = SYSMEM_SWP_OP;  //Swap trang cần dùng từ swap về RAM
     regs.a2 = tgtfpn;    // Trang cần lấy (trong SWAP)
     regs.a3 = vicpgn;    // Đẩy vào đây (RAM frame trống)
     __sys_memmap(caller, &regs);
     /* SYSCALL 17 sys_memmap */
 
     /* Update page table */
     //pte_set_swap() 
     //mm->pgd;
 
     /* Update its online status of the target page */
     //pte_set_fpn() &
     //mm->pgd[pgn];
     //pte_set_fpn();
     pte = 0;                      //Cập nhật page table
     pte_set_fpn(&pte, vicpgn);
     PAGING_PTE_SET_PRESENT(pte);
     mm->pgd[pgn] = pte;
 
     enlist_pgn_node(&caller->mm->fifo_pgn,pgn); //Ghi nhớ trang này vừa được đưa vào RAM, để hỗ trợ quản lý trang theo FIFO khi cần tìm victim tiếp theo.
   }
 
   *fpn = PAGING_FPN(mm->pgd[pgn]); //Trả về frame num của trang
 
   return 0;
 }
 
 /*pg_getval - read value at given offset
  *@mm: memory region
  *@addr: virtual address to acess
  *@value: value
  *
  */
 int pg_getval(struct mm_struct *mm, int addr, BYTE *data, struct pcb_t *caller)//Xác định trang ảo chứa địa chỉ addr.
 
 //Nếu trang không có trong RAM, sẽ dùng pg_getpage() để load từ swap vào RAM.
 
 //Sau đó, tính địa chỉ vật lý, và dùng hệ thống syscall để đọc byte từ RAM vào data.
 {
   int pgn = PAGING_PGN(addr); //Lấy page number từ địa chỉ ảo
   //int off = PAGING_OFFST(addr);
   int fpn;
 
   /* Get the page to MEMRAM, swap from MEMSWAP if needed */
   if (pg_getpage(mm, pgn, &fpn, caller) != 0) //Đảm bảo trang đã nằm trong RAM. Nếu không, gọi hệ thống phân trang để swap in.
                                               //ghi dữ liệu frame num của trang vào fpn (có dấu tham chiếu &)
     return -1; /* invalid page access */
 
   /* TODO 
    *  MEMPHY_read(caller->mram, phyaddr, data);
    *  MEMPHY READ 
    *  SYSCALL 17 sys_memmap with SYSMEM_IO_READ
    */
   int phyaddr = fpn * PAGING_PAGESZ + PAGING_OFFST(addr);  //Tính địa chỉ vật lý thật sự trong RAM
   struct sc_regs regs;                                        //syscall đọc dữ liệu từ RAM
   regs.a1 = SYSMEM_IO_READ;
   regs.a2 = phyaddr;
   regs.a3 = (uint32_t)data;
 
   __sys_memmap(caller, &regs);
   /* SYSCALL 17 sys_memmap */
 
   // Update data
   // data = (BYTE)
 
   return 0;
 }
 
 /*pg_setval - write value to given offset
  *@mm: memory region
  *@addr: virtual address to acess
  *@value: value
  *
  */
 int pg_setval(struct mm_struct *mm, int addr, BYTE value, struct pcb_t *caller) //Xác định trang ảo chứa địa chỉ addr.
 
 //Đảm bảo trang đó đang ở RAM (nếu không, gọi pg_getpage để load vào).
 
 //Tính địa chỉ vật lý.
 
 //Gọi SYSCALL 17 để ghi byte vào RAM tại địa chỉ vật lý đó.
 {
   int pgn = PAGING_PGN(addr); // lấy số trang từ địa chỉ ảo.
   //int off = PAGING_OFFST(addr);
   int fpn; //frame vật lý tương ứng với page.
 
   /* Get the page to MEMRAM, swap from MEMSWAP if needed */
   if (pg_getpage(mm, pgn, &fpn, caller) != 0) //Gọi pg_getpage để đảm bảo page đã có mặt ở RAM.
     return -1; /* invalid page access */
 
   /* TODO
    *  MEMPHY_write(caller->mram, phyaddr, value);
    *  MEMPHY WRITE
    *  SYSCALL 17 sys_memmap with SYSMEM_IO_WRITE
    */
   int phyaddr = fpn * PAGING_PAGESZ + PAGING_OFFST(addr);// Tính địa chỉ vật lý
 
   struct sc_regs regs; // Gọi syscall để ghi giá trị vào RAM
   regs.a1 = SYSMEM_IO_WRITE;
   regs.a2 = phyaddr;
   regs.a3 = (uint32_t)&value;
 
   __sys_memmap(caller, &regs);
   /* SYSCALL 17 sys_memmap */
 
   // Update data
   // data = (BYTE) 
 
   return 0;
 }
 
 /*__read - read value in region memory
  *@caller: caller
  *@vmaid: ID vm area to alloc memory region
  *@offset: offset to acess in memory region
  *@rgid: memory region ID (used to identify variable in symbole table)
  *@size: allocated size
  *
  */
 int __read(struct pcb_t *caller, int vmaid, int rgid, int offset, BYTE *data)
 {
   struct vm_rg_struct *currg = get_symrg_byid(caller->mm, rgid);
   struct vm_area_struct *cur_vma = get_vma_by_num(caller->mm, vmaid);
 
   if (currg == NULL || cur_vma == NULL) /* Invalid memory identify */
     return -1;
 
   pg_getval(caller->mm, currg->rg_start + offset, data, caller);
 
   return 0;
 }
 
 /*libread - PAGING-based read a region memory */
 int libread(
     struct pcb_t *proc, // Process executing the instruction
     uint32_t source,    // Index of source register
     uint32_t offset,    // Source address = [source] + [offset]
     uint32_t* destination)
 {
  BYTE data;
  int val = __read(proc, 0, source, offset, &data); // Gọi __read để lấy dữ liệu từ [source] + offset, kết quả lưu vào data.

  if (val != 0) { // Nếu có lỗi
      printf("libread: Failed to read from region=%d, offset=%d\n", source, offset);
      return val;
  }

  *destination = (uint32_t)data; // Cập nhật giá trị đọc được, chuyển 1 byte sang 4 byte

#ifdef IODUMP
  printf("read region=%d offset=%d value=%d\n", source, offset, data);
#ifdef PAGETBL_DUMP
  print_pgtbl(proc, 0, -1); // print max TBL
#endif
  MEMPHY_dump(proc->mram);
#endif

  return val;
 }
 
 /*__write - write a region memory
  *@caller: caller
  *@vmaid: ID vm area to alloc memory region
  *@offset: offset to acess in memory region
  *@rgid: memory region ID (used to identify variable in symbole table)
  *@size: allocated size
  *
  */
 int __write(struct pcb_t *caller, int vmaid, int rgid, int offset, BYTE value)
 {
   struct vm_rg_struct *currg = get_symrg_byid(caller->mm, rgid);
   struct vm_area_struct *cur_vma = get_vma_by_num(caller->mm, vmaid);
 
   if (currg == NULL || cur_vma == NULL) /* Invalid memory identify */
     return -1;
 
   pg_setval(caller->mm, currg->rg_start + offset, value, caller);
 
   return 0;
 }
 
 /*libwrite - PAGING-based write a region memory */
 int libwrite(
     struct pcb_t *proc,   // Process executing the instruction
     BYTE data,            // Data to be wrttien into memory
     uint32_t destination, // Index of destination register
     uint32_t offset)
 {
 #ifdef IODUMP
   printf("write region=%d offset=%d value=%d\n", destination, offset, data);
 #ifdef PAGETBL_DUMP
   print_pgtbl(proc, 0, -1); //print max TBL
 #endif
   MEMPHY_dump(proc->mram);
 #endif
 
   return __write(proc, 0, destination, offset, data);
 }
 
 /*free_pcb_memphy - collect all memphy of pcb
  *@caller: caller
  *@vmaid: ID vm area to alloc memory region
  *@incpgnum: number of page
  */
 int free_pcb_memph(struct pcb_t *caller) 
 {
   int pagenum, fpn;
   uint32_t pte;
 
 
   for(pagenum = 0; pagenum < PAGING_MAX_PGN; pagenum++)
   {
     pte= caller->mm->pgd[pagenum];
 
     if (!PAGING_PAGE_PRESENT(pte))
     {
       fpn = PAGING_PTE_FPN(pte);
       MEMPHY_put_freefp(caller->mram, fpn);
     } else {
       fpn = PAGING_PTE_SWP(pte);
       MEMPHY_put_freefp(caller->active_mswp, fpn);    
     }
   }
 
   return 0;
 }
 
 
 /*find_victim_page - find victim page
  *@caller: caller
  *@pgn: return page number
  *
  */
 int find_victim_page(struct mm_struct *mm, int *retpgn)//Tìm một trang (page) đã được nạp vào bộ nhớ RAM lâu nhất, 
 //và trả về số hiệu trang đó qua *retpgn.
 {
   struct pgn_t *pg = mm->fifo_pgn;
 
   /* TODO: Implement the theorical mechanism to find the victim page */
   if (pg == NULL)
     return -1; // không còn trang nào để thay thế
 
   *retpgn = pg->pgn; // lấy số hiệu trang
 
   // Cập nhật danh sách FIFO: bỏ phần tử đầu
   mm->fifo_pgn = pg->pg_next;
   free(pg);
 
   return 0;
 }
 
 /*get_free_vmrg_area - get a free vm region
  *@caller: caller
  *@vmaid: ID vm area to alloc memory region
  *@size: allocated size
  *
  */
 int get_free_vmrg_area(struct pcb_t *caller, int vmaid, int size, struct vm_rg_struct *newrg) //Lấy rg đang k đc sử dụng
 {
   struct vm_area_struct *cur_vma = get_vma_by_num(caller->mm, vmaid);
   struct vm_rg_struct *rgit = cur_vma->vm_freerg_list;
 
   if (rgit == NULL)
     return -1;
 
   // Khởi tạo newrg chưa tìm thấy
   newrg->rg_start = newrg->rg_end = -1;
 
   // Duyệt qua danh sách vùng trống
   while (rgit != NULL)
   {
     int free_size = rgit->rg_end - rgit->rg_start + 1;
     if (free_size >= size)
     {
       newrg->rg_start = rgit->rg_start;
       newrg->rg_end = rgit->rg_start + size - 1;
 
       // Cập nhật lại vùng trống sau khi cắt bớt
       rgit->rg_start = newrg->rg_end + 1;
 
       return 0; // Thành công
     }
 
     rgit = rgit->rg_next;  //QUAN TRỌNG: chuyển sang vùng tiếp theo
   }
 
   return -1; // Không tìm thấy vùng phù hợp
 }
 
 //#endif
 