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

