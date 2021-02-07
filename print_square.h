/*
 * square.h
 *
 *  Created on: 6 de fev. de 2021
 *      Author: fgrando
 */

#ifndef PRINT_SQUARE_H_
#define PRINT_SQUARE_H_

#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>

#define MAX(x,y) ((x>y)?x:y)

extern void print_square();

/*

 7 7 7 7 7 7 7 7 7 7 7 7 7
 7 6 6 6 6 6 6 6 6 6 6 6 7
 7 6 5 5 5 5 5 5 5 5 5 6 7
 7 6 5 4 4 4 4 4 4 4 5 6 7
 7 6 5 4 3 3 3 3 3 4 5 6 7
 7 6 5 4 3 2 2 2 3 4 5 6 7
 7 6 5 4 3 2 1 2 3 4 5 6 7
 7 6 5 4 3 2 2 2 3 4 5 6 7
 7 6 5 4 3 3 3 3 3 4 5 6 7
 7 6 5 4 4 4 4 4 4 4 5 6 7
 7 6 5 5 5 5 5 5 5 5 5 6 7
 7 6 6 6 6 6 6 6 6 6 6 6 7
 7 7 7 7 7 7 7 7 7 7 7 7 7

 */

void print_square()
{
    int n = 7;

    for (int row = -n; row <= n; row++){
        for(int col = -n; col <= n; col++){
            if (row == 0) row+=2;
            if (col == 0) col+=2;

            printf("%d ", MAX(abs(row), abs(col)));
        }
        printf("\n");
    }
}



#endif /* PRINT_SQUARE_H_ */
