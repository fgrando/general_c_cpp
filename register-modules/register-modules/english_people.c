#include <stdio.h> // printf, perror
#include <stdlib.h> // malloc, free
#include "people.h"
#include "people_list.h"


void say_hi_english(const char* name) {
    printf("Hello, my name is %s!\n", name);
}

void english_people_init()
{
    // Allocate picture buffer and simulate data
    size_t picture_size = 30;
    unsigned char *picture = malloc(picture_size);
    if (!picture) {
        perror("malloc picture");
        return;
    }
    for (size_t i = 0; i < picture_size; ++i) {
        picture[i] = (unsigned char)(i % 256); // Simulated image content
    }

    Person inputs[] = {
        {3, "Alice", 30, say_hi_english, picture, picture_size},
        {2, "Diana", 35, say_hi_english, picture, picture_size},
    };
    int n = sizeof(inputs) / sizeof(inputs[0]);

    for (int i = 0; i < n; i++) {
        if (!add_person(inputs[i])) {
			free(picture); // Free picture buffer on failure
			break; // failure
        }
    }
}