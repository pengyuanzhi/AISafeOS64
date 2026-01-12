#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
测试 Claude Code Hooks

用法:
    python test_hooks.py
"""

import sys
import json
import subprocess
from pathlib import Path

# 项目根目录
PROJECT_ROOT = Path(__file__).parent.parent.parent
HOOKS_DIR = PROJECT_ROOT / ".claude" / "hooks"


def test_hook(hook_name, test_data):
    """测试单个 Hook"""
    hook_script = HOOKS_DIR / hook_name

    if not hook_script.exists():
        print(f"[SKIP] {hook_name} 不存在")
        return False

    print(f"\n[TEST] 测试 {hook_name}...")

    try:
        # 运行 hook 脚本
        result = subprocess.run(
            [sys.executable, str(hook_script)],
            input=json.dumps(test_data),
            capture_output=True,
            text=True,
            timeout=5
        )

        if result.returncode == 0:
            print(f"[OK] {hook_name} 执行成功")
            if result.stdout:
                print(f"  输出: {result.stdout[:100]}")
            return True
        else:
            print(f"[FAIL] {hook_name} 返回错误码 {result.returncode}")
            if result.stderr:
                print(f"  错误: {result.stderr}")
            return False

    except subprocess.TimeoutExpired:
        print(f"[FAIL] {hook_name} 超时")
        return False
    except Exception as e:
        print(f"[FAIL] {hook_name} 执行失败: {e}")
        return False


def main():
    """主函数"""
    print("=" * 60)
    print("Claude Code Hooks 测试")
    print("=" * 60)

    # 测试数据
    tests = [
        (
            "session_start.py",
            {
                "sessionId": "test-session-123",
                "model": "claude-sonnet-4.5",
                "isResume": False,
                "workingDirectory": str(PROJECT_ROOT)
            }
        ),
        (
            "user_prompt_submit.py",
            {
                "sessionId": "test-session-123",
                "prompt": "如何实现信号量？",
                "model": "claude-sonnet-4.5",
                "workingDirectory": str(PROJECT_ROOT),
                "contextFiles": [
                    {"path": "src/kernel/sync/semaphore.c", "lines": "1-100"},
                    {"path": "include/kernel/sync.h"}
                ]
            }
        ),
        (
            "stop.py",
            {
                "sessionId": "test-session-123",
                "response": "信号量实现需要考虑原子操作...",
                "toolCalls": [
                    {"name": "Read", "input": {"file_path": "semaphore.c"}},
                    {"name": "Edit", "input": {"file_path": "semaphore.c"}}
                ]
            }
        ),
        (
            "session_end.py",
            {
                "sessionId": "test-session-123",
                "durationSeconds": 300,
                "promptCount": 5,
                "toolUseCount": 10
            }
        )
    ]

    # 运行所有测试
    passed = 0
    failed = 0

    for hook_name, test_data in tests:
        if test_hook(hook_name, test_data):
            passed += 1
        else:
            failed += 1

    # 输出总结
    print("\n" + "=" * 60)
    print(f"测试总结: {passed} 通过, {failed} 失败")
    print("=" * 60)

    if failed == 0:
        print("[OK] 所有测试通过！")
        return 0
    else:
        print("[WARN] 部分测试失败，请检查脚本")
        return 1


if __name__ == '__main__':
    sys.exit(main())
