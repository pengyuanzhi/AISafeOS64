## 2. MISRA-C:2012 核心规则

### 2.1 必须遵循的强制规则

#### 规则 1.1: 程序不得包含不可到达的代码
```c
/* ❌ 错误: return后的代码不可到达 */
return;
x = 5;  /* 违规 */

/* ✅ 正确 */
return;
```

#### 规则 2.1: 项目不得包含未定义或未指定的行为
```c
/* ❌ 错误: 未定义行为 - 有符号整数溢出 */
int32_t a = INT32_MAX;
int32_t b = a + 1;  /* 违规 */

/* ✅ 正确: 检查溢出 */
int32_t a = INT32_MAX;
int32_t b;
if (a < INT32_MAX) 
{
    b = a + 1;
}
```

#### 规则 3.1: 字符和字符串字面量中不得使用字符转义序列
```c
/* ❌ 错误: 使用八进制转义 */
char c = '\123';

/* ✅ 正确: 使用十六进制转义 */
char c = '\x53';
```

#### 规则 4.1: 字节和半字对象的访问必须使用明确的类型
```c
/* ❌ 错误: 通过指针别名访问 */
uint32_t value = 0x12345678U;
uint8_t byte = *((uint8_t *)&value);

/* ✅ 正确: 使用联合体或位移操作 */
uint32_t value = 0x12345678U;
uint8_t byte = (uint8_t)(value & 0xFFU);
```

#### 规则 5.1: 位域必须显式声明为signed或unsigned
```c
/* ❌ 错误: 隐式int类型 */
struct 
{
    int flag : 1;  /* 违规 */
};

/* ✅ 正确 */
struct 
{
    int32_t flag : 1;       /* 显式有符号 */
    uint32_t status : 8;    /* 显式无符号 */
};
```

#### 规则 6.1: 不允许使用char、short、enum等隐式类型转换
```c
/* ❌ 错误: 隐式类型转换 */
uint32_t x = 10;
int32_t y = -5;
if (x > y) {  /* 违规: y被隐式转换为uint32_t */
}

/* ✅ 正确: 显式比较 */
uint32_t x = 10U;
int32_t y = -5;
if ((y >= 0) && (x > (uint32_t)y)) {
}
```

#### 规则 7.1: 禁止八进制常量（除0外）和八进制转义序列
```c
/* ❌ 错误 */
int x = 010;  /* 违规: 八进制 */

/* ✅ 正确 */
int x = 10;
int x = 0xA;  /* 十六进制 */
```

#### 规则 7.2: 无符号整数常量必须有u或U后缀
```c
/* ❌ 错误 */
uint32_t mask = 0xFF;  /* 违规: 无符号常量缺少U后缀 */

/* ✅ 正确 */
uint32_t mask = 0xFFU;  /* 添加U后缀 */
```

#### 规则 7.3: 不应使用小写字母l作为字面量后缀
```c
/* ❌ 错误: 小写l容易与数字1混淆 */
int64_t value = 100l;  /* 违规 */

/* ✅ 正确: 使用大写L */
int64_t value = 100L;  /* 或使用LL表示long long */
int64_t value = 100LL;
```

#### 规则 7.4: 字符串字面量不应赋值给非const限定指针
```c
/* ❌ 错误 */
char *str = "hello";  /* 违规: 字符串字面量可修改 */
str[0] = 'H';  /* 未定义行为 */

/* ✅ 正确 */
const char *str = "hello";  /* 使用const限定符 */
char arr[] = "hello";  /* 或使用可修改的数组 */
```

#### 规则 8.1: 类型定义必须有标识符
```c
/* ❌ 错误: 无标识符的typedef */
typedef struct { int x; };  /* 违规 */

/* ✅ 正确 */
typedef struct { int x; } MyStruct_t;
```

#### 规则 9.1: 不允许使用变长数组（VLA）
```c
/* ❌ 错误: 变长数组 */
void func(uint32_t n) {
    int32_t arr[n];  /* 违规 */
}

/* ✅ 正确: 使用固定大小或动态分配 */
#define MAX_SIZE 256U
void func(uint32_t n) {
    int32_t arr[MAX_SIZE];
    if (n <= MAX_SIZE) {
        /* 使用arr */
    }
}
```

