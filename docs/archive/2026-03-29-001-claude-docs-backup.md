# AISafe64 C代码生成规范系统提示词

本文档为AISafe64操作系统（AI-Generated, Safety-Certifiable, Native 64-bit RTOS）开发提供完整的C代码生成规范，严格遵循MISRA-C:2012标准，并针对ARM64多核SMP架构进行了扩展。

---

## 1. 核心原则

### 1.1 安全编码原则
- **可预测性优先**: 所有代码行为必须可预测，避免未定义行为
- **显式优于隐式**: 所有类型转换、操作必须显式声明
- **最小权限原则**: 限制数据访问范围，使用const和volatile
- **防御性编程**: 假设所有外部输入都可能有问题
- **内存安全**: 确保没有内存泄漏、越界访问、悬空指针

### 1.2 多核安全原则
- **原子操作**: 多核共享数据必须使用原子操作或锁保护
- **内存屏障**: ARMv8弱内存模型，必须正确使用内存屏障
- **缓存一致性**: 确保多核间数据一致性
- **无锁编程**: 优先使用无锁数据结构和算法

### 1.3 功能安全原则
- **可追溯性**: 每个代码模块对应需求文档
- **可测试性**: 所有代码必须可测试
- **可验证性**: 复杂度限制，便于形式化验证
- **错误处理**: 所有可能的错误路径都必须处理

---

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

## 3. ARM64特定编码规范

### 3.1 数据类型规范

#### 3.1.1 标准数据类型
```c
/* 固定宽度整数类型（必须使用） */
#include <stdint.h>

int8_t    i8;    /* 8位有符号整数 */
uint8_t   u8;    /* 8位无符号整数 */
int16_t   i16;   /* 16位有符号整数 */
uint16_t  u16;   /* 16位无符号整数 */
int32_t   i32;   /* 32位有符号整数 */
uint32_t  u32;   /* 32位无符号整数 */
int64_t   i64;   /* 64位有符号整数 */
uint64_t  u64;   /* 64位无符号整数 */

/* 指针大小整数类型 */
uintptr_t  ptr_value;  /* 可存放指针的无符号整数 */
intptr_t   ptr_signed; /* 可存放指针的有符号整数 */

/* 最大/最小宽度整数类型 */
intmax_t   max_int;
uintmax_t  max_uint;
```

#### 3.1.2 类型定义命名规范
```c
/* 结构体和联合体必须使用typedef */
typedef struct TaskControlBlock TCB_t;
typedef struct Mutex Mutex_t;
typedef struct PageTable PageTable_t;

/* 函数指针类型定义 */
typedef void (*TaskEntry_t)(void);
typedef uint32_t (*ErrorHandler_t)(uint32_t error_code);

/* 枚举类型定义 */
typedef enum 
{
    TASK_READY = 0U,      /* 就绪态：等待CPU调度 */
    TASK_RUNNING,         /* 运行态：正在执行 */
    TASK_BLOCKED,         /* 阻塞态：等待资源（信号量、消息队列） */
    TASK_SLEEPING,        /* 休眠态：延时等待，超时自动唤醒 */
    TASK_SUSPENDED        /* 挂起态：被挂起，需要显式恢复 */
} TaskState_t;
```

### 3.2 对齐规范

#### 3.2.1 数据对齐
```c
/* 16字节对齐（SIMD优化） */
typedef struct 
{
    uint64_t data[2];
} __attribute__((aligned(16))) SIMDData_t;

/* 缓存行对齐（64字节，多核共享数据） */
typedef struct 
{
    atomic_uint64_t lock;
    uint64_t data[7];
} __attribute__((aligned(64))) CacheLine_t;

/* 页对齐（4KB） */
typedef struct 
{
    uint64_t entries[512];
} __attribute__((aligned(4096))) PageTable_t;
```

#### 3.2.2 栈对齐
```c
/* 函数入口必须16字节对齐（ARM64 ABI要求） */
void task_entry(void) 
{
    /* 栈指针保证16字节对齐 */
}

/* 分配栈时确保16字节对齐 */
uint64_t *stack_alloc(uint32_t size) 
{
    uint64_t *stack = malloc(size + 15U);
    if (stack != NULL) 
    {
        stack = (uint64_t *)(((uintptr_t)stack + 15U) & ~0xFU);
    }
    return stack;
}
```

### 3.3 内联汇编规范

#### 3.3.1 基本内联汇编
```c
/* 使用volatile关键字防止优化 */
static inline void memory_barrier(void) 
{
    __asm__ volatile("dmb ish" ::: "memory");
}

/* 带输入输出的内联汇编 */
static inline uint64_t get_cycle_count(void) 
{
    uint64_t count;
    __asm__ volatile("mrs %0, cntvct_el0" : "=r"(count));
    return count;
}

/* 带约束的内联汇编 */
static inline void set_page_table(uint64_t ttbr0) 
{
    __asm__ volatile("msr ttbr0_el1, %0" :: "r"(ttbr0));
    __asm__ volatile("isb");
}
```

#### 3.3.2 内存屏障宏定义
```c
/* 数据同步屏障 */
#define barrier() \
    __asm__ volatile("dmb ish" ::: "memory")

/* 数据同步屏障（轻量级，仅加载） */
#define barrier_load() \
    __asm__ volatile("dmb ishld" ::: "memory")

/* 数据同步屏障（轻量级，仅存储） */
#define barrier_store() \
    __asm__ volatile("dmb ishst" ::: "memory")

/* 指令同步屏障 */
#define barrier_inst() \
    __asm__ volatile("isb")

/* 完整屏障（数据 + 指令） */
#define full_barrier() \
    do { \
        __asm__ volatile("dmb ish" ::: "memory"); \
        __asm__ volatile("isb"); \
    } while (0)
```

### 3.4 原子操作规范

#### 3.4.1 C11原子操作
```c
#include <stdatomic.h>

/* 原子加载 */
static inline uint32_t atomic_load_acquire(atomic_uint *ptr) {
    return atomic_load_explicit(ptr, memory_order_acquire);
}

/* 原子存储 */
static inline void atomic_store_release(atomic_uint *ptr, uint32_t value) {
    atomic_store_explicit(ptr, value, memory_order_release);
}

/* 原子加法（返回旧值） */
static inline uint32_t atomic_fetch_add(atomic_uint *ptr, uint32_t value) {
    return atomic_fetch_add_explicit(ptr, value, memory_order_acq_rel);
}

/* 原子比较交换 */
static inline bool atomic_cas(atomic_uint *ptr,
                               uint32_t *expected,
                               uint32_t desired) {
    return atomic_compare_exchange_strong_explicit(
        ptr, expected, desired,
        memory_order_acq_rel,
        memory_order_acquire
    );
}
```

#### 3.4.2 ARM64特定的原子操作
```c
/* LL/SC（Load-Linked/Store-Conditional）模式 */
static inline uint32_t atomic_inc(volatile uint32_t *ptr) {
    uint32_t old;
    uint32_t new;

    do {
        old = *ptr;
        new = old + 1U;
        __asm__ volatile("": ::: "memory");  /* 编译器屏障 */
    } while (!__builtin_expect(
        __sync_bool_compare_and_swap(ptr, old, new), 1
    ));

    return old;
}

/* ARM64 LDXR/STXR指令 */
static inline bool atomic_cas_arm64(uint64_t *ptr,
                                    uint64_t expected,
                                    uint64_t new) {
    uint64_t tmp;
    int result;

    __asm__ volatile(
        "   ldxr %0, [%2]\n"
        "   cmp %0, %3\n"
        "   b.ne 1f\n"
        "   stxr %w1, %4, [%2]\n"
        "1:\n"
        : "=&r"(tmp), "=&r"(result)
        : "r"(ptr), "r"(expected), "r"(new)
        : "cc", "memory"
    );

    return (result == 0);
}
```

### 3.5 多核编程规范

#### 3.5.1 自旋锁模式
```c
/* Ticket Lock（公平自旋锁） */
typedef struct 
{
    atomic_uint16_t next_ticket;
    atomic_uint16_t serving_ticket;
} TicketLock_t;

static inline void ticket_lock_acquire(TicketLock_t *lock) 
{
    uint16_t my_ticket = atomic_fetch_add(&lock->next_ticket, 1U);

    while (atomic_load(&lock->serving_ticket) != my_ticket) 
    {
        /* 使用wfe指令降低功耗 */
        __asm__ volatile("wfe");
    }

    /* 获取锁后的内存屏障 */
    barrier();
}

static inline void ticket_lock_release(TicketLock_t *lock) 
{
    /* 释放锁前的内存屏障 */
    barrier();
    atomic_fetch_add(&lock->serving_ticket, 1U);
}
```

#### 3.5.2 禁止抢占
```c
/* 禁止调度器（禁止任务切换） */
static inline void scheduler_disable(void) {
    uint32_t cpu_id = get_cpu_id();
    scheduler.lock_count[cpu_id]++;
    barrier();
}

static inline void scheduler_enable(void) {
    uint32_t cpu_id = get_cpu_id();

    scheduler.lock_count[cpu_id]--;
    barrier();

    if (scheduler.lock_count[cpu_id] == 0U) {
        schedule();  /* 触发调度 */
    }
}
```

