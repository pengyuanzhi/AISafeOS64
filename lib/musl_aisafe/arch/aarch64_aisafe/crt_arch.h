/**
 * @file    crt_arch.h
 * @brief   AISafeOS64 musl 适配层 — 启动代码
 *
 * 简化版启动代码，不依赖 Linux 动态链接器。
 * AISafeOS64 用户态程序由内核直接加载，不需要 _DYNAMIC 解析。
 */

/* AISafeOS64 使用静态链接，不需要动态链接器支持 */
/* START 符号由链接脚本定义 */

/* 简化的启动入口：设置栈帧并跳转到 C 入口 */
__asm__(
".text \n"
".global " START "\n"
".type " START ",%function\n"
START ":\n"
"	mov x29, #0\n"
"	mov x30, #0\n"
"	mov x0, sp\n"
"	and sp, x0, #-16\n"
"	b " START "_c\n"
);