#### 规则 10.1: 禁止隐式整数类型转换
```c
/* ❌ 错误 */
uint32_t x = 10U;
int16_t y = x;  /* 违规: 隐式转换 */

/* ✅ 正确: 显式转换 */
uint32_t x = 10U;
int16_t y = (int16_t)x;
```

#### 规则 10.2: for循环控制变量不应在循环体内修改
```c
/* ❌ 错误 */
for (i = 0U; i < 10U; i++) {
    i = i + 2U;  /* 违规: 修改循环变量 */
}

/* ✅ 正确 */
for (i = 0U; i < 10U; i++) {
    /* 不修改i */
}
```

#### 规则 10.3: 赋值操作符不应用作真值表达式
```c
/* ❌ 错误 */
if (x = y) {  /* 违规: 赋值而非比较 */
}

/* ✅ 正确 */
if (x == y) {  /* 使用比较运算符 */
}
```

#### 规则 10.4: 逻辑运算符&&和||的操作数应为有效布尔值
```c
/* ❌ 不推荐 */
int32_t x = 5;
if (x && y) {  /* x不是布尔值 */
}

/* ✅ 更好 */
int32_t x = 5;
if ((x != 0) && (y != 0)) {  /* 显式布尔表达式 */
}
```

#### 规则 10.5: 逻辑非运算符!的操作数应为有效布尔值
```c
/* ❌ 不推荐 */
int32_t x = 5;
if (!x) {  /* x不是布尔值 */
}

/* ✅ 更好 */
int32_t x = 5;
if (x == 0) {  /* 显式比较 */
}
```

#### 规则 10.6: 位运算符~的操作数应为无符号整数
```c
/* ❌ 错误 */
int32_t x = 0xFF;
int32_t y = ~x;  /* 违规: 有符号数的位运算 */

/* ✅ 正确 */
uint32_t x = 0xFFU;
uint32_t y = ~x;  /* 无符号数的位运算 */
```

#### 规则 10.7: 不应使用逗号运算符
```c
/* ❌ 错误 */
for (i = 0U, j = 0U; i < 10U; i++, j++) {  /* 违规: 逗号运算符 */
}

/* ✅ 正确: 分别处理 */
for (i = 0U; i < 10U; i++) {
    j = i;  /* 在循环体内处理 */
}
```

#### 规则 10.8: 语句表达式值应为忽略
```c
/* ❌ 不推荐 */
x = (y++, z++);  /* 使用逗号表达式结果 */

/* ✅ 更好 */
y++;
x = z++;  /* 分别处理 */
```

#### 规则 11.1: 禁止指针和整数之间的转换（除uintptr_t外）
```c
/* ❌ 错误 */
uint32_t x = (uint32_t)ptr;  /* 违规 */

/* ✅ 正确 */
uintptr_t x = (uintptr_t)ptr;
```

#### 规则 11.2: 不应使用浮点变量作为循环计数器
```c
/* ❌ 错误 */
float f;
for (f = 0.0F; f < 10.0F; f++) {  /* 违规 */
}

/* ✅ 正确 */
uint32_t i;
for (i = 0U; i < 10U; i++) {
    float f = (float)i;
}
```

#### 规则 11.4: 指针与整数间不应转换（建议）
```c
/* ❌ 不推荐 */
uint32_t addr = (uint32_t)ptr;  /* 可能丢失信息 */
ptr = (uint8_t *)addr;          /* 可能无效 */

/* ✅ 更好 */
uintptr_t addr = (uintptr_t)ptr;  /* 使用正确的类型 */
ptr = (uint8_t *)addr;
```

#### 规则 11.5: void指针不应转换为对象指针
```c
/* ❌ 不推荐 */
void *vptr = malloc(100);
int32_t *iptr = (int32_t *)vptr;  /* 不安全 */

/* ✅ 更好 */
void *vptr = malloc(100);
int32_t *iptr = NULL;
if (vptr != NULL) {
    iptr = (int32_t *)vptr;
}
```

#### 规则 11.6: 从指针到void的转换
```c
/* ❌ 不推荐 */
const int32_t *ptr1;
void *ptr2 = (void *)ptr1;  /* 丢失const限定符 */

/* ✅ 更好 */
const int32_t *ptr1;
const void *ptr2 = (const void *)ptr1;  /* 保留const */
```

