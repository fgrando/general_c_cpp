#ifndef GENLIST_H_INCLUDED_
#define GENLIST_H_INCLUDED_
#include <stdlib.h> // calloc, free
#include <string.h> // memcmp

#define GENLIST_DECLARE_HEADER(TYPE, NAME)                                                \
    typedef struct genlist_struct##TYPE                                                   \
    {                                                                                     \
        TYPE data;                                                                        \
        struct genlist_struct##TYPE *next;                                                \
    } NAME##_genlist_t;                                                                   \
                                                                                          \
    NAME##_genlist_t *NAME##_add(NAME##_genlist_t *head, TYPE *elem);                     \
    NAME##_genlist_t *NAME##_remove(NAME##_genlist_t *head, TYPE *elem);                  \
    NAME##_genlist_t *NAME##_remove_at(NAME##_genlist_t *head, int index);                \
    void NAME##_print_all(NAME##_genlist_t *head, void (*printer)(TYPE *), int new_line); \
    int NAME##_count(NAME##_genlist_t *head);                                             \
    NAME##_genlist_t *NAME##_free_all(NAME##_genlist_t *head);

#define GENLIST_DECLARE_SOURCE(TYPE, NAME)    \
    GENLIST_DECLARE_ADD_API(TYPE, NAME)       \
    GENLIST_DECLARE_REMOVE_API(TYPE, NAME)    \
    GENLIST_DECLARE_REMOVE_AT_API(TYPE, NAME) \
    GENLIST_DECLARE_PRINT_ALL_API(TYPE, NAME) \
    GENLIST_DECLARE_COUNT_API(TYPE, NAME)     \
    GENLIST_DECLARE_FREE_ALL_API(TYPE, NAME)

#define GENLIST_DECLARE_ADD_API(TYPE, NAME)                           \
    NAME##_genlist_t *NAME##_add(NAME##_genlist_t *head, TYPE *elem)  \
    {                                                                 \
        NAME##_genlist_t *last = calloc(1, sizeof(NAME##_genlist_t)); \
        last->next = NULL;                                            \
        last->data = *elem;                                           \
        if (head != NULL)                                             \
        {                                                             \
            NAME##_genlist_t *current = head;                         \
            while (current->next != NULL)                             \
            {                                                         \
                current = current->next;                              \
            }                                                         \
            current->next = last;                                     \
        }                                                             \
        else                                                          \
        {                                                             \
            head = last;                                              \
        }                                                             \
        return head;                                                  \
    }

#define GENLIST_DECLARE_REMOVE_API(TYPE, NAME)                              \
    NAME##_genlist_t *NAME##_remove(NAME##_genlist_t *head, TYPE *elem)     \
    {                                                                       \
        if (head == NULL)                                                   \
            return NULL;                                                    \
        NAME##_genlist_t *current = head;                                   \
        NAME##_genlist_t *previous = NULL;                                  \
        while (current != NULL)                                             \
        {                                                                   \
            if (0 == memcmp(&(current->data), elem, sizeof(current->data))) \
            {                                                               \
                break;                                                      \
            }                                                               \
            previous = current;                                             \
            current = current->next;                                        \
        }                                                                   \
        if (current == NULL)                                                \
            return head;                                                    \
        if (previous == NULL)                                               \
        {                                                                   \
            head = current->next;                                           \
        }                                                                   \
        else                                                                \
        {                                                                   \
            previous->next = current->next;                                 \
        }                                                                   \
        free(current);                                                      \
        return head;                                                        \
    }

#define GENLIST_DECLARE_REMOVE_AT_API(TYPE, NAME)                         \
    NAME##_genlist_t *NAME##_remove_at(NAME##_genlist_t *head, int index) \
    {                                                                     \
        if (head == NULL)                                                 \
            return NULL;                                                  \
        NAME##_genlist_t *current = head;                                 \
        NAME##_genlist_t *previous = NULL;                                \
        int i = 0;                                                        \
        while (current != NULL && i < index)                              \
        {                                                                 \
            previous = current;                                           \
            current = current->next;                                      \
            i++;                                                          \
        }                                                                 \
        if (current == NULL)                                              \
            return head;                                                  \
        if (previous == NULL)                                             \
        {                                                                 \
            head = current->next;                                         \
        }                                                                 \
        else                                                              \
        {                                                                 \
            previous->next = current->next;                               \
        }                                                                 \
        free(current);                                                    \
        return head;                                                      \
    }

#define GENLIST_DECLARE_PRINT_ALL_API(TYPE, NAME)                                        \
    void NAME##_print_all(NAME##_genlist_t *head, void (*printer)(TYPE *), int new_line) \
    {                                                                                    \
        NAME##_genlist_t *current = head;                                                \
        while (current != NULL)                                                          \
        {                                                                                \
            printer(&current->data);                                                     \
            current = current->next;                                                     \
            if (new_line)                                                                \
            {                                                                            \
                printf("\n");                                                            \
            }                                                                            \
        }                                                                                \
    }

#define GENLIST_DECLARE_COUNT_API(TYPE, NAME) \
    int NAME##_count(NAME##_genlist_t *head)  \
    {                                         \
        int count = 0;                        \
        NAME##_genlist_t *current = head;     \
        while (current != NULL)               \
        {                                     \
            count++;                          \
            current = current->next;          \
        }                                     \
        return count;                         \
    }

#define GENLIST_DECLARE_FREE_ALL_API(TYPE, NAME)              \
    NAME##_genlist_t *NAME##_free_all(NAME##_genlist_t *head) \
    {                                                         \
        NAME##_genlist_t *current = head;                     \
        while (current != NULL)                               \
        {                                                     \
            NAME##_genlist_t *next = current->next;           \
            free(current);                                    \
            current = next;                                   \
        }                                                     \
        return NULL;                                          \
    }

#endif // GENLIST_H_INCLUDED_