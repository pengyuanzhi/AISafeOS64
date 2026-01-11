#!/usr/bin/env python3
"""
Conventional Commits 提交消息验证和生成工具

遵循 Conventional Commits 规范：
https://www.conventionalcommits.org/

使用方法：
    验证提交消息：python scripts/conventional_commit.py validate <commit-file>
    生成提交消息：python scripts/conventional_commit.py generate
"""

import sys
import re
import os
import io
from typing import Tuple, Optional, List

# 设置标准输出编码为 UTF-8（Windows 兼容）
if sys.platform == 'win32':
    sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding='utf-8', errors='replace')
    sys.stderr = io.TextIOWrapper(sys.stderr.buffer, encoding='utf-8', errors='replace')

# ANSI 颜色代码
class Colors:
    RED = '\033[91m'
    GREEN = '\033[92m'
    YELLOW = '\033[93m'
    BLUE = '\033[94m'
    MAGENTA = '\033[95m'
    CYAN = '\033[96m'
    WHITE = '\033[97m'
    RESET = '\033[0m'
    BOLD = '\033[1m'

# 提交类型定义
COMMIT_TYPES = {
    'feat': '新功能',
    'fix': 'Bug修复',
    'docs': '文档更新',
    'style': '代码格式（不影响功能）',
    'refactor': '重构（既不是新功能也不是修复）',
    'perf': '性能优化',
    'test': '测试相关',
    'chore': '构建/工具链相关',
    'ci': 'CI/CD配置',
    'revert': '回滚之前的提交'
}

# 提交作用域
COMMIT_SCOPES = [
    'kernel',     # 内核核心
    'scheduler',  # 调度器
    'mm',         # 内存管理
    'ipc',        # 进程间通信
    'fs',         # 文件系统
    'driver',     # 设备驱动
    'arch',       # 架构相关代码
    'crypto',     # 加密/签名
    'build',      # 构建系统
    'config'      # 配置系统
]

# Conventional Commits 正则表达式
COMMIT_PATTERN = re.compile(
    r'^(?P<type>\w+)'              # 类型
    r'(?:\((?P<scope>[\w-]+)\))?'  # 可选作用域
    r'(?P<breaking>!)?:\s+'        # 可选的破坏性变更标记
    r'(?P<subject>.+)$'            # 主题
)

BODY_PATTERN = re.compile(r'^(?P<text>(?:(?!^#).)+)', re.MULTILINE)
ISSUE_PATTERN = re.compile(r'(?:close|closes|closed|fix|fixes|fixed|resolve|resolves|resolved)\s+#?\d+', re.IGNORECASE)


def print_error(message: str) -> None:
    """打印错误消息"""
    print(f"{Colors.RED}错误: {message}{Colors.RESET}")


def print_success(message: str) -> None:
    """打印成功消息"""
    print(f"{Colors.GREEN}{message}{Colors.RESET}")


def print_warning(message: str) -> None:
    """打印警告消息"""
    print(f"{Colors.YELLOW}警告: {message}{Colors.RESET}")


def print_info(message: str) -> None:
    """打印信息消息"""
    print(f"{Colors.CYAN}{message}{Colors.RESET}")


def print_header(message: str) -> None:
    """打印标题"""
    print(f"\n{Colors.BOLD}{Colors.BLUE}{'=' * 70}{Colors.RESET}")
    print(f"{Colors.BOLD}{Colors.BLUE}{message:^70}{Colors.RESET}")
    print(f"{Colors.BOLD}{Colors.BLUE}{'=' * 70}{Colors.RESET}\n")


