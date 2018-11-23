#include <stdio.h>

int main(void) {
	int a31 = 1 << 31;
	int a32 = 1 << 32;
	printf("%d\n", sizeof (int));
	printf("1<<31 : %x\n1<<32 : %x\n1<<31<<1 : %x\n", a31, a32, 1<<31<<1);
	return 0;
}