#### 规则 11.7: 指针值不应超出对象范围
```c
/* ❌ 错误 */
int32_t arr[10];
int32_t *ptr = &arr[10];  /* 违规: 超出范围 */

/* ✅ 正确 */
int32_t arr[10];
int32_t *ptr = &arr[9];  /* 最后一个元素 */
```

#### 规则 11.8: 指针减法结果应为指针差值类型
```c
/* ❌ 错误 */
int32_t arr[10];
int32_t diff = &arr[9] - &arr[0];  /* 类型可能错误 */

/* ✅ 正确 */
int32_t arr[10];
ptrdiff_t diff = &arr[9] - &arr[0];  /* 使用正确的类型 */
```

#### 规则 11.9: memcpy/memmove使用限制
```c
/* ❌ 错误: 重叠的内存区域 */
int32_t src[10];
int32_t dest[10];
memcpy(&dest[0], &src[1], 9 * sizeof(int32_t));  /* 可能重叠 */

/* ✅ 正确: 使用memmove处理重叠 */
int32_t src[10];
int32_t dest[10];
memmove(&dest[0], &src[1], 9 * sizeof(int32_t));  /* 安全处理重叠 */
```

#### 规则 12.1: 表达式的值不得依赖于求值顺序
```c
/* ❌ 错误: 未定义求值顺序 */
x = arr[i++] + arr[i++];  /* 违规 */

/* ✅ 正确 */
x = arr[i] + arr[i + 1U];
i = i + 2U;
```

#### 规则 13.1: 禁止初始化器列表中的未指定行为
```c
/* ❌ 错误: 跳过初始化器 */
int arr[5] = { [0] = 1, [3] = 4 };  /* 违规 */

/* ✅ 正确 */
int arr[5] = { 1, 0, 0, 4, 0 };
```

#### 规则 14.1: 禁止浮点数变量作为循环计数器
```c
/* ❌ 错误 */
float f;
for (f = 0.0F; f < 10.0F; f++) {  /* 违规 */
}

/* ✅ 正确 */
uint32_t i;
for (i = 0U; i < 10U; i++) {
    float f = (float)i;
}
```

#### 规则 15.1: 禁止goto语句
```c
/* ❌ 错误 */
goto error_handler;  /* 违规 */

/* ✅ 正确: 使用函数返回 */
if (error) {
    return ERROR_CODE;
}
```

#### 规则 16.1: 禁止递归函数调用
```c
/* ❌ 错误 */
uint32_t factorial(uint32_t n) {
    if (n <= 1U) {
        return 1U;
    }
    return n * factorial(n - 1U);  /* 违规: 递归 */
}

/* ✅ 正确: 使用迭代 */
uint32_t factorial(uint32_t n) {
    uint32_t result = 1U;
    uint32_t i;

    for (i = 2U; i <= n; i++) {
        result *= i;
    }
    return result;
}
```

#### 规则 17.1: 禁止可变参数函数（除特定情况）
```c
/* ❌ 错误 */
int my_printf(const char *fmt, ...);  /* 违规 */

/* ✅ 正确: 使用固定参数或宏定义 */
int my_printf(const char *str, int32_t val);
```

#### 规则 18.1: 指针运算必须限制在声明的数组对象内
```c
/* ❌ 错误 */
int32_t arr[10];
int32_t *p = &arr[10];  /* 违规: 超出数组范围 */

/* ✅ 正确 */
int32_t arr[10];
int32_t *p = &arr[9];
```

#### 规则 19.1: 禁止联合体（Union）用于类型双关
```c
/* ❌ 错误: 类型双关 */
union {
    uint32_t u32;
    uint16_t u16[2];
} data;
data.u32 = 0x12345678U;
uint16_t low = data.u16[0];  /* 违规 */

/* ✅ 正确: 使用位移操作 */
uint32_t value = 0x12345678U;
uint16_t low = (uint16_t)(value & 0xFFFFU);
```

#### 规则 20.1: 禁止#include包含带相对路径的文件
```c
/* ❌ 错误 */
#include "../include/types.h"  /* 违规 */

/* ✅ 正确 */
#include "types.h"
```

#### 规则 21.1: #include必须放在文件开头（除注释外）
```c
/* ✅ 正确 */
/* 文件头部注释 */
#include "types.h"
#include "scheduler.h"
```

### 2.2 建议遵循的规则

