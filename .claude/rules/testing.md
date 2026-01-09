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

