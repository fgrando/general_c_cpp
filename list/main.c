#include <stdio.h>
#include "genlist.h" // list facility

// example basic type to create a list
typedef struct
{
    char name[50];
    int age;
} Person;

// header declarations (struct and function prototypes)
GENLIST_DECLARE_HEADER(Person, persons)

// the type instance
persons_genlist_t *POPULATION_LIST = NULL;

// function to print the Person struct
void printer(Person *p)
{
    printf("Name: %s, Age: %d", p->name, p->age);
}

// source code declarations (implementations of the header functions)
// use same NAME and TYPE as in the header
GENLIST_DECLARE_SOURCE(Person, persons)

persons_genlist_t *persons_remove2(persons_genlist_t *head, Person *elem)
{
    if (elem == ((void *)0))
        return head;
    if (head == ((void *)0))
        return ((void *)0);
    persons_genlist_t *current = head;
    persons_genlist_t *previous = ((void *)0);
    while (current != ((void *)0))
    {
        if (0 == memcmp(&(current->data), elem, sizeof(Person)))
        {
            break;
        }
        previous = current;
        current = current->next;
    }
    if (current == ((void *)0))
        return head;
    if (previous == ((void *)0))
    {
        head = current->next;
    }
    else
    {
        previous->next = current->next;
    }
    free(current);
    return head;
}

int main()
{
    int val[3] = {1, 2, 3};
    char *str = "Hello, World!";
    printf("%s %d\n", str, val[3]); // BUG!

    int new_line = 1;
    persons_print_all(POPULATION_LIST, printer, new_line);

    Person p1 = {"Alice", 30};
    POPULATION_LIST = persons_add(POPULATION_LIST, &p1);
    Person p2 = {"Bob", 25};
    POPULATION_LIST = persons_add(POPULATION_LIST, &p2);
    Person p3 = {"Charlie", 35};
    POPULATION_LIST = persons_add(POPULATION_LIST, &p3);
    Person p4 = {"David", 40};
    POPULATION_LIST = persons_add(POPULATION_LIST, &p4);
    Person p5 = {"Eve", 28};
    POPULATION_LIST = persons_add(POPULATION_LIST, &p5);
    Person p6 = {"Frank", 32};
    POPULATION_LIST = persons_add(POPULATION_LIST, &p6);
    Person p7 = {"Grace", 29};
    POPULATION_LIST = persons_add(POPULATION_LIST, &p7);
    Person p8 = {"Heidi", 31};
    POPULATION_LIST = persons_add(POPULATION_LIST, &p8);

    printf("show all persons (%d)\n", persons_count(POPULATION_LIST));
    persons_print_all(POPULATION_LIST, printer, new_line);

    printf("removal\n");
    POPULATION_LIST = persons_remove_at(POPULATION_LIST, 10); // invalid index
    POPULATION_LIST = persons_remove_at(POPULATION_LIST, 1);  // Bob
    POPULATION_LIST = persons_remove_at(POPULATION_LIST, 6);  // Frank
    POPULATION_LIST = persons_remove(POPULATION_LIST, &p1);
    POPULATION_LIST = persons_remove(POPULATION_LIST, &p2);
    POPULATION_LIST = persons_remove(POPULATION_LIST, &p3);

    printf("survivers (%d)\n", persons_count(POPULATION_LIST));
    persons_print_all(POPULATION_LIST, printer, new_line);

    POPULATION_LIST = persons_free_all(POPULATION_LIST);
    persons_print_all(POPULATION_LIST, printer, new_line);
    printf("result (%d)\n", persons_count(POPULATION_LIST));
    return 0;
}