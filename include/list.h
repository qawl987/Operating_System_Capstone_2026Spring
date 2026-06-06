/**
 * Simplified generic circular doubly linked list
 * Based on Linux kernel's list.h
 */
#ifndef _LIST_H
#define _LIST_H

#include <stddef.h>

/**
 * struct list_head - Circular doubly linked list node
 * Embed this in your struct to make it a list element
 */
struct list_head {
    struct list_head *next;
    struct list_head *prev;
};

/**
 * container_of - Get the struct containing this member
 * @ptr:    pointer to the member
 * @type:   type of the container struct
 * @member: name of the member within the struct
 */
#define container_of(ptr, type, member)                                        \
    ((type *)((char *)(ptr) - offsetof(type, member)))

/**
 * list_entry - Get the struct for this list entry
 * @ptr:    the &struct list_head pointer
 * @type:   the type of the struct this is embedded in
 * @member: the name of the list_head within the struct
 */
#define list_entry(ptr, type, member) container_of(ptr, type, member)

/**
 * list_first_entry - Get the first element from a list
 * @ptr:    the list head
 * @type:   the type of the struct
 * @member: the name of the list_head within the struct
 * Note: list must not be empty
 */
#define list_first_entry(ptr, type, member)                                    \
    list_entry((ptr)->next, type, member)

/**
 * list_for_each - iterate over a list
 * @pos:  the &struct list_head to use as a loop cursor
 * @head: the head for your list
 */
#define list_for_each(pos, head)                                               \
    for (pos = (head)->next; pos != (head); pos = pos->next)

/**
 * list_for_each_safe - iterate over a list safe against removal
 * @pos:  the &struct list_head to use as a loop cursor
 * @n:    another &struct list_head to use as temporary storage
 * @head: the head for your list
 */
#define list_for_each_safe(pos, n, head)                                       \
    for (pos = (head)->next, n = pos->next; pos != (head);                     \
         pos = n, n = pos->next)

/**
 * list_for_each_entry - iterate over list of given type
 * @pos:    the type * to use as a loop cursor
 * @head:   the head for your list
 * @member: the name of the list_head within the struct
 */
#define list_for_each_entry(pos, head, member)                                 \
    for (pos = list_entry((head)->next, typeof(*pos), member);                 \
         &pos->member != (head);                                               \
         pos = list_entry(pos->member.next, typeof(*pos), member))

/**
 * list_for_each_entry_safe - iterate over list safe against removal
 * @pos:    the type * to use as a loop cursor
 * @n:      another type * to use as temporary storage
 * @head:   the head for your list
 * @member: the name of the list_head within the struct
 */
#define list_for_each_entry_safe(pos, n, head, member)                         \
    for (pos = list_entry((head)->next, typeof(*pos), member),                 \
        n = list_entry(pos->member.next, typeof(*pos), member);                \
         &pos->member != (head);                                               \
         pos = n, n = list_entry(n->member.next, typeof(*n), member))

/**
 * LIST_HEAD_INIT - Initialize a list head at compile time
 */
#define LIST_HEAD_INIT(name) {&(name), &(name)}

/**
 * LIST_HEAD - Declare and initialize a list head
 */
#define LIST_HEAD(name) struct list_head name = LIST_HEAD_INIT(name)

/**
 * INIT_LIST_HEAD - Initialize a list_head at runtime
 * @list: list_head structure to be initialized
 */
static inline void INIT_LIST_HEAD(struct list_head *list) {
    list->next = list;
    list->prev = list;
}

/**
 * list_empty - Check if a list is empty
 * @head: the list to test
 */
static inline int list_empty(const struct list_head *head) {
    return head->next == head;
}

/**
 * __list_add - Insert a new entry between two consecutive entries
 * Internal use only
 */
static inline void __list_add(struct list_head *new, struct list_head *prev,
                              struct list_head *next) {
    next->prev = new;
    new->next = next;
    new->prev = prev;
    prev->next = new;
}

/**
 * list_add - Add a new entry after the head (stack behavior)
 * @new:  new entry to be added
 * @head: list head to add it after
 */
static inline void list_add(struct list_head *new, struct list_head *head) {
    __list_add(new, head, head->next);
}

/**
 * list_add_tail - Add a new entry before the head (queue behavior)
 * @new:  new entry to be added
 * @head: list head to add it before
 */
static inline void list_add_tail(struct list_head *new,
                                 struct list_head *head) {
    __list_add(new, head->prev, head);
}

/**
 * __list_del - Delete entry by making prev/next point to each other
 * Internal use only
 */
static inline void __list_del(struct list_head *prev, struct list_head *next) {
    next->prev = prev;
    prev->next = next;
}

/**
 * list_del - Delete entry from list
 * @entry: the element to delete
 * Note: entry is in undefined state after this, use list_del_init if you
 *       want to reuse it
 */
static inline void list_del(struct list_head *entry) {
    __list_del(entry->prev, entry->next);
    entry->next = (void *)0;
    entry->prev = (void *)0;
}

/**
 * list_del_init - Delete entry from list and reinitialize it
 * @entry: the element to delete
 */
static inline void list_del_init(struct list_head *entry) {
    __list_del(entry->prev, entry->next);
    INIT_LIST_HEAD(entry);
}

/**
 * list_move - Delete from one list and add as another's head
 * @list: the entry to move
 * @head: the head that will precede our entry
 */
static inline void list_move(struct list_head *list, struct list_head *head) {
    __list_del(list->prev, list->next);
    list_add(list, head);
}

/**
 * list_move_tail - Delete from one list and add as another's tail
 * @list: the entry to move
 * @head: the head that will follow our entry
 */
static inline void list_move_tail(struct list_head *list,
                                  struct list_head *head) {
    __list_del(list->prev, list->next);
    list_add_tail(list, head);
}

#endif /* _LIST_H */
