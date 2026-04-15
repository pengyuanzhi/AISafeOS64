/**
 * @file    test_musl_aisafe.c
 * @brief   musl_aisafe 库链接测试
 * @version 1.0
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

int main(void)
{
    char *str;
    int len;

    printf("Hello from musl_aisafe!\\n");

    str = strdup("musl_aisafe test");
    if (str)
    {
        len = strlen(str);
        printf("String: %s, Length: %d\\n", str, len);
        free(str);
    }

    return 0;
}