#### 3.5.3 核心间中断（IPI）
```c
/* IPI类型定义 */
#define IPI_RESCHEDULE   0U
#define IPI_STOP         1U
#define IPI_TIMER        2U
#define IPI_CALL_FUNC    3U

/* 发送IPI */
static inline void ipi_send(uint32_t target_cpu, uint32_t ipi_type) {
    uint32_t cpu_mask = (1U << target_cpu);

    /* 写入GIC SGI寄存器 */
    uint64_t sgi_reg = ((uint64_t)ipi_type << 24U) |
                       ((uint64_t)target_cpu << 16U);

    __asm__ volatile(
        "msr ICC_SGI1R_EL1, %0"
        :: "r"(sgi_reg)
    );
}
```

### 3.6 异常处理规范

#### 3.6.1 异常级别
```c
/* ARMv8异常级别 */
#define EL0    0U   /* User mode */
#define EL1    1U   /* Kernel mode */
#define EL2    2U   /* Hypervisor mode */
#define EL3    3U   /* Secure monitor mode */

/* 获取当前异常级别 */
static inline uint32_t get_current_el(void) {
    uint64_t currentel;
    __asm__ volatile("mrs %0, currentel" : "=r"(currentel));
    return (uint32_t)((currentel >> 2U) & 0x3U);
}

/* 从EL1切换到EL0 */
static inline void drop_to_el0(void) {
    __asm__ volatile(
        "mov x0, #0\n"
        "msr spsr_el1, x0\n"
        "eret"
    );
}
```

#### 3.6.2 异常向量表
```c
/* 异常向量表对齐要求（2KB = 0x800字节） */
typedef void (*ExceptionHandler_t)(void);

__attribute__((aligned(2048))) ExceptionHandler_t exception_table[16] = {
    /* 当前EL，SP0，同步异常 */
    exception_sync_sp0,
    /* 当前EL，SP0，IRQ异常 */
    exception_irq_sp0,
    /* 当前EL，SP0，FIQ异常 */
    exception_fiq_sp0,
    /* 当前EL，SP0，SError异常 */
    exception_serror_sp0,

    /* 当前EL，SPx，同步异常 */
    exception_sync_spx,
    /* 当前EL，SPx，IRQ异常 */
    exception_irq_spx,
    /* 当前EL，SPx，FIQ异常 */
    exception_fiq_spx,
    /* 当前EL，SPx，SError异常 */
    exception_serror_spx,

    /* 低EL（AArch64），同步异常 */
    exception_sync_lower64,
    /* 低EL（AArch64），IRQ异常 */
    exception_irq_lower64,
    /* 低EL（AArch64），FIQ异常 */
    exception_fiq_lower64,
    /* 低EL（AArch64），SError异常 */
    exception_serror_lower64,

    /* 低EL（AArch32），同步异常 */
    exception_sync_lower32,
    /* 低EL（AArch32），IRQ异常 */
    exception_irq_lower32,
    /* 低EL（AArch32），FIQ异常 */
    exception_fiq_lower32,
    /* 低EL（AArch32），SError异常 */
    exception_serror_lower32,
};
```

### 3.7 缓存操作规范

#### 3.7.1 缓存维护
```c
/* 清理数据缓存到内存 */
static inline void dcache_clean(void *addr, uint32_t size) 
{
    uint64_t start = (uint64_t)addr;
    uint64_t end = start + (uint64_t)size;

    start &= ~0x3FULL;  /* 64字节缓存行对齐 */

    while (start < end) 
    {
        __asm__ volatile("dc cvac, %0" :: "r"(start) : "memory");
        start += 64U;
    }
}

/* 使数据缓存无效 */
static inline void dcache_invalidate(void *addr, uint32_t size) 
{
    uint64_t start = (uint64_t)addr;
    uint64_t end = start + (uint64_t)size;

    start &= ~0x3FULL;

    while (start < end) 
    {
        __asm__ volatile("dc ivac, %0" :: "r"(start) : "memory");
        start += 64U;
    }
}

/* 清理并使数据缓存无效 */
static inline void dcache_clean_and_invalidate(void *addr, uint32_t size) 
{
    uint64_t start = (uint64_t)addr;
    uint64_t end = start + (uint64_t)size;

    start &= ~0x3FULL;

    while (start < end) 
    {
        __asm__ volatile("dc civac, %0" :: "r"(start) : "memory");
        start += 64U;
    }
}
```

#### 3.7.2 TLB操作
```c
/* 使TLB项无效（所有地址） */
static inline void tlb_invalidate_all(void) 
{
    __asm__ volatile("tlbi vmalle1is");
    barrier();
}

/* 使TLB项无效（指定地址） */
static inline void tlb_invalidate_page(uint64_t addr) 
{
    uint64_t page = addr >> 12U;
    __asm__ volatile("tlbi vae1is, %0" :: "r"(page));
    barrier();
}

/* 同步TLB操作 */
static inline void tlb_sync(void) 
{
    barrier();
    __asm__ volatile("isb");
}
```

---

## 4. 代码风格规范

### 4.1 命名规范

#### 4.1.1 函数命名
```c
/* 格式: <模块>_<对象>_<动作> */
uint32_t scheduler_task_create(void (*entry)(void), uint8_t prio);
void scheduler_task_delete(uint32_t task_id);
void memory_pool_init(uint32_t pool_id);

/* 简短函数可以省略模块名（如果明确） */
uint32_t task_create(void (*entry)(void), uint8_t prio);
```

#### 4.1.2 变量命名
```c
/* 局部变量: 小写 + 下划线 */
uint32_t task_count;
uint64_t system_ticks;
TCB_t *current_task;

/* 全局变量: 加g_前缀 */
uint32_t g_max_tasks;
Scheduler_t g_scheduler;

/* 静态全局变量: 加s_前缀 */
static uint32_t s_initialized = 0U;
static TCB_t *s_idle_task = NULL;

/* 常量: 全大写 + _后缀表示类型 */
#define MAX_TASK_COUNT     256U
#define TICK_RATE_HZ       1000U
#define STACK_SIZE_MIN     4096U

/* 枚举值: 全大写 + 前缀 */
typedef enum 
{
    TASK_READY = 0U,      /* 就绪态：等待CPU调度 */
    TASK_RUNNING,         /* 运行态：正在执行 */
    TASK_BLOCKED,         /* 阻塞态：等待资源（信号量、消息队列） */
    TASK_SLEEPING,        /* 休眠态：延时等待，超时自动唤醒 */
    TASK_SUSPENDED        /* 挂起态：被挂起，需要显式恢复 */
} TaskState_t;
```

#### 4.1.3 类型命名
```c
/* 结构体和联合体: _t后缀 */
typedef struct TaskControlBlock TCB_t;
typedef struct Mutex Mutex_t;
typedef union RegisterValue RegValue_t;

/* 函数指针: _fn或_cb后缀 */
typedef void (*TaskEntry_fn)(void);
typedef uint32_t (*ErrorCallback_fn)(uint32_t error);
```

### 4.2 格式规范

#### 4.2.1 缩进和空格
```c
/* 使用4个空格缩进（不使用Tab） */
void function(void) 
{
    uint32_t x = 10U;

    if (x > 5U) 
    {
        x = x + 1U;
    }
}

/* 运算符两边加空格 */
x = a + b * c;        /* ❌ 错误: *两边没有空格 */
x = a + (b * c);      /* ✅ 正确 */

/* 函数参数: 左括号前不加空格 */
func (arg);           /* ❌ 错误 */
func(arg);            /* ✅ 正确 */

/* 控制语句: 括号前加空格 */
if(condition)         /* ❌ 错误 */
if (condition)        /* ✅ 正确 */
```

#### 4.2.2 大括号规范（Allman风格）
```c
/* Allman风格：左大括号必须换行 */
void function(void)
{                    /* ✅ 正确 - Allman风格 */
    /* code */
}

void function(void) { /* ❌ 错误 - K&R风格 */
    /* code */
}

/* 单语句也必须使用大括号 */
if (condition)
    x = 1;           /* ❌ 错误：缺少大括号 */

if (condition)
{                    /* ✅ 正确：Allman风格 */
    x = 1;
}

/* 控制语句必须使用Allman风格 */
if (condition)
{
    do_something();
}
else
{
    do_other_thing();
}

while (condition)
{
    do_something();
}

for (int i = 0; i < max; i++)
{
    do_something();
}

/* 函数定义必须使用Allman风格 */
void function_name(parameter1, parameter2)
{
    /* 函数体 */
}

/* 结构体定义必须使用Allman风格 */
typedef struct StructureName
{
    uint32_t field1;
    uint32_t field2;
} StructureName_t;
```

#### 4.2.3 无限循环规范
```c
/* 无限循环必须使用 for (;;) 而不是 while (1) 或 while (true) */
for (;;)
{
    /* 无限循环体 */
    do_something();
}

/* ❌ 错误：使用 while (1) */
while (1)
{
    /* 不推荐的做法 */
    do_something();
}

/* ❌ 错误：使用 while (true) */
while (true)
{
    /* 不推荐的做法 */
    do_something();
}

/* ✅ 正确：for (;;) 是标准的无限循环写法 */
/* 理由：
 * 1. for (;;) 是明确表达"无限循环"的惯用写法
 * 2. 避免魔法数字（1）或布尔值（true）
 * 3. 更好的编译器优化
 * 4. MISRA-C:2012 规则 15.1 推荐做法
 */
void idle_task(void)
{
    for (;;)
    {
        /* 等待中断或执行空闲任务 */
        __asm__ volatile("wfe");
    }
}
```

#### 4.2.4 行长度
```c
/* 每行最多120个字符 */
uint32_t result = function_with_very_long_name(argument1, argument2, argument3, argument4);

/* 超过120字符需要换行 */
uint32_t result = function_with_very_long_name(
    argument1,
    argument2,
    argument3,
    argument4
);

/* 函数调用换行对齐 */
uint32_t result = scheduler_task_create(
    task_entry_function,
    priority_value,
    stack_size_bytes,
    task_name_string
);
```

