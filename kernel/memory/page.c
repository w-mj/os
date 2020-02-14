#include <memory.h>
#include <delog.h>
#include <list.h>
#include <base.h>
#include <list.h>
#include <asm.h>
#include <string.h>
#include "early_kmalloc.h"

static FrameEntry *mem_pool = NULL;  // 存放所有页框的数组
static int mem_frame_cnt = 0;  //  总页框数量

static ListEntry mem_free_head;  // 空闲页框列表头
static ListEntry mem_occupied_head;  // 已被占用页框列表头
static int mem_free_cnt = 0;
static int mem_occupied_cnt = 0;

u64 virtual_to_physics(u64 pml, u64 vir) {
	int level = 4;
	while (level >= 1) {
		u64 *pml_addr = (u64*)VIRTUAL(pml);
		u16 pmli = PMLOFFSET(vir, level);
		u64 pmle = pml_addr[pmli];
		if (!PRESENT(pmle)) {
			loge("page not present at level %d", level);
			die();
		}
		pml = PTENTRYADDR(pmle);
		level--;
	}
	return pml | (vir &0xfff);
}

extern u64 heap_end;
extern u64 pml4, pm_end;
u64 kernel_pml4; // 内核4级页表物理地址

// 分配一个空闲页框，返回页框号
static int frame_alloc_cnt = 0;
FrameEntry *frame_alloc() {
	if (mem_free_cnt == 0) {
		loge("No remain frame");
		return 0;
	}
	frame_alloc_cnt++;
	ListEntry *free_frame = mem_free_head.prev;
	FrameEntry *frame_entry = list_entry(free_frame, FrameEntry, next);
	frame_entry->count++;
	frame_entry->state = OCCUPIED;
	list_remove(free_frame);
	list_add(free_frame, &mem_occupied_head);
	mem_free_cnt--;
	mem_occupied_cnt++;
	return frame_entry;
}


void frame_release(FrameEntry *frame) {
	frame->count--;
	if (frame->count > 0)
		return;
	frame->state = AVAILABLE;
	list_remove(&frame->next);
	list_add(&frame->next, &mem_free_head);
	mem_free_cnt++;
	mem_occupied_cnt--;
}

void page_table_set_entry(u64 pmltop, u64 page_table_entry, u64 value, bool auto_alloc) {
	int level = 4;
	u64 *pml = (u64*)pmltop;
	while (level > 1) {
		pml = (u64*)VIRTUAL(pml);  // 物理页框号转换为虚拟地址
		u16 pmli = PMLOFFSET(page_table_entry, level);
		u64 pmle = pml[pmli];
		if (!PRESENT(pmle)) {
			if (!auto_alloc) {
				loge("page_table_set_entry pml%d[%d] is not present", level, pmli);
				return;
			} else {
				// 自动分配页框
				pmle = pml[pmli] = frame_alloc()->address + 7;
				// logd("auto alloc frame %llx %llx", VIRTUAL(pmle), pmle);
				page_table_set_entry(pmltop, heap_end, pmle, true);
				heap_end += PAGESIZE;
			}
		}
		pml = (u64*)PTENTRYADDR(pmle);
		level--;
	}
	pml = (u64*)VIRTUAL(pml);  // 物理页框号转换为虚拟地址
	u16 pml1i = PMLOFFSET(page_table_entry, 1);
	pml[pml1i] = value;
}
#define logadd(x) \
	logd("vir %llx psy %llx abs %llx", (x), virtual_to_physics(newmp, (x)), ABSOLUTE(x));

static void write_new_kernel_page_table() {
	u64 newmp = frame_alloc()->address;  // 新的四级页表地址（页框号是物理地址）
	_sL(newmp);
	memset((u64*)VIRTUAL(newmp), 0, PAGESIZE);
	_sL(VIRTUAL(newmp));
	u64 addr = 0;

	addr = 0;
	// 低16M全部直接映射
	while(addr < 16 * (1 << 20)) {
		page_table_set_entry(newmp, addr, addr + 7, true);
		addr += PAGESIZE;
	}
	// 构建内核代码数据页表
	// 0xffffffff81000000 -> 0x0000000001000000
	addr = (u64)&KERNEL_VMA;
	while (addr < heap_end) {
		page_table_set_entry(newmp, addr,  ABSOLUTE(addr & (~(0xfff)))+ 7, true);
		addr += PAGESIZE;
	}
	page_table_set_entry(newmp, VIRTUAL(newmp), newmp + 7, true);

	logd("new kernel page table occupies %d frames", frame_alloc_cnt);
	// logd("vir %d psy %d", 0x1000, virtual_to_physics(newmp, 0x1000));
	// logadd(VIRTUAL(newmp));
	// logadd(0xffffffff8b000007);
	// logadd(0xffffffff81019180);
	// logadd(0xffffffff81000000);
	// logadd(0xB8000);
	write_cr3(newmp);
	kernel_pml4 = newmp;
	// ASM("int $3");
	logd("load new page table finish");
}

