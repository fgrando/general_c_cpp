
// source of some examples: https://github1s.com/libuv/libuv/blob/HEAD/include/uv.h#L34-L50

#ifdef __cplusplus
extern "C" {
#endif


#include <stdio.h>


// Types
typedef struct fzg_person_s fzg_person_t;

typedef void (*print_int_cb)(fzg_person_t* p);

struct fzg_person_s {
    char name[256];
    int age; /* years */
    print_int_cb print;
};

typedef enum {
  FZG_COLOR_BLACK,
  FZG_COLOR_BLUE,
  FZG_COLOR_RED,
} fzg_color_t;

typedef enum {
  FZG_PIN_VAL1 = (1<<0),
  FZG_PIN_VAL2 = (1<<1),
  FZG_PIN_VAL3 = (1<<2),
  FZG_PIN_VAL4 = (1<<3),
} fzg_pin_t;


#ifdef __cplusplus
extern "C++" {
// some CPP needed code:
inline int hello() { return 0; }
}
#endif


void print_person(fzg_person_t* p) { printf("Name: '%s'\nAge: %d\n",p->name, p->age); }

int main(){

    fzg_person_t var = {0};
    var.age = 40;
    var.print = print_person;

    var.print(&var);

    return 0;
}


#ifdef __cplusplus
}
#endif