### 4.3 注释规范

#### 4.3.1 文件头注释
```c
/**
 * @file    scheduler.c
 * @brief   任务调度器实现
 * @author  AISafe64 Team
 * @date    2025-01-07
 * @version 1.0
 *
 * @details 本文件实现了256级优先级的多核任务调度器
 *          支持抢占式调度、负载均衡和任务迁移
 *
 * @copyright Copyright (c) 2025 AISafe64 Team
 */
```

#### 4.3.2 函数注释
```c
/**
 * @brief 创建新任务
 *
 * @param entry 任务入口函数指针（不能为NULL）
 * @param priority 任务优先级（0-255，255为最高）
 * @param stack_size 堆栈大小（字节，最小4096）
 * @param name 任务名称（最多16字符）
 *
 * @return 成功返回任务ID，失败返回0
 *
 * @note 必须在调度器启动前调用
 * @warning 任务入口函数不得返回
 *
 * @code
 * uint32_t tid = task_create(my_task, 255, 8192, "MyTask");
 * if (tid != 0U) {
 *     printf("Task created: %u\n", tid);
 * }
 * @endcode
 */
uint32_t task_create(void (*entry)(void),
                    uint8_t priority,
                    uint32_t stack_size,
                    const char *name);
```

#### 4.3.3 代码注释
```c
/* 单行注释: 简短说明 */
uint32_t task_id;  /* 任务唯一标识 */

/* 多行注释: 详细说明 */
/*
 * 256级优先级位图实现：
 * - 使用4个uint64_t表示256位
 * - bitmap[0]: 优先级 0-63
 * - bitmap[1]: 优先级 64-127
 * - bitmap[2]: 优先级 128-191
 * - bitmap[3]: 优先级 192-255
 */
static uint64_t priority_bitmap[4];

/* TODO注释: 标记待完成的工作 */
/* TODO: 实现优先级捐赠算法 */

/* FIXME注释: 标记已知问题 */
/* FIXME: 负载均衡在高负载下效率低 */

/* HACK注释: 标记临时解决方案 */
/* HACK: 临时使用忙等待，后续改为WFE指令 */
```

#### 4.3.4 字符集和符号规范

**禁止使用Emoji和特殊符号**：

在源代码、头文件、文档和注释中，**严格禁止**使用以下字符：
- ❌ Emoji表情符号（如：✅ ❌ ⚠️ 🎯 🛡️ 🚀 📋 📊 等）
- ❌ 特殊Unicode符号（如：→ ← ↑ ↓ ⇒ ⇔ 等）
- ❌ 装饰性符号（如：★ ☆ ♥ ♦ 等）
- ❌ 非ASCII字符（包括中文全角标点：，。！？等）

**正确做法**：
```c
/* ✅ 正确: 使用ASCII字符 */
int result = 0;  /* Operation successful */
if (error != 0) {
    return ERROR_FAILED;  /* Error occurred */
}

/* ✅ 正确: 使用英文和ASCII标点 */
/*
 * High priority task: Priority > 200
 * Low priority task: Priority < 50
 */

/* ❌ 错误: 使用Emoji */
int result = 0;  /* ✅ 成功 */
if (error != 0) {
    return ERROR_FAILED;  /* ❌ 失败 */
}

/* ❌ 错误: 使用中文全角标点 */
int result = 0； /* 成功 */

/* ❌ 错误: 使用特殊Unicode符号 */
int result = 0;  /* OK → 成功 */
```

**文档规范**：
- 使用英文编写注释和文档
- 仅使用ASCII字符（0x00-0x7F）
- 使用标准ASCII标点符号：`, . ! ? : ; ( ) [ ] { } < > / \ | - _`
- 代码注释优先使用英文，必要时可用中文但必须用半角标点

**工具检查**：
```bash
# 检查文件中是否包含非ASCII字符
grep -P '[^\x00-\x7F]' filename.c

# 检查是否包含Emoji（常见Emoji范围）
grep -P '[\x{1F000}-\x{1F9FF}]' filename.c
```

**MISRA-C:2012合规性**：
- 此规则符合MISRA-C:2012关于源字符集的要求
- 确保代码在不同编译器和编辑器中的一致性
- 便于代码审查和版本控制

### 4.4 文件组织规范

#### 4.4.1 头文件结构
```c
/**
 * @file    scheduler.h
 * @brief   任务调度器头文件
 */

#ifndef SCHEDULER_H
#define SCHEDULER_H

/* 1. 包含其他头文件 */
#include "types.h"
#include "task.h"

/* 2. 宏定义 */
#define MAX_PRIORITY      255U
#define MIN_PRIORITY      0U
#define PRIORITY_LEVELS   256U

/* 3. 类型定义 */
typedef struct Scheduler Scheduler_t;

/* 4. 函数声明 */
void scheduler_init(void);
void scheduler_start(void);

/* 5. 内联函数（如果需要） */
static inline uint32_t scheduler_get_cpu_count(void) 
{
    return MAX_CPUS;
}

#endif /* SCHEDULER_H */
```

#### 4.4.2 源文件结构
```c
/**
 * @file    scheduler.c
 * @brief   任务调度器实现
 */

/* 1. 包含头文件 */
#include "scheduler.h"
#include <string.h>

/* 2. 宏定义（仅本文件使用） */
#define SCHEDULER_LOCK_TIMEOUT_US  1000U

/* 3. 类型定义（仅本文件使用） */
typedef struct 
{
    uint32_t count;
    uint64_t time;
} ScheduleStat_t;

/* 4. 全局变量 */
static Scheduler_t s_scheduler;
static bool s_initialized = false;

/* 5. 内部函数声明 */
static void schedule_internal(void);
static uint8_t find_highest_priority(void);

/* 6. 公共函数实现 */
void scheduler_init(void) 
{
    /* 实现代码 */
}

/* 7. 内部函数实现 */
static void schedule_internal(void) 
{
    /* 实现代码 */
}
```

---

## 5. 内存管理规范

### 5.1 动态内存分配

#### 5.1.1 分配和释放
```c
/* ✅ 正确: 检查返回值 */
void *ptr = malloc(100);
if (ptr == NULL) 
{
    return ERROR_OUT_OF_MEMORY;
}
/* 使用ptr... */
free(ptr);
ptr = NULL;  /* 防止悬空指针 */

/* ❌ 错误: 未检查返回值 */
void *ptr = malloc(100);
*ptr = 10;  /* 可能段错误 */

/* ✅ 正确: 使用calloc清零内存 */
TCB_t *task = (TCB_t *)calloc(1, sizeof(TCB_t));
if (task == NULL) 
{
    return ERROR_OUT_OF_MEMORY;
}

/* ❌ 错误: 内存泄漏 */
void function(void) 
{
    void *ptr = malloc(100);
    return;  /* 忘记释放ptr */
}
```

#### 5.1.2 对齐分配
```c
/* 分配对齐的内存 */
void *aligned_alloc(uint32_t size, uint32_t alignment) 
{
    void *ptr = NULL;
    void *aligned_ptr = NULL;

    if ((alignment & (alignment - 1U)) != 0U) {
        return NULL;  /* alignment必须是2的幂 */
    }

    ptr = malloc(size + alignment);
    if (ptr == NULL) {
        return NULL;
    }

    aligned_ptr = (void *)(((uintptr_t)ptr + alignment) & ~(alignment - 1U));

    /* 保存原始指针，以便释放 */
    *((void **)aligned_ptr - 1) = ptr;

    return aligned_ptr;
}

void aligned_free(void *ptr) 
{
    if (ptr != NULL) 
    {
        free(*((void **)ptr - 1));
    }
}
```

### 5.2 栈使用规范

#### 5.2.1 限制栈大小
```c
/* ❌ 错误: 大数组在栈上 */
void function(void) 
{
    uint8_t buffer[10000];  /* 10KB，可能栈溢出 */
}

/* ✅ 正确: 使用静态或动态分配 */
void function(void) 
{
    static uint8_t buffer[10000];  /* 静态分配 */
    /* 或 */
    uint8_t *buffer = malloc(10000);  /* 动态分配 */
    if (buffer != NULL) 
    {
        /* 使用buffer */
        free(buffer);
    }
}
```

#### 5.2.2 栈检查
```c
/* 使用栈水印检测栈使用 */
#define STACK_CANARY 0xABCD1234U

void stack_init(TCB_t *task, void *stack, uint32_t size) 
{
    uint32_t *stack_bottom = (uint32_t *)stack;

    /* 设置栈魔数 */
    for (uint32_t i = 0U; i < (size / sizeof(uint32_t)); i++) 
    {
        stack_bottom[i] = STACK_CANARY;
    }

    task->stack_base = stack;
    task->stack_size = size;
}

bool stack_check(TCB_t *task) 
{
    uint32_t *stack_bottom = (uint32_t *)task->stack_base;
    uint32_t used = 0U;

    /* 计算栈使用量 */
    for (uint32_t i = 0U; i < (task->stack_size / sizeof(uint32_t)); i++) 
    {
        if (stack_bottom[i] != STACK_CANARY) 
        {
            used = (i + 1U) * sizeof(uint32_t);
            break;
        }
    }

    uint32_t usage_percent = (used * 100U) / task->stack_size;

    if (usage_percent > 80U) 
    {
        /* 栈使用超过80%，警告 */
        return false;
    }

    return true;
}
```

