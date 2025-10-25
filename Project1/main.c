#include <stdio.h>


void myPrint(char input[100]) {
    printf("This is input text %s\n", input);
}

int main() {
    int myFavoriteNumber = 0;
    char myChar[100];
    printf("enter your Number\n");
    scanf("%d",&myFavoriteNumber);
    printf("You entered number is %d\n", myFavoriteNumber);
    printf("enter your char\n");
    scanf("%s",myChar);
    myPrint(myChar);
    return 1;
}


