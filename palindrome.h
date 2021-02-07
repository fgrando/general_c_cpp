/*
 * palindrome.h
 *
 *  Created on: 6 de fev. de 2021
 *      Author: fgrando
 */

#ifndef PALINDROME_H_
#define PALINDROME_H_

#include <cstdio>
#include <cstring>
int is_palindrome(const char* str){
    int sz = strlen(str);
    int begin = 0;
    int end = sz -1;

    for (int i = 0; (i < sz) && (begin != end); i++){
        if (str[begin] != str[end]) {
            return 0;
        }
        begin++;
        end--;
    }

    return 1;
}

void check(){
    const char* list[] = {"madam", "bob", "ana", "cat", "mosquitto"};
    for (int i = 0; i < 5; i++){
        printf("%s is palindrome? %s\n", list[i], (is_palindrome(list[i]) > 0 ? "yes" : "no"));
    }
}




#endif /* PALINDROME_H_ */