### 5.3 堆保护

#### 5.3.1 双重释放检测
```c
typedef struct {
    void *ptr;
    uint32_t magic;
} AllocationInfo_t;

#define ALLOC_MAGIC 0xDEADBEEFU

void safe_free(void **ptr) {
    if ((ptr == NULL) || (*ptr == NULL)) {
        return;
    }

    /* 检查是否已释放 */
    AllocationInfo_t *info = (AllocationInfo_t *)*ptr - 1;
    if (info->magic != ALLOC_MAGIC) {
        /* 已释放或损坏 */
        return;
    }

    /* 清除魔数 */
    info->magic = 0U;

    /* 释放内存 */
    free(info);
    *ptr = NULL;
}

void *safe_malloc(uint32_t size) {
    AllocationInfo_t *info = malloc(sizeof(AllocationInfo_t) + size);
    if (info == NULL) {
        return NULL;
    }

    info->magic = ALLOC_MAGIC;
    info->ptr = (void *)(info + 1);

    return info->ptr;
}
```

---

## 6. 并发和同步规范

### 6.1 锁使用规范

#### 6.1.1 自旋锁
```c
/* ✅ 正确: 使用ticket lock */
TicketLock_t lock = {0};

void critical_section(void) {
    ticket_lock_acquire(&lock);

    /* 临界区代码 */
    protected_variable++;

    ticket_lock_release(&lock);
}

/* ❌ 错误: 忘记释放锁 */
void critical_section(void) {
    ticket_lock_acquire(&lock);

    if (error) {
        return;  /* 忘记释放锁 */
    }

    ticket_lock_release(&lock);
}

/* ✅ 正确: 确保锁释放 */
void critical_section(void) {
    ticket_lock_acquire(&lock);

    if (error) {
        ticket_lock_release(&lock);
        return;
    }

    ticket_lock_release(&lock);
}
```

#### 6.1.2 互斥锁（任务上下文）
```c
/* ✅ 正确: 在任务上下文使用互斥锁 */
Mutex_t mutex;
mutex_init(&mutex);

void task_function(void) {
    mutex_lock(&mutex);

    /* 临界区代码 */

    mutex_unlock(&mutex);
}

/* ❌ 错误: 在中断中使用互斥锁 */
void irq_handler(void) {
    mutex_lock(&mutex);  /* 可能死锁 */
    /* ... */
}
```

#### 6.1.3 锁顺序规范
```c
/* 定义全局锁顺序 */
enum LockOrder {
    LOCK_ORDER_SCHEDULER = 0,
    LOCK_ORDER_MEMORY,
    LOCK_ORDER_SYNC,
    LOCK_ORDER_MAX
};

/* 始终按照相同顺序获取锁 */
void multi_lock_function(void) {
    /* 先获取scheduler锁 */
    ticket_lock_acquire(&scheduler.lock);

    /* 再获取memory锁 */
    ticket_lock_acquire(&memory.lock);

    /* 临界区代码 */

    /* 按相反顺序释放 */
    ticket_lock_release(&memory.lock);
    ticket_lock_release(&scheduler.lock);
}
```

### 6.2 无锁编程

#### 6.2.1 单生产者单消费者（SPSC）队列
```c
typedef struct {
    uint32_t buffer[256];
    uint32_t head;
    uint32_t tail;
} SPSCQueue_t;

void spsc_enqueue(SPSCQueue_t *queue, uint32_t value) {
    uint32_t next_head = (queue->head + 1U) & 0xFFU;

    if (next_head == queue->tail) {
        return;  /* 队列满 */
    }

    queue->buffer[queue->head] = value;
    barrier_store();  /* 确保数据先写入 */
    queue->head = next_head;
}

bool spsc_dequeue(SPSCQueue_t *queue, uint32_t *value) {
    if (queue->tail == queue->head) {
        return false;  /* 队列空 */
    }

    *value = queue->buffer[queue->tail];
    barrier_load();  /* 确保数据先读取 */
    queue->tail = (queue->tail + 1U) & 0xFFU;

    return true;
}
```

#### 6.2.2 无锁栈
```c
typedef struct StackNode {
    uint32_t value;
    struct StackNode *next;
} StackNode_t;

typedef struct {
    StackNode_t *top;
} LockFreeStack_t;

void stack_push(LockFreeStack_t *stack, uint32_t value) {
    StackNode_t *node = malloc(sizeof(StackNode_t));
    if (node == NULL) {
        return;
    }

    node->value = value;

    do {
        node->next = stack->top;
    } while (!atomic_compare_exchange_weak(
        &stack->top,
        &node->next,
        node
    ));
}

bool stack_pop(LockFreeStack_t *stack, uint32_t *value) {
    StackNode_t *old_top;
    StackNode_t *new_top;

    do {
        old_top = stack->top;
        if (old_top == NULL) {
            return false;
        }
        new_top = old_top->next;
    } while (!atomic_compare_exchange_weak(
        &stack->top,
        &old_top,
        new_top
    ));

    *value = old_top->value;
    free(old_top);

    return true;
}
```

### 6.3 死锁预防

#### 6.3.1 超时机制
```c
bool mutex_lock_timeout(Mutex_t *mutex, uint32_t timeout_ms) {
    uint64_t start = get_system_time_ms();

    while (!mutex_try_lock(mutex)) {
        if ((get_system_time_ms() - start) >= timeout_ms) {
            return false;  /* 超时 */
        }
    }

    return true;
}
```

#### 6.3.2 死锁检测
```c
/* 构建资源分配图 */
typedef struct {
    uint8_t wait_graph[256][256];  /* 任务等待矩阵 */
} DeadlockDetector_t;

bool detect_deadlock(void) {
    /* 使用DFS检测环路 */
    for (uint8_t i = 0U; i < 256U; i++) {
        if (dfs_detect_cycle(i)) {
            return true;
        }
    }
    return false;
}
```

---

## 7. 错误处理规范

### 7.1 POSIX错误码约定

**编码约束**: 系统必须使用标准POSIX错误码（定义在`<errno.h>`）进行错误返回，确保与POSIX规范兼容。

**核心POSIX错误码**：
```c
/* POSIX标准错误码（必须包含在src/include/errno.h） */
#define EPERM        1   /* 操作不允许 */
#define ENOENT       2   /* 文件或目录不存在 */
#define ESRCH        3   /* 没有该进程 */
#define EINTR        4   /* 系统调用被中断 */
#define EIO          5   /* I/O错误 */
#define ENXIO        6   /* 没有这个设备或地址 */
#define E2BIG        7   /* 参数列表过长 */
#define ENOEXEC      8   /* 执行格式错误 */
#define EBADF        9   /* 错误的文件描述符 */
#define ECHILD      10   /* 没有子进程 */
#define EAGAIN      11   /* 再试一次 */
#define ENOMEM      12   /* 内存不足 */
#define EACCES      13   /* 权限被拒绝 */
#define EFAULT      14   /* 错误的地址 */
#define ENOTBLK     15   /* 块设备要求 */
#define EBUSY       16   /* 设备或资源忙 */
#define EEXIST      17   /* 文件存在 */
#define EXDEV       18   /* 跨设备链接 */
#define ENODEV      19   /* 没有这个设备 */
#define ENOTDIR     20   /* 不是目录 */
#define EISDIR      21   /* 是目录 */
#define EINVAL      22   /* 无效的参数 */
#define ENFILE      23   /* 文件表溢出 */
#define EMFILE      24   /* 打开文件过多 */
#define ENOTTY      25   /* 不是终端 */
#define ETXTBSY     26   /* 文本文件忙 */
#define EFBIG       27   /* 文件过大 */
#define ENOSPC      28   /* 设备上没有空间 */
#define ESPIPE      29   /* 非法查找 */
#define EROFS       30   /* 只读文件系统 */
#define EMLINK      31   /* 链接过多的文件 */
#define EPIPE       32   /* 破损的管道 */
#define EDOM        33   /* 数学参数超出定义域 */
#define ERANGE      34   /* 结果过大 */
#define EDEADLK     35   /* 资源死锁避免 */
#define ENAMETOOLONG 36  /* 文件名过长 */
#define ENOLCK      37   /* 没有记录锁可用 */
#define ENOSYS      38   /* 功能未实现 */
#define ENOTEMPTY   39   /* 目录非空 */
#define ELOOP       40   /* 符号链接层级过多 */
#define ENOMSG      42   /* 没有指定类型的消息 */
#define EIDRM       43   /* 标识符被删除 */
#define EOVERFLOW   75   /* 值过大 */
#define ETIMEDOUT   116 /* 连接超时 */
```

**错误码使用约定**：

1. **参数验证错误必须使用`EINVAL`**：
   ```c
   /* ✅ 正确：参数无效返回EINVAL */
   if (param == NULL) {
       return -EINVAL;
   }
   
   /* ❌ 错误：不要使用自定义错误码 */
   if (param == NULL) {
       return -ERROR_INVALID_PARAM;  /* 违反POSIX约定 */
   }
   ```

2. **系统调用/内核函数返回负错误码**：
   ```c
   /* 系统调用约定：成功返回0或正数，失败返回负错误码 */
   long sys_read(int fd, void *buf, size_t count) {
       if (buf == NULL) {
           return -EINVAL;  /* 返回负的POSIX错误码 */
       }
       if (fd < 0 || fd >= MAX_OPEN_FILES) {
           return -EBADF;
       }
       /* ... */
       return bytes_read;  /* 成功返回读取字节数 */
   }
   ```