#### 规则 2.2: 禁止未知的实现相关行为
```c
/* ❌ 可能有问题 */
int32_t x = -1;
uint32_t y = (uint32_t)x;  /* 实现相关: 可能是0xFFFFFFFF或陷阱 */

/* ✅ 更安全 */
uint32_t y = (x < 0) ? 0U : (uint32_t)x;
```

#### 规则 11.3: 指针转换必须检查类型兼容性
```c
/* ❌ 警告 */
void *ptr = malloc(100);
int32_t *ip = (int32_t *)ptr;  /* 类型不明确 */

/* ✅ 更好 */
void *ptr = malloc(100);
int32_t *ip = NULL;
if (ptr != NULL) {
    ip = (int32_t *)ptr;
}
```

#### 规则 1.2: 禁止使用语言扩展
```c
/* ❌ 错误: 使用编译器特定扩展 */
int __attribute__((weak)) func(void);
asm volatile ("nop");

/* ✅ 正确: 使用标准C */
__attribute__((weak)) int func(void);  /* 移除扩展 */
/* 使用内联函数或编译器内置函数替代asm */
```

#### 规则 1.3: 禁止未定义或关键未指定行为
```c
/* ❌ 错误: 有符号整数溢出（未定义行为） */
int32_t a = INT32_MAX;
int32_t b = a + 1;  /* 违规 */

/* ✅ 正确: 检查溢出 */
int32_t a = INT32_MAX;
int32_t b;
if (a < INT32_MAX) {
    b = a + 1;
}
```

#### 规则 2.3: 项目不应包含未使用的类型声明
```c
/* ❌ 错误: 未使用的类型 */
typedef struct { int x; } UnusedType_t;  /* 违规 */

/* ✅ 正确: 删除未使用的类型或使用它 */
typedef struct { int x; } UsedType_t;
UsedType_t var;  /* 使用该类型 */
```

#### 规则 2.4: 项目不应包含未使用的标签声明
```c
/* ❌ 错误: 未使用的标签声明 */
struct UnusedTag { int x; };  /* 违规 */

/* ✅ 正确: 删除未使用的标签 */
/* 或者定义并使用该标签 */
struct UsedTag { int x; };
struct UsedTag var;
```

#### 规则 2.5: 项目不应包含未使用的宏声明
```c
/* ❌ 错误: 未使用的宏 */
#define UNUSED_MACRO 100  /* 违规 */

/* ✅ 正确: 删除未使用的宏 */
#define USED_MACRO 100
int x = USED_MACRO;  /* 使用宏 */
```

#### 规则 2.6: 函数不应包含未使用的标签声明
```c
/* ❌ 错误: 未使用的标签 */
void func(void) {
    unused_label:  /* 违规 */
    return;
}

/* ✅ 正确: 删除未使用的标签 */
void func(void) {
    return;
}
```

#### 规则 2.7: 函数不应有未使用的参数
```c
/* ❌ 错误: 未使用的参数 */
void func(int param) {  /* param未使用 */
    (void)param;  /* 如果参数确实不需要，注释掉 */
}

/* ✅ 正确: 使用参数或删除 */
void func(int param) {
    int x = param + 1;  /* 使用参数 */
}

/* 或者使用 (void)param 明确标记未使用 */
void func(int param) {
    (void)param;  /* 明确标记为有意未使用 */
}
```

#### 规则 5.2: 同一作用域和命名空间的标识符必须不同
```c
/* ❌ 错误: 同名标识符 */
void func(void) {
    int32_t x;  /* 第一个声明 */
    int32_t x;  /* 违规: 重复声明 */
}

/* ✅ 正确: 使用不同的标识符 */
void func(void) {
    int32_t x;
    int32_t y;  /* 不同的标识符 */
}
```

#### 规则 5.3: 内层作用域标识符不应隐藏外层标识符
```c
/* ❌ 错误: 内层作用域隐藏外层标识符 */
void func(void) {
    int32_t x = 10;
    {
        int32_t x = 20;  /* 违规: 隐藏外层x */
    }
}

/* ✅ 正确: 使用不同的标识符 */
void func(void) {
    int32_t x = 10;
    {
        int32_t y = 20;  /* 不隐藏外层标识符 */
    }
}
```

