#include <stdio.h>

double dummy(int a, double b, char c, char* d) {
#pragma GCCPLUGIN debug
  printf("No\n");
  if(a == 1) {
    return b * 2; 
  } else {
    return b * 3;
  }
}

// void dummy(int a, double b, char c, char* d) {
//   printf("%d\n", a);
//   printf("%f\n", b);
//   printf("%c\n", c);
//   printf("%s\n", d);
//   printf("No\n");
// }


int main(int argc, char *argv[]) {
  dummy(2, 1.0, 'c', "abc");
}