3. **POSIX兼容层设置`errno`并返回-1**：
   ```c
   /* pthread等POSIX API：设置errno并返回-1 */
   int pthread_mutex_lock(pthread_mutex_t *mutex) {
       if (mutex == NULL) {
           return EINVAL;  /* 直接返回错误码 */
       }
       /* ... */
       return 0;  /* 成功返回0 */
   }
   ```

4. **内部内核函数可使用`ErrorCode_t`类型**：
   ```c
   /* 内部内核函数使用自定义错误码 */
   ErrorCode_t page_alloc(uint64_t *addr) {
       if (addr == NULL) {
           return ERROR_INVALID_PARAM;
       }
       /* ... */
       return ERROR_SUCCESS;
   }
   ```

**错误码映射规则**：

| 场景 | 使用错误码 | 返回格式 |
|------|-----------|----------|
| 系统调用参数无效 | `EINVAL` | `-EINVAL` |
| 文件描述符无效 | `EBADF` | `-EBADF` |
| 内存不足 | `ENOMEM` | `-ENOMEM` |
| 权限不足 | `EPERM` | `-EPERM` |
| 功能未实现 | `ENOSYS` | `-ENOSYS` |
| 设备忙 | `EBUSY` | `-EBUSY` |
| POSIX pthread | `EINVAL`等 | 直接返回错误码 |
| 内核函数 | 自定义错误码 | `ErrorCode_t`类型 |

**MISRA-C:2012合规性**：
```c
/* 错误码比较使用显式转换 */
int ret = some_syscall();
if (ret < 0) {
    /* 明确转换避免符号扩展问题 */
    int err = -(int32_t)ret;
    if (err == (int32_t)EINVAL) {
        /* 处理EINVAL错误 */
    }
}
```

### 7.2 错误处理模式

#### 7.2.1 返回值检查
```c
int function(void) {
    int ret;

    ret = sub_function1();
    if (ret != 0) {
        return ret;
    }

    ret = sub_function2();
    if (ret != 0) {
        return ret;
    }

    return 0;
}

/* 调用者必须检查返回值 */
int ret = function();
if (ret != 0) {
    handle_error(ret);
}
```

#### 7.2.2 资源清理模式
```c
int complex_function(void) {
    void *resource1 = NULL;
    void *resource2 = NULL;
    int ret = 0;

    resource1 = malloc(100);
    if (resource1 == NULL) {
        return -ENOMEM;
    }

    resource2 = malloc(200);
    if (resource2 == NULL) {
        free(resource1);
        return -ENOMEM;
    }

    ret = process_resources(resource1, resource2);
    if (ret != 0) {
        goto cleanup;
    }

    /* 更多操作... */

cleanup:
    if (resource2 != NULL) {
        free(resource2);
    }
    if (resource1 != NULL) {
        free(resource1);
    }

    return ret;
}
```

### 7.3 断言和诊断

#### 7.3.1 编译时断言
```c
/* 静态断言（C11） */
_Static_assert(sizeof(uint64_t) == 8U, "uint64_t must be 8 bytes");
_Static_assert((MAX_PRIORITY & (MAX_PRIORITY - 1U)) == 0U,
               "MAX_PRIORITY must be power of 2");

/* 兼容C99的静态断言 */
#define STATIC_ASSERT(expr, msg) \
    typedef char static_assertion_##msg[(expr) ? 1 : -1]

STATIC_ASSERT(sizeof(TCB_t) <= 1024U, TCB_too_large);
```

#### 7.3.2 运行时断言
```c
/* 调试断言 */
#ifdef DEBUG
#define ASSERT(expr) \
    do { \
        if (!(expr)) { \
            assertion_failed(__FILE__, __LINE__, #expr); \
        } \
    } while (0)
#else
#define ASSERT(expr) ((void)0)
#endif

/* 使用示例 */
void task_delete(uint32_t task_id) {
    ASSERT(task_id < MAX_TASKS);
    ASSERT(task_table[task_id] != NULL);

    /* 删除任务... */
}
```

---

## 8. 性能优化规范

### 8.1 内联函数

#### 8.1.1 何时使用inline
```c
/* ✅ 适合内联: 小函数，频繁调用 */
static inline uint32_t get_cpu_id(void) {
    uint64_t mpidr;
    __asm__ volatile("mrs %0, mpidr_el1" : "=r"(mpidr));
    return (uint32_t)(mpidr & 0xFFU);
}

/* ✅ 适合内联: 位操作 */
static inline uint8_t find_highest_priority(uint64_t *bitmap) {
    if (bitmap[0] != 0U) {
        return (uint8_t)__builtin_clzll(bitmap[0]);
    }
    return 0U;
}

/* ❌ 不适合内联: 大函数 */
static inline void complex_function(void) {  /* 不要内联 */
    /* 100行代码... */
}
```

### 8.2 分支预测提示

#### 8.2.1 likely/unlikely宏
```c
/* 分支预测宏定义 */
#if defined(__GNUC__) || defined(__clang__)
#define likely(x)   __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)
#else
#define likely(x)   (x)
#define unlikely(x) (x)
#endif

/* 使用示例 */
uint8_t find_highest_priority(uint64_t *bitmap) {
    if (unlikely(bitmap[0] == 0U)) {  /* 通常不为0 */
        if (unlikely(bitmap[1] == 0U)) {
            /* 继续检查... */
        }
    }
    return priority;
}
```

### 8.3 缓存优化

#### 8.3.1 数据结构布局
```c
/* ✅ 好: 热数据放在一起 */
typedef struct {
    /* 频繁访问的数据 */
    uint8_t  priority;
    uint8_t  state;
    uint16_t flags;

    /* 较少访问的数据 */
    char     name[16];
    uint64_t create_time;
} TCB_t;

/* ❌ 差: 冷热数据混合 */
typedef struct {
    char     name[16];
    uint64_t create_time;
    uint8_t  priority;  /* 频繁访问，但不在缓存行开头 */
    uint8_t  state;
    uint16_t flags;
} TCB_t;
```

#### 8.3.2 缓存行对齐
```c
/* 多核共享数据，避免伪共享 */
typedef struct __attribute__((aligned(64))) {
    atomic_uint32_t lock;
    uint32_t task_count;
    uint8_t padding[64 - sizeof(atomic_uint32_t) - sizeof(uint32_t)];
} PerCPUData_t;
```

---

## 9. 测试规范

### 9.1 单元测试

#### 9.1.1 测试用例结构
```c
/* Unity测试框架示例 */
void test_scheduler_task_create(void) {
    uint32_t task_id;
    ErrorCode_t ret;

    /* 测试: 正常创建 */
    ret = task_create(dummy_task, 100, 4096, "TestTask");
    TEST_ASSERT_EQUAL(ERROR_SUCCESS, ret);
    TEST_ASSERT_NOT_EQUAL(0U, ret);

    /* 测试: 无效参数 */
    task_id = task_create(NULL, 100, 4096, "NullTask");
    TEST_ASSERT_EQUAL(ERROR_INVALID_PARAM, task_id);

    /* 测试: 优先级越界 */
    task_id = task_create(dummy_task, 256, 4096, "BadPrio");
    TEST_ASSERT_EQUAL(ERROR_INVALID_PARAM, task_id);
}
```

#### 9.1.2 Mock外部依赖
```c
/* Mock硬件定时器 */
void mock_timer_init(void) {
    /* 设置初始状态 */
    timer_tick_count = 0U;
}

void mock_timer_tick(void) {
    timer_tick_count++;
}

/* 测试中使用mock */
void test_task_delay(void) {
    mock_timer_init();

    task_delay(10);
    TEST_ASSERT_EQUAL(10U, timer_tick_count);
}
```

### 9.2 覆盖率要求

```c
/* MC/DC覆盖率示例 */
void coverage_example(uint32_t a, uint32_t b, uint32_t c) {
    /* 条件: (a > 5) && (b < 10) || (c == 0) */
    /* 测试用例必须独立改变每个条件 */

    /* 测试用例1: a=6, b=5, c=1  -> true && true || false = true */
    /* 测试用例2: a=4, b=5, c=1  -> false && true || false = false */
    /* 测试用例3: a=6, b=15, c=1 -> true && false || false = false */
    /* 测试用例4: a=6, b=5, c=0  -> true && true || true = true */

    if ((a > 5U) && (b < 10U) || (c == 0U)) {
        result = 1U;
    } else {
        result = 0U;
    }
}
```

---

## 10. 文档要求

### 10.1 代码注释覆盖率
```c
/* 所有公开API必须有文档注释 */
/**
 * @brief 函数简短描述（单行）
 *
 * @details 详细描述（可以多行）
 *          解释函数的用途、算法等
 *
 * @param param1 参数1说明
 * @param param2 参数2说明
 *
 * @return 返回值说明
 *
 * @note 注意事项
 * @warning 警告信息
 * @see 参考其他函数
 */
```

### 10.2 复杂度要求
```c
/* 圈复杂度限制: 每个函数不超过10 */
void complex_function(void) {  /* ❌ 圈复杂度太高 */
    if (condition1) {
        if (condition2) {
            if (condition3) {
                /* ... */
            }
        }
    }
}

/* 重构为多个小函数 */
void complex_function(void) {  /* ✅ 圈复杂度降低 */
    if (condition1) {
        handle_case1();
    } else {
        handle_case2();
    }
}
```

---

## 11. 检查清单

