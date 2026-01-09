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

