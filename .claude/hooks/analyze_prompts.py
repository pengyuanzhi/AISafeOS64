#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
提示词记录分析工具

功能:
- 统计提示词数量
- 生成关键词云
- 按模块分类
- 按时间统计
"""

import os
import re
import json
import sys
from pathlib import Path
from datetime import datetime
from collections import Counter

# 修复 Windows 控制台编码问题
if sys.platform == 'win32':
    import io
    sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding='utf-8')
    sys.stderr = io.TextIOWrapper(sys.stderr.buffer, encoding='utf-8')

# 项目根目录
PROJECT_ROOT = Path(__file__).parent.parent.parent
PROMPT_LOG = PROJECT_ROOT / "prompt" / "prompt.md"


def extract_sessions():
    """从 prompt.md 提取所有会话"""
    content = PROMPT_LOG.read_text(encoding='utf-8')

    # 按分隔符分割会话
    sessions = content.split('---\n\n## 提示词记录 #')

    results = []

    for session in sessions[1:]:  # 跳过第一个空会话
        try:
            # 提取会话ID
            session_id = session.split('\n')[0].strip('#')

            # 提取时间戳
            time_match = re.search(r'\*\*时间\*\*:\s*(\d{4}-\d{2}-\d{2}\s+\d{2}:\d{2}:\d{2})', session)
            timestamp = time_match.group(1) if time_match else "unknown"

            # 提取提示词内容
            prompt_match = re.search(r'### 用户提示词\n\n```\n(.+?)\n```', session, re.DOTALL)
            prompt_text = prompt_match.group(1) if prompt_match else ""

            # 提取上下文文件
            files_match = re.search(r'### 上下文文件\n\n(.+?)\n\n###', session, re.DOTALL)
            files_text = files_match.group(1) if files_match else ""
            files = re.findall(r'`([^`]+)`', files_text)

            results.append({
                'session_id': session_id,
                'timestamp': timestamp,
                'prompt': prompt_text,
                'files': files
            })
        except Exception as e:
            print(f"[WARN] 解析会话失败: {e}")
            continue

    return results


def analyze_keywords(sessions):
    """分析关键词"""
    all_text = ' '.join([s['prompt'] for s in sessions])

    # 定义技术关键词
    keywords = [
        '信号量', '互斥锁', '调度器', '任务', '内存管理', 'MMU',
        'IPC', '文件系统', '驱动', '中断', 'ARM64', 'MISRA',
        'POSIX', 'CMake', 'MenuConfig', '测试', '调试',
        '锁', '同步', '并发', '多核', 'SMP', '缓存'
    ]

    keyword_counts = Counter()

    for keyword in keywords:
        count = all_text.count(keyword)
        if count > 0:
            keyword_counts[keyword] = count

    return keyword_counts.most_common()


def categorize_by_module(sessions):
    """按模块分类"""
    modules = {
        '内核': ['调度器', '任务', '中断', '异常'],
        '内存': ['MMU', '内存管理', '页表', '堆', '栈'],
        '同步': ['信号量', '互斥锁', '锁', '同步', '并发'],
        'IPC': ['消息队列', '共享内存', '管道', '信号'],
        '文件系统': ['VFS', '文件', '目录', 'inode'],
        '驱动': ['驱动', 'GPIO', 'UART', 'SPI', 'I2C'],
        '网络': ['TCP', 'UDP', 'socket', '网络'],
        '构建': ['CMake', 'Makefile', '编译', '链接'],
        '测试': ['测试', '单元测试', '覆盖率', 'Unity'],
        '安全': ['MISRA', '安全', '认证', 'ASIL']
    }

    module_counts = {module: 0 for module in modules.keys()}

    for session in sessions:
        prompt = session['prompt']

        for module, keywords in modules.items():
            if any(keyword in prompt for keyword in keywords):
                module_counts[module] += 1

    # 移除计数为0的模块
    return {k: v for k, v in module_counts.items() if v > 0}


def generate_report():
    """生成统计报告"""
    if not PROMPT_LOG.exists():
        print(f"❌ 文件不存在: {PROMPT_LOG}")
        return

    print("[INFO] 正在分析提示词记录...")
    sessions = extract_sessions()

    if not sessions:
        print("⚠️  未找到会话记录")
        return

    print(f"\n✅ 找到 {len(sessions)} 个会话\n")
    print("=" * 60)

    # 1. 基本信息
    print("\n[STATS] 基本信息")
    print(f"  总提示词数: {len(sessions)}")

    if sessions:
        first_time = sessions[0]['timestamp']
        last_time = sessions[-1]['timestamp']
        print(f"  时间范围: {first_time} ~ {last_time}")

    # 2. 关键词统计
    print("\n[KEYWORDS] 热门关键词")
    keywords = analyze_keywords(sessions)
    for keyword, count in keywords[:10]:
        bar = '█' * (count // 2 + 1)
        print(f"  {keyword:12s} {bar} ({count})")

    # 3. 模块分布
    print("\n[MODULES] 按模块分类")
    modules = categorize_by_module(sessions)
    total = sum(modules.values())
    for module, count in sorted(modules.items(), key=lambda x: x[1], reverse=True):
        percent = (count / total * 100) if total > 0 else 0
        print(f"  {module:8s}: {count:2d} ({percent:5.1f}%)")

    # 4. 最近会话
    print("\n[RECENT] 最近 5 个会话")
    for session in sessions[-5:]:
        prompt_preview = session['prompt'][:40].replace('\n', ' ')
        print(f"  [{session['timestamp']}] {prompt_preview}...")

    # 5. 文件引用统计
    print("\n[FILES] 最常引用的文件")
    all_files = []
    for session in sessions:
        all_files.extend(session['files'])

    file_counts = Counter(all_files).most_common(10)
    for file, count in file_counts:
        print(f"  {count:2d}x {file}")

    print("\n" + "=" * 60)
    print("[OK] 分析完成\n")


def export_json(output_file=None):
    """导出为 JSON 格式"""
    sessions = extract_sessions()

    if output_file is None:
        output_file = PROJECT_ROOT / "prompt" / "prompts_export.json"

    data = {
        'export_time': datetime.now().isoformat(),
        'total_sessions': len(sessions),
        'sessions': sessions
    }

    with open(output_file, 'w', encoding='utf-8') as f:
        json.dump(data, f, ensure_ascii=False, indent=2)

    print(f"[OK] 已导出到: {output_file}")


def main():
    """主函数"""
    import argparse

    parser = argparse.ArgumentParser(description='提示词记录分析工具')
    parser.add_argument('--export-json', action='store_true', help='导出为 JSON 格式')
    parser.add_argument('--output', type=str, help='输出文件路径')

    args = parser.parse_args()

    if args.export_json:
        export_json(args.output)
    else:
        generate_report()


if __name__ == '__main__':
    main()
