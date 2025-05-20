
#define FIELD_NAME(name) name##2025

struct First_tag {
    int a1;
    float FIELD_NAME(a2);
};

struct Second_tag {
    char b1[50];
    int b2;
} __attribute__((__packed__));


typedef struct {
    char c1;
    int c2;
} Third;

struct Fourth_tag {
    unsigned int d1 : 1;
    unsigned int d2 : 1;
    unsigned int d3 : 2;
};

struct Fifth_tag {
    int e1, e2, e3;
};

struct Sixth_tag {
    const int f1;
    volatile int f2;
};

struct Seventh_tag {
    float g1;
    int g2;
} G1, G2;

struct Octave_tag {
    struct {
        int h1;
        char h2;
    }; // unnamed struct
    int h3;
};

struct Ninth_tag {
    int i1;
    struct Ninth_tag* i2;
};

typedef struct Thenth_tag {
    struct {
        int h1;
        char h2;
    }; // unnamed struct
    int h3;
}Thenth;

#include <stdio.h>
int main(){
    printf("%ld\n", sizeof(struct First_tag));
    printf("%ld\n", sizeof(struct Second_tag));
    printf("%ld\n", sizeof(Third));
    printf("%ld\n", sizeof(struct Fourth_tag));
    printf("%ld\n", sizeof(struct Fifth_tag));
    printf("%ld\n", sizeof(struct Sixth_tag));
    printf("%ld\n", sizeof(struct Seventh_tag));
    printf("%ld\n", sizeof(struct Octave_tag));
    printf("%ld\n", sizeof(struct Ninth_tag));
    return 0;
}