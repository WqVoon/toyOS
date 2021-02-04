#include "list.h"
#include "interrupt.h"

#define THREAD_SAFE_CODE(code)\
{\
	intr_status old_status = intr_disable();\
	{code}\
	intr_set_status(old_status);\
}

/* 初始化双向链表 */
void list_init(struct list* list) {
	list->head.prev = NULL;
	list->head.next = &list->tail;
	list->tail.prev = &list->head;
	list->tail.next = NULL;
}

/* 把链表元素 elem 插入在元素 before 之前 */
void list_insert_before(struct list_elem* before, struct list_elem* elem) {
	THREAD_SAFE_CODE(
		before->prev->next = elem;
		elem->prev = before->prev;
		elem->next = before;
		before->prev = elem;
	);
}

/* 添加元素到链表队首 */
void list_push(struct list* plist, struct list_elem* elem) {
	list_insert_before(plist->head.next, elem);
}

/* 追加元素到链表队尾 */
void list_append(struct list* plist, struct list_elem* elem) {
	list_insert_before(&plist->tail, elem);
}

/* 从链表中删除 pelem */
void list_remove(struct list_elem* pelem) {
	THREAD_SAFE_CODE(
		pelem->prev->next = pelem->next;
		pelem->next->prev = pelem->prev;
	);
}

/* 将链表第一个元素弹出并返回 */
struct list_elem* list_pop(struct list* plist) {
	struct list_elem* elem = plist->head.next;
	list_remove(elem);
	return elem;
}

/* 在链表中查找 obj_elem ，成功则返回 true，否则返回 false */
bool elem_find(struct list* plist, struct list_elem* obj_elem) {
	struct list_elem* elem = plist->head.next;
	while (elem != &plist->tail) {
		// TODO: 这里比较的是两个 elem 的地址？
		if (elem == obj_elem) {
			return 1;
		}
		elem = elem->next;
	}
	return 0;
}

/* 用 func 和 arg 来判断是否有符合条件的 elem，若有返回指针，否则返回 NULL */
struct list_elem* list_traversal(struct list* plist, function func, int arg) {
	struct list_elem* elem = plist->head.next;
	if (list_empty(plist)) return NULL;

	while (elem != &plist->tail) {
		if (func(elem, arg)) {
			return elem;
		}
		elem = elem->next;
	}
	return NULL;
}

/* 返回链表的长度，不包括 head 和 tail */
uint32_t list_len(struct list* plist) {
	struct list_elem* elem = plist->head.next;
	uint32_t length = 0;

	while (elem != &plist->tail) {
		length++;
		elem = elem->next;
	}
	return length;
}

/* 判断链表是否为空 */
bool list_empty(struct list* plist) {
	return plist->head.next == &plist->tail;
}