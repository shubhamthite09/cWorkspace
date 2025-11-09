#include <stdio.h>
#include <stdbool.h>


int main() {

    // calculate area and perimeter of a rectangle
    double length = 5.0;
    double width = 3.0;
    double area = length * width;
    double perimeter = 2 * (length + width);

    printf("The area of ractangle having height %f and width %f is %f and perimiter is %f\n", length, width, area, perimeter);
    
    return 0;
}


