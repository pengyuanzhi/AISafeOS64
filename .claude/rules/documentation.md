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

