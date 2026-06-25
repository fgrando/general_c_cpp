#include <stdio.h>

int var1 = 10;

void func1() {
    int var2 = 20;
    printf("Function 1 called %d %d\n", var1, var2);
    var1++;
}

int main() {
    func1();
    return 0;
}