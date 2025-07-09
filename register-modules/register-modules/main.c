#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "people.h"
#include "people_list.h"
#include "people_init.h"

// Comparison function for binary search by ID
int compareIdForSearch(const void* key, const void* element) {
    const int* id = (const int*)key;
    const Person* p = (const Person*)element;
    return *id - p->id;
}

void init()
{
	// Initialize the people list
    people_list_init();
    
    // add people to it
    english_people_init();
	french_people_init();
    spanish_people_init();
}

void print_list() {
    Person* people = people_list();
    int count = people_list_count();

    printf("Sorted list:\n");
    for (int i = 0; i < count; i++) {
        printf("ID: %d, Name: %s, Age: %d, Picture size: %zu bytes\n",
            people[i].id, people[i].name, people[i].age, people[i].picture_size);
    }
}

int main() {
   
	init();

    print_list();

    // Search for a person with a specific ID using binary search
    int targetId = 4;
    Person* found = (Person*)bsearch(&targetId, people_list(), people_list_count(), sizeof(Person), compareIdForSearch);

    if (found) {
        printf("\nFound person with ID %d:\n", targetId);
        printf("Name: %s, Age: %d\n", found->name, found->age);
        printf("They say: ");
        found->say_hi(found->name);
    }
    else {
        printf("\nPerson with ID %d not found.\n", targetId);
    }

    return 0;
}