### 11.1 提交前检查
- [ ] 所有文件包含MISRA-C:2012合规的头文件注释
- [ ] 所有函数有文档注释
- [ ] 所有魔术数字替换为宏定义
- [ ] 所有类型转换显式声明
- [ ] 所有数组访问检查边界
- [ ] 所有指针使用前检查NULL
- [ ] 所有错误路径处理
- [ ] 所有锁正确释放
- [ ] 所有动态分配释放
- [ ] 通过静态分析（零警告）

### 11.2 代码审查检查
- [ ] 遵循命名规范
- [ ] 遵循格式规范
- [ ] 无未定义行为
- [ ] 无内存泄漏
- [ ] 无死锁风险
- [ ] 正确使用内存屏障
- [ ] 正确使用原子操作
- [ ] 测试覆盖率>95%

---

## 12. 工具和脚本

### 12.1 静态分析配置
```bash
# PC-lint Plus配置
# lint配置文件: .lnt

# MISRA-C:2012规则集
-misra2

# 包含路径
-I./include
-I./src

# 定义宏
+d__builtin_expect(x,y)=((x))
+d__builtin_clzll(x)=__CLZ_LL(x)

# 抑警告（如需要）
-esym(534, my_function.c)  /* 忽略返回值（已验证） */
```

### 12.2 自动化检查脚本
```python
#!/usr/bin/env python3
# misra_check.py

import subprocess
import sys

def run_lint(file_path):
    """运行PC-lint Plus"""
    cmd = ["lint", "-u", file_path]
    result = subprocess.run(cmd, capture_output=True, text=True)
    return result.returncode, result.stdout, result.stderr

def main():
    if len(sys.argv) < 2:
        print("Usage: misra_check.py <file>")
        return 1

    file_path = sys.argv[1]
    returncode, stdout, stderr = run_lint(file_path)

    if returncode != 0:
        print(f"MISRA violations found in {file_path}:")
        print(stdout)
        print(stderr)
        return 1
    else:
        print(f"No MISRA violations in {file_path}")
        return 0

if __name__ == "__main__":
    sys.exit(main())
```

---

## 13. CMake构建系统规范

### 13.1 CMake文件组织

#### 13.1.1 项目结构要求
```cmake
# CMakeLists.txt文件组织原则:
# 1. 根CMakeLists.txt: 项目整体配置
# 2. 子目录CMakeLists.txt: 模块级配置
# 3. cmake/*.cmake: 可重用的CMake模块
# 4. 工具链文件独立: cmake/toolchain-arm64.cmake
```

#### 13.1.2 命名规范
```cmake
# CMake变量命名
set(PROJECT_NAME TinyOS64)           # 项目: 全大写
set(SOURCE_FILES main.c)              # 局部: 大写下划线
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS}") # CMake内置: 保留原始

# 目标命名
add_executable(tinyos64_core ${SRC})  # 可执行文件: 小写下划线
add_library(kernel STATIC ${SRC})      # 库: 小写下划线
add_custom_target(misra_check ...)    # 自定义目标: 小写下划线

# 宏和函数命名
macro(add_kernel_module name)          # 宏: 小写下划线
function(compile_config_file)          # 函数: 小写下划线
endfunction()
endmacro()
```

#### 13.1.3 最小CMake版本
```cmake
# 必须声明最小CMake版本
cmake_minimum_required(VERSION 3.20)
project(TinyOS64 C ASM)

# 设置C标准
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)  # 禁止编译器扩展，确保标准合规
```

### 13.2 编译选项规范

#### 13.2.1 安全相关编译选项
```cmake
# MISRA-C:2012合规的编译选项
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(
        -Wall                   # 启用所有警告
        -Wextra                 # 启用额外警告
        -Werror                 # 将警告视为错误
        -Wpedantic              # 严格遵循标准
        -Wconversion            # 隐式转换警告
        -Wsign-conversion       # 符号转换警告
        -Wshadow                # 变量遮蔽警告
        -Wstrict-prototypes     # 严格原型检查
        -Wmissing-prototypes    # 缺失原型警告
        -Wstrict-overflow=1     # 严格溢出检查
        -Wvla                   # 禁止变长数组警告
        -Wpedantic              # ISO C合规
    )
endif()

# 嵌入式系统特定选项
add_compile_options(
    -ffreestanding          # 自由standing环境（无标准库）
    -fno-builtin            # 禁用内置函数
    -fno-common             # 禁止未初始化全局变量合并
    -fdata-sections         # 分离数据段
    -ffunction-sections     # 分离代码段
    -fno-strict-aliasing    # 禁止严格别名（避免未定义行为）
)
```

#### 13.2.2 调试/发布配置
```cmake
# Debug配置: 无优化，包含调试信息
set(CMAKE_C_FLAGS_DEBUG "-O0 -g3")

# Release配置: 优化，包含调试符号
set(CMAKE_C_FLAGS_RELEASE "-O2 -g1")

# RelWithDebInfo: 优化并保留调试信息
set(CMAKE_C_FLAGS_RELWITHDEBINFO "-O2 -g3")

# MinSizeRel: 最小体积优化
set(CMAKE_C_FLAGS_MINSIZEREL "-Os -g1")

# 设置默认构建类型
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()
```

### 13.3 链接选项规范

#### 13.3.1 链接器脚本
```cmake
# 指定链接器脚本
set(LINKER_SCRIPT ${CMAKE_SOURCE_DIR}/lds/linker.ld)

# 链接选项
set(CMAKE_EXE_LINKER_FLAGS
    "${CMAKE_EXE_LINKER_FLAGS}
    -nostartfiles           # 不使用标准启动文件
    -nostdlib               # 不链接标准库
    -T ${LINKER_SCRIPT}     # 使用自定义链接器脚本
    -Wl,--gc-sections       # 删除未使用的段
    -Wl,-Map=$<TARGET>.map # 生成内存映射文件
    "
)
```

#### 13.3.2 链接库顺序
```cmake
# 链接库顺序: 依赖者在前，被依赖者在后
target_link_libraries(tinyos64_kernel
    kernel                      # 内核核心
    hal                         # 硬件抽象层
    lib                         # 工具库
    -lm                         # 数学库（最后）
)

# 不要链接标准C库
# 嵌入式系统通常不使用标准库
```

### 13.4 目标定义规范

#### 13.4.1 静态库目标
```cmake
# 定义静态库
add_library(kernel STATIC
    scheduler.c
    task.c
    smp.c
    mmu.c
    sync.c
    timer.c
)

# 设置库属性
set_target_properties(kernel PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
    POSITION_INDEPENDENT_CODE OFF
)

# 添加包含目录
target_include_directories(kernel
    PUBLIC
        ${CMAKE_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src/kernel
)
```

#### 13.4.2 可执行文件目标
```cmake
# 定义可执行文件
add_executable(tinyos.elf
    startup.S
    main.c
)

# 链接库
target_link_libraries(tinyos.elf
    PRIVATE kernel hal lib
)

# 设置输出属性
set_target_properties(tinyos.elf PROPERTIES
    OUTPUT_NAME "tinyos"
    SUFFIX ".elf"
)

# 生成二进制文件
add_custom_command(TARGET tinyos.elf POST_BUILD
    COMMAND ${CMAKE_OBJCOPY}
        -O binary
        $<TARGET_FILE:tinyos.elf>
        ${CMAKE_BINARY_DIR}/tinyos.bin
    COMMENT "Generating binary file..."
)

# 生成反汇编文件
add_custom_command(TARGET tinyos.elf POST_BUILD
    COMMAND ${CMAKE_OBJDUMP}
        -d -S
        $<TARGET_FILE:tinyos.elf>
        > ${CMAKE_BINARY_DIR}/tinyos.dis
    COMMENT "Generating disassembly..."
)
```

### 13.5 交叉编译配置

#### 13.5.1 工具链文件
```cmake
# cmake/toolchain-arm64.cmake
# 目标系统
set(CMAKE_SYSTEM_NAME Generic)          # 通用嵌入式系统
set(CMAKE_SYSTEM_PROCESSOR aarch64)     # ARM64架构

# 交叉编译工具链
set(CMAKE_C_COMPILER aarch64-none-elf-gcc)
set(CMAKE_ASM_COMPILER aarch64-none-elf-gcc)
set(CMAKE_AR aarch64-none-elf-ar)
set(CMAKE_RANLIB aarch64-none-elf-ranlib)
set(CMAKE_OBJCOPY aarch64-none-elf-objcopy)
set(CMAKE_OBJDUMP aarch64-none-elf-objdump)
set(CMAKE_SIZE aarch64-none-elf-size)

# 设置查找路径行为
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
```

#### 13.5.2 使用工具链文件
```bash
# 命令行使用
cmake .. -DCMAKE_TOOLCHAIN_FILE=../cmake/toolchain-arm64.cmake

# 或设置环境变量
export CMAKE_TOOLCHAIN_FILE=../cmake/toolchain-arm64.cmake
cmake ..
```

### 13.6 测试集成

#### 13.1.1 启用测试
```cmake
# 启用测试
enable_testing()

# 添加测试
add_executable(test_scheduler tests/test_scheduler.c)
target_link_libraries(test_scheduler kernel unity)

# 注册测试
add_test(NAME scheduler_test COMMAND test_scheduler)
```

#### 13.6.2 覆盖率收集
```cmake
# 启用覆盖率（仅Debug构建）
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_options(
        -fprofile-arcs
        -ftest-coverage
    )
    add_link_options(
        -fprofile-arcs
        -ftest-coverage
    )

    # 添加覆盖率目标
    add_custom_target(coverage
        COMMAND lcov --capture --directory . --output-file coverage.info
        COMMAND lcov --remove coverage.info '/usr/*' --output-file coverage.info
        COMMAND genhtml coverage.info --output-directory coverage_html
        COMMENT "Generating code coverage report..."
    )
endif()
```