#### 规则 5.4: 宏标识符必须不同
```c
/* ❌ 错误: 重复的宏名称 */
#define MAX_SIZE 100
#define MAX_SIZE 200  /* 违规: 重复定义 */

/* ✅ 正确: 使用不同的宏名称 */
#define MAX_SIZE 100
#define MIN_SIZE 200  /* 不同的宏名称 */
```

#### 规则 5.5: 标识符必须与宏名不同
```c
/* ❌ 错误: 标识符与宏名相同 */
#define STATUS 100
int32_t STATUS = 200;  /* 违规: 与宏名冲突 */

/* ✅ 正确: 使用不同的名称 */
#define STATUS 100
int32_t status_code = 200;  /* 不同的名称 */
```

#### 规则 5.6: typedef名必须是唯一标识符
```c
/* ❌ 错误: typedef名与其他标识符冲突 */
typedef int32_t Status_t;
int32_t Status_t = 10;  /* 违规: 类型名与变量名冲突 */

/* ✅ 正确: 使用不同的名称 */
typedef int32_t Status_t;
int32_t status_value = 10;  /* 不同的名称 */
```

#### 规则 5.7: 标签名必须是唯一标识符
```c
/* ❌ 错误: 标签名与其他标识符冲突 */
struct Point { int x; int y; };
int32_t Point = 10;  /* 违规: 标签名与变量名冲突 */

/* ✅ 正确: 使用不同的名称 */
struct Point { int x; int y; };
int32_t point_count = 10;  /* 不同的名称 */
```

#### 规则 5.8: 外部链接标识符必须唯一
```c
/* ❌ 错误: 外部标识符重名 */
/* file1.c */
int32_t counter = 0;

/* file2.c */
int32_t counter = 0;  /* 违规: 外部链接冲突 */

/* ✅ 正确: 使用static或不同的名称 */
/* file1.c */
int32_t counter = 0;

/* file2.c */
static int32_t counter = 0;  /* 或使用不同的名称 */
```

#### 规则 5.9: 内部链接标识符应该唯一
```c
/* ❌ 不推荐: 内部标识符重名 */
/* file1.c */
static int32_t temp = 0;

/* file2.c */
static int32_t temp = 0;  /* 不推荐: 可能引起混淆 */

/* ✅ 正确: 使用不同的名称 */
/* file1.c */
static int32_t temp1 = 0;

/* file2.c */
static int32_t temp2 = 0;  /* 不同的名称 */
```

#### 规则 8.2: 函数类型必须是原型形式并带命名参数
```c
/* ❌ 错误: 旧式函数声明 */
int32_t func();  /* 违规: 无原型 */
int32_t func(int, int);  /* 违规: 参数无名 */

/* ✅ 正确: 现代原型形式 */
int32_t func(void);
int32_t func(int32_t param1, int32_t param2);
```

#### 规则 8.3: 对象或函数的所有声明应使用相同名称和类型限定符
```c
/* ❌ 错误: 声明不一致 */
/* file1.c */
extern const int32_t value;

/* file2.c */
extern int32_t value;  /* 违规: 缺少const限定符 */

/* ✅ 正确: 声明一致 */
/* file1.c */
extern const int32_t value;

/* file2.c */
extern const int32_t value;  /* 保持一致 */
```

#### 规则 8.4: 兼容的声明在定义外部对象或函数时必须可见
```c
/* ❌ 错误: 定义时没有兼容声明 */
int32_t value = 10;  /* 违规: 缺少声明 */

/* ✅ 正确: 先声明后定义 */
extern int32_t value;  /* 声明 */
int32_t value = 10;     /* 定义 */
```

#### 规则 8.5: 外部对象或函数应在一个文件中声明一次
```c
/* ❌ 错误: 多次声明 */
/* header1.h */
extern int32_t counter;

/* header2.h */
extern int32_t counter;  /* 违规: 重复声明 */

/* ✅ 正确: 在一个头文件中声明 */
/* common.h */
extern int32_t counter;

/* 其他文件包含 common.h */
```

#### 规则 8.6: 外部链接标识符应有且仅有一个外部定义
```c
/* ❌ 错误: 多个外部定义 */
/* file1.c */
int32_t global = 0;

/* file2.c */
int32_t global = 0;  /* 违规: 重复定义 */

/* ✅ 正确: 只有一个定义，其他用extern */
/* file1.c */
int32_t global = 0;  /* 定义 */

/* file2.c */
extern int32_t global;  /* 声明 */
```

---

