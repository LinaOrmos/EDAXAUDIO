#include<stdio.h>
int main (){
int n, sum = 0, c, value;
printf("dime cuántos números quieres sumar perro\n");
scanf("%d" , & n);
printf("Enter %d integers\n" , n);
for (c = 1 ; c<= n; c++)
{
scanf("%d" , & value);
sum = sum + value;
}
printf("la suma es =%d\n" , sum );
return 0;
}