---

## 14. MenuConfig配置系统规范

### 14.1 Kconfig语法规范

#### 14.1.1 配置项类型
```kconfig
# Kconfig文件结构

# 1. bool类型: 布尔开关（y/n）
config ENABLE_MMU
    bool "Enable MMU Support"
    default y
    help
      Enable Memory Management Unit support for virtual memory.

# 2. tristate类型: 三态（y/m/n）
config KERNEL_MODULE
    tristate "Kernel Module Support"
    depends on MODULES
    default m

# 3. string类型: 字符串
config BOARD_NAME
    string "Board Name"
    default "rpi4"

# 4. hex类型: 十六进制数
config KERNEL_BASE_ADDR
    hex "Kernel Base Address"
    default 0xFFFF00000000

# 5. int类型: 整数
config MAX_TASKS
    int "Maximum Number of Tasks"
    range 1 256
    default 32

# 6. choice类型: 单选菜单
choice
    prompt "CPU Core Count"

config CPU_1_CORE
    bool "1 Core"

config CPU_2_CORES
    bool "2 Cores"

config CPU_4_CORES
    bool "4 Cores"

config CPU_8_CORES
    bool "8 Cores"

endchoice

# 使用choice选择
config NR_CPUS
    int
    default 1 if CPU_1_CORE
    default 2 if CPU_2_CORES
    default 4 if CPU_4_CORES
    default 8 if CPU_8_CORES
```

#### 14.1.2 依赖关系
```kconfig
# depends on: 依赖项
config SMP_SUPPORT
    bool "SMP Support"
    depends on ARCH_ARM64

# if ... endif: 条件块
config PRIORITY_LEVELS
    int "Priority Levels"
    default 32
    if HIGH_PRIORITY_SUPPORT
        default 256
    endif

# select: 自动选择
config ENABLE_SCHEDULER
    bool "Enable Scheduler"
    select TICK_SUPPORT
    select CONTEXT_SWITCH

# imply: 建议选择
config DEBUG_SUPPORT
    bool "Debug Support"
    imply LOGGING
```

#### 14.1.3 菜单结构
```kconfig
# main menu
mainmenu "TinyOS-64 Configuration"

# 菜单组
menu "Core Configuration"

config CPU_CORES
    int "CPU Core Count"
    range 1 8
    default 4

config MAX_TASKS
    int "Maximum Tasks"
    range 1 256
    default 32

endmenu

# 子菜单
menu "Memory Configuration"

source "kconfig/mem/Kconfig"
source "kconfig/mmu/Kconfig"

endmenu
```

### 14.2 配置文件格式

#### 14.2.1 .config文件
```bash
# .config文件由menuconfig自动生成
#
# 格式: CONFIG_<name>=<value>
#
# 符号:
#   y: 内置（编译进内核）
#   m: 模块
#   n: 未选择
#   字符串/数字: 直接值

# 自动生成的注释（不要手动编辑）
# 自动生成的注释

# 核心配置
CONFIG_CPU_CORES=4
CONFIG_MAX_TASKS=32
CONFIG_ENABLE_MMU=y
CONFIG_ENABLE_SMP=y

# 内存配置
CONFIG_KERNEL_HEAP_SIZE=1048576
CONFIG_TASK_STACK_SIZE=8192

# 调试配置
# CONFIG_DEBUG is not set
CONFIG_LOG_LEVEL=2
```

#### 14.2.2 defconfig文件
```bash
# configs/defconfig: 默认配置模板
# 只包含非默认值的配置项

CONFIG_CPU_CORES=4
CONFIG_MAX_TASKS=32
CONFIG_ENABLE_MMU=y
CONFIG_ENABLE_SMP=y
CONFIG_KERNEL_HEAP_SIZE=1048576
CONFIG_TASK_STACK_SIZE=8192
CONFIG_LOG_LEVEL=2
```

### 14.3 配置头文件生成

#### 14.3.1 config.h格式
```c
/**
 * @file config.h
 * @brief Auto-generated configuration header
 * @warning DO NOT EDIT - Generated by menuconfig
 */

#ifndef CONFIG_H
#define CONFIG_H

/**
 * @brief 配置验证宏
 */
#define CONFIG_TINYOS_VERSION_MAJOR 1
#define CONFIG_TINYOS_VERSION_MINOR 0

/**
 * @brief 核心配置
 */
#define CONFIG_CPU_CORES               4U
#define CONFIG_MAX_TASKS               32U
#define CONFIG_ENABLE_MMU              1
#define CONFIG_ENABLE_SMP              1

/**
 * @brief 优先级配置
 */
#define CONFIG_PRIORITY_LEVELS         256U
#define CONFIG_MIN_PRIORITY            0U
#define CONFIG_MAX_PRIORITY            255U

/**
 * @brief 内存配置
 */
#define CONFIG_KERNEL_HEAP_SIZE        1048576UL  /* 1MB */
#define CONFIG_TASK_STACK_SIZE         8192U      /* 8KB */
#define CONFIG_MMU_PAGE_SIZE           4096U      /* 4KB */

/**
 * @brief 调试配置
 */
#define CONFIG_LOG_LEVEL               2U         /* 0=off, 1=err, 2=warn, 3=info, 4=debug */
#define CONFIG_ASSERT_LEVEL            2U         /* 0=off, 1=error, 2=full */

/**
 * @brief 配置验证
 */
#if CONFIG_MAX_TASKS > 256
#error "CONFIG_MAX_TASKS cannot exceed 256"
#endif

#if CONFIG_PRIORITY_LEVELS != 256
#error "CONFIG_PRIORITY_LEVELS must be 256"
#endif

#endif /* CONFIG_H */
```

#### 14.3.2 配置解析脚本
```python
#!/usr/bin/env python3
# scripts/parse_config.py
import sys
import re

def parse_config(config_file, output_file):
    """Parse .config file and generate config.h"""

    configs = {}
    with open(config_file, 'r') as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith('#'):
                continue

            # 解析 CONFIG_XXX=value
            match = re.match(r'CONFIG_([^=]+)=(.+)', line)
            if match:
                name = match.group(1)
                value = match.group(2)

                # 转换值类型
                if value == 'y':
                    value = '1'
                elif value == 'n':
                    value = '0'
                elif value == 'm':
                    value = '2'

                configs[name] = value

    # 生成头文件
    with open(output_file, 'w') as f:
        f.write("/** @file auto-generated config.h */\n")
        f.write("#ifndef CONFIG_H\n")
        f.write("#define CONFIG_H\n\n")

        for name, value in sorted(configs.items()):
            # 数字类型
            if re.match(r'^\d+$', value):
                f.write(f"#define CONFIG_{name} {value}U\n")
            # 字符串类型
            elif value.startswith('"'):
                f.write(f"#define CONFIG_{name} {value}\n")
            # 十六进制
            elif value.startswith('0x'):
                f.write(f"#define CONFIG_{name} {value}UL\n")

        f.write("\n#endif /* CONFIG_H */\n")

if __name__ == '__main__':
    if len(sys.argv) != 3:
        print("Usage: parse_config.py <.config> <config.h>")
        sys.exit(1)

    parse_config(sys.argv[1], sys.argv[2])
```

### 14.4 代码中使用配置

#### 14.4.1 条件编译
```c
/* 使用CONFIG_宏进行条件编译 */
#include <generated/config.h>

void scheduler_init(void) {
#if CONFIG_ENABLE_SMP
    /* 多核初始化 */
    smp_init();
#endif

#if CONFIG_ENABLE_MMU
    /* MMU初始化 */
    mmu_init();
#endif

#if CONFIG_LOG_LEVEL >= 3
    log_info("Scheduler initialized\n");
#endif
}

/* 使用配置限制数组大小 */
TCB_t task_table[CONFIG_MAX_TASKS];
uint64_t ready_queue[CONFIG_CPU_CORES][CONFIG_PRIORITY_LEVELS];
```

#### 14.4.2 编译时断言
```c
/* 使用STATIC_ASSERT验证配置 */
#include <generated/config.h>

STATIC_ASSERT(CONFIG_MAX_TASKS <= 256, max_tasks_exceeded);
STATIC_ASSERT(CONFIG_PRIORITY_LEVELS == 256, priority_levels_fixed);
STATIC_ASSERT((CONFIG_KERNEL_HEAP_SIZE % 4096) == 0, heap_not_aligned);
```

---

## 15. 持续集成规范

### 15.1 CI检查清单

#### 15.1.1 提交前检查
```bash
#!/bin/bash
# scripts/check_patch.sh

set -e

echo "=== TinyOS-64 Pre-commit Check ==="

# 1. 格式检查
echo "Checking code format..."
./scripts/check_format.sh

# 2. MISRA检查
echo "Running MISRA-C:2012 checks..."
cmake --build build --target misra-check

# 3. 单元测试
echo "Running unit tests..."
cd build
ctest --output-on-failure
cd ..

# 4. 覆盖率检查
echo "Checking code coverage..."
./scripts/check_coverage.sh 95

echo "=== All checks passed ==="
```