def validate_commit_message(message: str) -> Tuple[bool, List[str]]:
    """
    验证提交消息是否符合 Conventional Commits 规范

    Args:
        message: 提交消息内容

    Returns:
        (是否有效, 错误消息列表)
    """
    errors = []
    lines = message.strip().split('\n')

    if not lines or not lines[0].strip():
        errors.append("提交消息不能为空")
        return False, errors

    first_line = lines[0].strip()

    # 解析第一行
    match = COMMIT_PATTERN.match(first_line)
    if not match:
        errors.append(
            f"提交消息格式无效。\n"
            f"期望格式: <type>(<scope>): <subject>\n"
            f"示例: feat(scheduler): add EDF scheduling support"
        )
        return False, errors

    commit_type = match.group('type')
    scope = match.group('scope')
    subject = match.group('subject')
    breaking = match.group('breaking') is not None

    # 验证类型
    if commit_type not in COMMIT_TYPES:
        errors.append(
            f"无效的提交类型: '{commit_type}'\n"
            f"有效类型: {', '.join(COMMIT_TYPES.keys())}"
        )

    # 验证作用域
    if scope and scope not in COMMIT_SCOPES:
        errors.append(
            f"无效的作用域: '{scope}'\n"
            f"有效作用域: {', '.join(COMMIT_SCOPES)}"
        )

    # 验证主题
    if len(subject) < 10:
        errors.append(f"主题太短（至少10个字符）: '{subject}'")

    if len(first_line) > 72:
        errors.append(
            f"第一行太长（{len(first_line)}字符，最多72字符）\n"
            f"当前: {first_line}"
        )

    # 检查主题是否以句号结尾
    if subject.endswith('.'):
        errors.append(f"主题不应以句号结尾: '{subject}'")

    # 检查主题首字母是否大写
    if subject[0].isupper():
        errors.append(f"主题应以小写字母开头: '{subject}'")

    # 验证正文行长度
    if len(lines) > 1:
        body_start = False
        for i, line in enumerate(lines[1:], 1):
            # 跳过空行和注释
            if not line.strip() or line.strip().startswith('#'):
                continue

            body_start = True
            if len(line) > 72:
                errors.append(
                    f"第{i + 1}行太长（{len(line)}字符，最多72字符）"
                )

    # 检查 BREAKING CHANGE
    if breaking or any('BREAKING CHANGE:' in line for line in lines):
        if not breaking and not any('BREAKING CHANGE:' in line for line in lines):
            errors.append("使用 '!' 标记破坏性变更，但缺少 BREAKING CHANGE 说明")

    return len(errors) == 0, errors


def validate_commit_file(commit_file: str) -> int:
    """
    验证 Git 提交消息文件（用于 commit-msg hook）

    Args:
        commit_file: Git 提交消息文件路径

    Returns:
        0表示成功，1表示失败
    """
    try:
        with open(commit_file, 'r', encoding='utf-8') as f:
            content = f.read()
    except FileNotFoundError:
        print_error(f"找不到提交消息文件: {commit_file}")
        return 1
    except Exception as e:
        print_error(f"读取提交消息文件失败: {e}")
        return 1

    # 分离消息和注释
    lines = []
    for line in content.split('\n'):
        if line.startswith('#'):
            break
        lines.append(line)
    message = '\n'.join(lines).strip()

    is_valid, errors = validate_commit_message(message)

    if not is_valid:
        print_error("提交消息验证失败:\n")
        for error in errors:
            print(f"  {Colors.RED}✗{Colors.RESET} {error}")
        print()
        print_info("提示: 运行 'python scripts/conventional_commit.py generate' 生成符合规范的提交消息")
        return 1

    print_success("✓ 提交消息验证通过")
    return 0


def get_user_choice(prompt: str, choices: List[str]) -> str:
    """获取用户选择"""
    print(f"\n{Colors.CYAN}{prompt}{Colors.RESET}")

    for i, choice in enumerate(choices, 1):
        print(f"  {Colors.GREEN}{i}.{Colors.RESET} {choice}")

    while True:
        try:
            value = input(f"\n{Colors.CYAN}选择 (1-{len(choices)}): {Colors.RESET}")
            index = int(value) - 1
            if 0 <= index < len(choices):
                return choices[index]
            print_error(f"请输入 1-{len(choices)} 之间的数字")
        except (ValueError, EOFError):
            print_error("无效输入，请输入数字")
            return ''


def get_user_input(prompt: str, default: str = '') -> str:
    """获取用户输入"""
    if default:
        full_prompt = f"{Colors.CYAN}{prompt} [{default}]: {Colors.RESET}"
    else:
        full_prompt = f"{Colors.CYAN}{prompt}: {Colors.RESET}"

    try:
        value = input(full_prompt).strip()
        return value if value else default
    except EOFError:
        return default


