#include <stdint.h>
#include <stdio.h>

#define CACHE_LINE_SIZE 64

#define CACHE_ALIGN(x) __attribute__((aligned(CACHE_LINE_SIZE)))

typedef struct
{
    int a;
    int b;
} CACHE_ALIGN(64) test_struct_t;

int main(void)
{
    test_struct_t s;
    printf("Size: %lu\n", sizeof(test_struct_t));
    return 0;
}
