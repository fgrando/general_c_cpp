#include <stdint.h>
#include <stdlib.h> // exit()
#include <stdio.h> // malloc, realloc, free

#include "people.h"


static Person* people = NULL;
static int capacity = 2; // expected number of people
static int count = 0;

void people_list_init() {
    people = malloc(capacity * sizeof(Person));
    if (!people) {
        perror("malloc");
        exit(EXIT_FAILURE); // handle memory allocation failure
    }
    count = 0;
}

Person* people_list() {
    return people;
}

int people_list_count() { 
    return count; 
}

int add_person2(Person** people, int* count, int* capacity, Person new_person) {
    if (*count >= *capacity) {
        *capacity *= 2;
        Person* temp = realloc(*people, *capacity * sizeof(Person));
        if (!temp) {
            perror("realloc");
            free(*people);
            *people = NULL;
            return 0; // failure
        }
        *people = temp;
    }

    (*people)[(*count)++] = new_person;

    return 1; // success
}




// Comparison function for sorting by ID
int compareById(const void* a, const void* b) {
    const Person* pa = (const Person*)a;
    const Person* pb = (const Person*)b;
    return pa->id - pb->id;
}

int add_person(Person new_person) {
    int ret = add_person2(&people, &count, &capacity, new_person);

    if (count > 1)
    {
        // Sort the list by ID
        qsort(people, count, sizeof(Person), compareById);
    }
    return ret;
}