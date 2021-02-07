#ifndef __LIB_KERNEL_LIST_H
#define __LIB_KERNEL_LIST_H

#include "global.h"

/* 获取结构体中某一成员在结构体中的偏移量 */
#define offset(struct_type, member)\
	(int)(&((struct_type*)0)->member)

/* ? */
#define elem2entry(struct_type, struct_member_name, elem_ptr)\
	(struct_type*)((int)elem_ptr - offset(struct_type, struct_member_name))

/* TODO:链表中的 node ，尚不清楚为什么没有数据区 */
struct list_elem {
	struct list_elem* prev;
	struct list_elem* next;
};

/** 双向链表本身
 *  head 和 tail 固定，新的 elem 加在两者之间
 *  因此 head.prev 和 tail.next 无意义
 */
struct list {
	struct list_elem head;
	struct list_elem tail;
};

/* 自定义函数类型 function，用于在 list_traversal 中做回调函数 */
typedef bool function(struct list_elem*, int);

void list_init(struct list*);
void list_insert_before(struct list_elem* before, struct list_elem* elem);
void list_push(struct list* plist, struct list_elem* elem);
// void list_iterate(struct list* plist);
void list_append(struct list* plist, struct list_elem* elem);
void list_remove(struct list_elem* pelem);
struct list_elem* list_pop(struct list* plist);
bool list_empty(struct list* plist);
uint32_t list_len(struct list* plist);
struct list_elem* list_traversal(struct list* plist, function func, int arg);
bool elem_find(struct list* plist, struct list_elem* obj_elem);

#endif