def generate_commit_message() -> None:
    """交互式生成提交消息"""
    print_header("Conventional Commits 提交消息生成器")

    # 选择类型
    type_list = list(COMMIT_TYPES.keys())
    type_display = [f"{t} - {COMMIT_TYPES[t]}" for t in type_list]
    commit_type = get_user_choice("选择提交类型:", type_display)

    if not commit_type:
        print_error("未选择类型，退出")
        return

    # 选择作用域
    scope = get_user_choice(
        "选择作用域 (可选，按 Enter 跳过):",
        COMMIT_SCOPES + ['(跳过)']
    )

    if scope == '(跳过)':
        scope = ''

    # 输入主题
    subject = get_user_input(
        "输入主题描述（使用现在时态，如 'add' 而非 'added'）"
    )

    if not subject:
        print_error("主题不能为空，退出")
        return

    # 破坏性变更
    breaking = get_user_input(
        "是否有破坏性变更? (y/N)",
        'n'
    ).lower() == 'y'

    # 详细描述
    print_info("\n输入详细描述（可选，空行结束）:")
    body_lines = []
    while True:
        try:
            line = input()
            if line == '':
                break
            body_lines.append(line)
        except EOFError:
            break

    body = '\n'.join(body_lines)

    # 关联 Issue
    footer = get_user_input("关联的 Issue (如 'fixes #123')")

    # 破坏性变更说明
    breaking_desc = ''
    if breaking:
        breaking_desc = get_user_input(
            "描述破坏性变更（必须）",
            'BREAKING CHANGE: '
        )

    # 构建提交消息
    commit_msg = f"{commit_type}"

    if scope:
        commit_msg += f"({scope})"

    if breaking:
        commit_msg += "!"

    commit_msg += f": {subject.lower()}"

    if body:
        commit_msg += f"\n\n{body}"

    if footer:
        commit_msg += f"\n\n{footer}"

    if breaking_desc:
        commit_msg += f"\n\n{breaking_desc}"

    # 显示生成的消息
    print_header("生成的提交消息")
    print(f"{Colors.GREEN}{commit_msg}{Colors.RESET}\n")

    # 确认
    confirm = get_user_input("确认使用此消息? (Y/n)", 'y')

    if confirm.lower() != 'n':
        # 保存到文件供 git 使用
        commit_file = '.git/COMMIT_EDITMSG'
        try:
            os.makedirs(os.path.dirname(commit_file), exist_ok=True)
            with open(commit_file, 'w', encoding='utf-8') as f:
                f.write(commit_msg)
            print_success(f"\n提交消息已保存到: {commit_file}")
            print_info("现在可以运行: git commit -F .git/COMMIT_EDITMSG")
        except Exception as e:
            print_error(f"保存提交消息失败: {e}")
            print_info("请复制上面的消息手动提交")


def print_usage() -> None:
    """打印使用说明"""
    print_header("Conventional Commits 工具")
    print(f"""
{Colors.BOLD}使用方法:{Colors.RESET}

  {Colors.CYAN}验证模式{Colors.RESET}（用于 Git Hook）:
    python scripts/conventional_commit.py validate <commit-file>

  {Colors.CYAN}生成模式{Colors.RESET}（交互式生成）:
    python scripts/conventional_commit.py generate

  {Colors.CYAN}帮助信息{Colors.RESET}:
    python scripts/conventional_commit.py help

{Colors.BOLD}提交消息格式:{Colors.RESET}

  <type>(<scope>): <subject>

  <body>

  <footer>

{Colors.BOLD}示例:{Colors.RESET}

  {Colors.GREEN}feat(scheduler): add EDF scheduling support{Colors.RESET}

  Implement earliest deadline first algorithm for real-time tasks.

  This improves schedulability compared to static priority FIFO.

  Closes #123

{Colors.BOLD}可用类型:{Colors.RESET}
""")
    for t, desc in COMMIT_TYPES.items():
        print(f"  {Colors.GREEN}{t:8s}{Colors.RESET} - {desc}")

    print(f"""
{Colors.BOLD}可用作用域:{Colors.RESET}
  {', '.join(COMMIT_SCOPES)}

{Colors.BOLD}更多信息:{Colors.RESET}
  https://www.conventionalcommits.org/
""")


def main() -> int:
    """主函数"""
    if len(sys.argv) < 2:
        print_usage()
        return 0

    command = sys.argv[1].lower()

    if command == 'validate':
        if len(sys.argv) < 3:
            print_error("缺少提交消息文件参数")
            print_info("用法: python scripts/conventional_commit.py validate <commit-file>")
            return 1
        return validate_commit_file(sys.argv[2])

    elif command == 'generate':
        generate_commit_message()
        return 0

    elif command in ['help', '--help', '-h']:
        print_usage()
        return 0

    else:
        print_error(f"未知命令: {command}")
        print_info("运行 'python scripts/conventional_commit.py help' 查看帮助")
        return 1


if __name__ == '__main__':
    sys.exit(main())
