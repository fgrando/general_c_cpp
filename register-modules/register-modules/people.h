#pragma once

typedef void (*SayHiFunc)(const char* name);

typedef struct {
    int id;
    char name[50];
    int age;
    SayHiFunc say_hi;  // function pointer to say_hi()
    unsigned char* picture;   // Pointer to picture data
    size_t picture_size;      // Size of picture buffers
} Person;