#### 15.1.2 CI配置
```yaml
# .gitlab-ci.yml
stages:
  - build
  - test
  - analyze

variables:
  BUILD_DIR: build

build:arm64:
  stage: build
  script:
    - mkdir -p $BUILD_DIR
    - cd $BUILD_DIR
    - cmake .. -DCMAKE_TOOLCHAIN_FILE=../cmake/toolchain-arm64.cmake
    - make -j$(nproc)
  artifacts:
    paths:
      - $BUILD_DIR/
    expire_in: 1 day

test:unit:
  stage: test
  dependencies:
    - build:arm64
  script:
    - cd $BUILD_DIR
    - ctest --output-on-failure
  coverage: '/lines\.*: (\d+\.\d+)%/'

analyze:misra:
  stage: analyze
  dependencies:
    - build:arm64
  script:
    - cd $BUILD_DIR
    - make misra-check
  allow_failure: false
```

### 15.2 代码审查清单

#### 15.2.1 CMake配置审查
- [ ] CMake最低版本 >= 3.20
- [ ] C标准设置为C11，禁用扩展
- [ ] 所有警告启用（-Wall -Wextra -Wpedantic）
- [ ] 警告视为错误（-Werror）
- [ ] MISRA合规编译选项
- [ ] 目标属性正确设置
- [ ] 链接顺序正确
- [ ] 无硬编码路径

#### 15.2.2 MenuConfig配置审查
- [ ] 配置项有明确帮助文本
- [ ] 配置项有合理默认值
- [ ] 依赖关系正确（depends on/select）
- [ ] choice选择完整且互斥
- [ ] 范围限制合理
- [ ] 配置项命名一致
- [ ] 配置生成脚本正确

---

## 15.3 Git 提交规范（Conventional Commits）

### 15.3.1 提交消息格式

AISafe64 项目严格遵循 **Conventional Commits** 规范，以确保提交历史的清晰和可追溯性。

#### 提交消息结构

```
<type>(<scope>): <subject>

<body>

<footer>
```

#### 提交类型（type）

| 类型 | 描述 | 示例 |
|------|------|------|
| `feat` | 新功能 | feat(scheduler): add EDF scheduling algorithm |
| `fix` | Bug 修复 | fix(mm): resolve page table corruption issue |
| `docs` | 文档更新 | docs(readme): update build instructions |
| `style` | 代码格式（不影响功能） | style(kernel): fix indentation in scheduler.c |
| `refactor` | 重构（既不是新功能也不是修复） | refactor(ipc): simplify message queue implementation |
| `perf` | 性能优化 | perf(scheduler): optimize priority lookup with CLZ |
| `test` | 测试相关 | test(mm): add unit tests for page allocator |
| `chore` | 构建/工具链相关 | chore(cmake): update toolchain requirements |
| `ci` | CI/CD 配置 | ci(gitlab): add MISRA check pipeline |
| `revert` | 回滚之前的提交 | revert: fix(mm): resolve page table corruption |

#### 提交作用域（scope）

作用域用于指定提交影响的模块：

| 模块 | 说明 |
|------|------|
| `kernel` | 内核核心 |
| `scheduler` | 调度器 |
| `mm` | 内存管理 |
| `ipc` | 进程间通信 |
| `fs` | 文件系统 |
| `driver` | 设备驱动 |
| `arch` | 架构相关代码 |
| `crypto` | 加密/签名 |
| `build` | 构建系统 |
| `config` | 配置系统 |

#### 主题（subject）

- 使用动词原形开头（如 add、fix、update）
- 首字母小写
- 不以句号结尾
- 限制在 50 个字符以内

**示例：**
```
✅ good: feat(scheduler): add EDF scheduling support
❌ bad: Added EDF scheduling support.
❌ bad: feat(scheduler): Added EDF scheduling support.
```

#### 正文（body）

- 详细说明本次提交的**内容**和**原因**
- 每行限制在 72 个字符以内
- **必须**说明"是什么"和"为什么"

**示例：**
```
feat(scheduler): add EDF scheduling support

- Implement earliest deadline first algorithm
- Add red-black tree for deadline tracking
- Integrate with existing scheduler class framework
- Update configuration to select between FIFO/EDF/CFS

This allows dynamic priority scheduling for real-time tasks
with periodic deadlines, improving schedulability compared
to static priority FIFO.

Performance: O(log n) enqueue/dequeue operations
```

#### 页脚（footer）

- 关联 Issue：`Closes #123`, `Fixes #456`
- 破坏性变更：`BREAKING CHANGE: <description>`
- 引用相关提交：`Co-Authored-By: <name> <email>`

**示例：**
```
feat(api): remove deprecated task_create interface

BREAKING CHANGE: The old task_create() interface has been removed.
Migrate to task_create_ex() which supports additional parameters.

Closes #789

Co-Authored-By: Claude Sonnet 4.5 <noreply@anthropic.com>
```

### 15.3.2 提交消息示例

#### 示例 1：新功能
```
feat(mm): add transparent huge page support

- Implement 2MB page allocation
- Add automatic huge page promotion
- Update page fault handler to support mixed page sizes
- Add sysfs interface for statistics

This reduces TLB pressure and improves performance for
large memory allocations by up to 30%.

Performance: 2MB page allocation takes <1ms
```

#### 示例 2：Bug 修复
```
fix(scheduler): resolve race condition in task migration

The task migration code had a race condition where a task
could be migrated while being scheduled on another CPU,
leading to a use-after-free.

Fix: Add RCU read-side lock around migration check
and update scheduler to handle migrating tasks correctly.

Reported-by: John Doe <john@example.com>
Tested-by: Jane Smith <jane@example.com>
Fixes #1234
```

#### 示例 3：文档更新
```
docs(CLARUDE.md): add Git commit convention rules

- Document Conventional Commits specification
- Add commit type definitions and examples
- Include scope guidelines and best practices

This ensures consistent commit messages across the project.
```

### 15.3.3 提交最佳实践

#### DO（推荐做法）

```bash
# 1. 每个提交做一件事
git commit -m "feat(scheduler): add EDF algorithm"
git commit -m "test(scheduler): add EDF unit tests"

# 2. 使用完整的句子解释
git commit -m "fix(mm): resolve memory leak in page allocator

The page allocator was not freeing pages on error path,
causing a memory leak of 4KB per failed allocation.

Fix: Add proper cleanup in error handling path."

# 3. 引用相关 Issue
git commit -m "feat(driver): add GPIO driver

Implements basic GPIO operations for Raspberry Pi 4.

Closes #456"
```

#### DON'T（不推荐做法）

```bash
# ❌ 1. 不要混合多个不相关的修改
git commit -m "update various things"

# ❌ 2. 不要使用模糊的描述
git commit -m "fix stuff"
git commit -m "update code"

# ❌ 3. 不要在提交消息中包含敏感信息
git commit -m "add password hardcoding: admin123"

# ❌ 4. 不要使用过长的主题行
git commit -m "feat(scheduler): implement a very complex scheduling algorithm \
that does many things and has a very long description that exceeds fifty characters"
```

### 15.3.4 提交检查清单

在执行 `git commit` 前检查：

- [ ] 提交类型符合 Conventional Commits 规范
- [ ] 作用域（scope）明确指定
- [ ] 主题行不超过 50 个字符
- [ ] 主题行以动词原形开头，首字母小写
- [ ] 主题行不以句号结尾
- [ ] 正文解释了"是什么"和"为什么"
- [ ] 正文每行不超过 72 个字符
- [ ] 关联了相关 Issue（如果存在）
- [ ] 标记了破坏性变更（如果有）
- [ ] 没有包含敏感信息

### 15.3.5 Git 配置

#### 自动化提交消息检查

安装 commitlint 工具：

```bash
npm install -g @commitlint/cli @commitlint/config-conventional
```

配置文件 `.commitlintrc.yml`：

```yaml
extends:
  - '@commitlint/config-conventional'

rules:
  type-enum:
    - feat
    - fix
    - docs
    - style
    - refactor
    - perf
    - test
    - chore
    - ci
    - revert

  scope-enum:
    - kernel
    - scheduler
    - mm
    - ipc
    - fs
    - driver
    - arch
    - crypto
    - build
    - config

  subject-case:
    - lower-case

  body-max-line-length: 72
```

#### Git Hooks 配置

`.git/hooks/commit-msg`:

```bash
#!/bin/bash
commitlint --edit "$1"
```

### 15.3.6 提交工作流

#### 功能开发工作流

```bash
# 1. 创建特性分支
git checkout -b feature/edf-scheduler

# 2. 开发并提交（遵循 Conventional Commits）
git add src/scheduler/edf.c
git commit -m "feat(scheduler): add EDF scheduling algorithm"

# 3. 更多提交
git add tests/test_edf.c
git commit -m "test(scheduler): add EDF unit tests"

# 4. 推送到远程
git push origin feature/edf-scheduler

# 5. 创建 Pull Request
# GitHub 会自动检测提交类型
```

#### Bug 修复工作流

```bash
# 1. 创建修复分支
git checkout -b fix/mm-page-leak

# 2. 修复并提交
git add src/mm/page_alloc.c
git commit -m "fix(mm): resolve memory leak in page allocator

The page allocator was not freeing pages on error path.

Fix: Add proper cleanup in error handling path.

Fixes #1234"

# 3. 推送并创建 PR
git push origin fix/mm-page-leak
```

### 15.3.7 版本号规范

遵循语义化版本（Semantic Versioning）：`MAJOR.MINOR.PATCH`

- **MAJOR**：不兼容的 API 变更
- **MINOR**：向后兼容的新功能
- **PATCH**：向后兼容的 Bug 修复

示例：
- `1.0.0` → `1.1.0`：添加新功能（MINOR）
- `1.1.0` → `1.1.1`：Bug 修复（PATCH）
- `1.1.1` → `2.0.0`：破坏性变更（MAJOR）

---