static void rebuild_kernel_page() {
	logi("rebuild kernel page");
	// logi("0x%x%08x", h32(heap_end), l32(heap_end));
	u64 real_heap_end = ABSOLUTE(heap_end);
	_sL(&pml4);
	// logi("0x%x%08x", h32((u64)&pml4), l32((u64)&pml4));
	int cnt = 0;
	int pcnt = 0;
	// 临时内核页表地址
	void *early_pms = (void *)ABSOLUTE(&pml4);
	void *early_pme = (void *)ABSOLUTE(&pm_end);
	kernel_pml4 = (u64)early_pms;  // 内核4级页表地址
	for (int i = 0; i < mem_frame_cnt; i++) {
		u64 addr = mem_pool[i].address;
		// 将内核代码占用的页框标记为已占用
		if (addr >= (u64)&KERNEL_LMA && addr < real_heap_end) {
			if (addr >= (u64)early_pms && addr < (u64)early_pme) {
				// 这个页是内核临时页表页
				pcnt++;
			}
			cnt++;
			mem_pool[i].state = OCCUPIED;
			mem_pool[i].count = 1;
			list_remove(&mem_pool[i].next);
			mem_free_cnt--;
			list_add(&mem_pool[i].next, &mem_occupied_head);
			mem_occupied_cnt++;
		}
	}
	logi("kernel code %d pages", cnt);
	logi("kernel page %d pages", pcnt);
	// 写入新内核页表
	write_new_kernel_page_table();
	
	// 释放临时页表
	cnt = 0;
	u64 addr = (u64)&pml4;
	while (addr < (u64)&pm_end) {
		page_table_set_entry(kernel_pml4, addr, 0, false);
		addr += PAGESIZE;
		cnt++;
	}
	ListEntry *cursor = mem_occupied_head.next;
	while (cursor != &mem_occupied_head) {
		FrameEntry *frame = list_entry(cursor, FrameEntry, next);
		cursor = cursor->next;
		if (frame->address >= (u64) early_pms && frame->address < (u64) early_pme) {
			list_remove(&frame->next);
		}
	}
	logi("release %d early kernel frames.", cnt);
}
void init_page(void *mmap_addr, u64 mmap_length) {
	logi("init page");
	early_kmalloc_init();
	list_init(&mem_free_head);
	list_init(&mem_occupied_head);
	multiboot_memory_map_t *mmap;
	u64 mem_size = 0;
	for (mmap = (multiboot_memory_map_t *)mmap_addr;
	        (u64) mmap < (u64)mmap_addr + mmap_length;
	        mmap = (multiboot_memory_map_t *)((unsigned long ) mmap + mmap->size + sizeof(mmap->size))) {
		mem_size += mmap->len;	
	}
	logi("Detected memory size %d", mem_size);
	mem_frame_cnt = ROUND_UP(mem_size, 4096) / PAGESIZE;
	mem_pool = (FrameEntry*)early_kmalloc(mem_frame_cnt * sizeof(FrameEntry));
	for (int i = 0; i < mem_frame_cnt; i++)
		mem_pool[i].state = RESERVE;
	int ava_frame = 0;
	// 再次遍历内存表，初始化页框表
	for (mmap = (multiboot_memory_map_t *)mmap_addr;
	        (u64) mmap < (u64)mmap_addr + mmap_length;
	        mmap = (multiboot_memory_map_t *)((unsigned long ) mmap + mmap->size + sizeof(mmap->size))) {
		// init_mem_area(mmap->addr, mmap->len, mmap->type);

		u64 addr_end = ROUND_UP(mmap->addr + mmap->len, PAGESIZE);
		u64 addr   = ROUND_DOWN(mmap->addr, PAGESIZE);
		if (mmap->type == 1) {
			// 可用内存尽量小，不可用区域尽量大
			addr_end = ROUND_DOWN(mmap->addr + mmap->len, PAGESIZE);
			addr   = ROUND_UP(mmap->addr, PAGESIZE);
		}
		int frame_cnt = (addr_end - addr) / PAGESIZE;
		int frame_start = (addr) / PAGESIZE;
		// _si(frame_start);
		// _si(frame_cnt + frame_start);
		for (int i = frame_start; i < frame_cnt + frame_start; i++) {
			list_init(&mem_pool[i].next);
			mem_pool[i].count = 0;
			mem_pool[i].address = i * PAGESIZE;
			if (mmap->type == 2) {
				mem_pool[i].state = RESERVE;
			} else if (mem_pool[i].address < 16 * (1 << 20)) {
				mem_pool[i].state = DMA;
			} else {
				mem_pool[i].state = AVAILABLE;
				ava_frame++;
				list_add(&mem_pool[i].next, &mem_free_head);
			}
		}
	}
	logi("Build frame pool %d available, %d frames", ava_frame, mem_frame_cnt);
	early_kmalloc_depercated();  //  停用early_kmalloc
	rebuild_kernel_page();
}

void *kernel_pages_alloc(int n) {
	// 分配n个页面，此处应该加锁，后期使用buddy优化
	FrameEntry *t = frame_alloc();
	page_table_set_entry(kernel_pml4, VIRTUAL(t->address), t->address, true);
	n -= 1;
	while (n--) {
		FrameEntry *frame = frame_alloc();
		page_table_set_entry(kernel_pml4, VIRTUAL(t->address), frame->address, true);
	}
	return (void*)VIRTUAL(t->address);
}

void kernel_pages_release(void *addr, int n) {
	logi("release kernel %d page %llx", n, addr);
	// u64 frame = virtual_to_physics(kernel_pml4, (u64)addr);	
	u64 a = (u64)a;
	while (n--) {
		u64 frame = ABSOLUTE(a);
		frame /= PAGESIZE;
		FrameEntry *f = mem_pool + frame;
		frame_release(f);
		page_table_set_entry(kernel_pml4, a, 0, false);
		a += PAGESIZE;
	}
}

void *kernel_page_alloc() {
	return kernel_pages_alloc(1);
}

void kernel_page_release(void *addr) {
	kernel_pages_release(addr, 1);
}

