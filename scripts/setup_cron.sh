#!/bin/bash
# AISafeOS64 Cron 任务配置脚本

# 获取当前用户
USER=$(whoami)

# 创建临时 crontab 文件
TEMP_CRON=$(mktemp)

# 导出当前 crontab
crontab -l > "$TEMP_CRON" 2>/dev/null || true

# 添加新的定时任务（避免重复）
if ! grep -q "AISafeOS64 每日进度报告" "$TEMP_CRON"; then
    echo "# AISafeOS64 全自主开发定时任务" >> "$TEMP_CRON"
    echo "" >> "$TEMP_CRON"
    echo "# 每日 20:00 - 生成每日进度报告" >> "$TEMP_CRON"
    echo "0 20 * * * /home/kerfs/AISafeOS64/AISafeOS64/scripts/daily_report.sh >> /home/kerfs/AISafeOS64/AISafeOS64/logs/cron_daily.log 2>&1" >> "$TEMP_CRON"
    echo "" >> "$TEMP_CRON"
    echo "# 每周日 21:00 - 生成每周进度汇总" >> "$TEMP_CRON"
    echo "0 21 * * 0 /home/kerfs/AISafeOS64/AISafeOS64/scripts/weekly_report.sh >> /home/kerfs/AISafeOS64/AISafeOS64/logs/cron_weekly.log 2>&1" >> "$TEMP_CRON"
    echo "" >> "$TEMP_CRON"
    echo "# 每月1日 22:00 - 评估月度里程碑" >> "$TEMP_CRON"
    echo "0 22 1 * * /home/kerfs/AISafeOS64/AISafeOS64/scripts/monthly_report.sh >> /home/kerfs/AISafeOS64/AISafeOS64/logs/cron_monthly.log 2>&1" >> "$TEMP_CRON"
    echo "" >> "$TEMP_CRON"
    echo "# 每个工作日 09:00-18:00 每小时 - 自动开发任务" >> "$TEMP_CRON"
    echo "0 9-18 * * 1-5 /home/kerfs/AISafeOS64/AISafeOS64/scripts/auto_dev.sh >> /home/kerfs/AISafeOS64/AISafeOS64/logs/cron_dev.log 2>&1" >> "$TEMP_CRON"
    echo "" >> "$TEMP_CRON"
    echo "# 每周一 10:00 - 自动提交代码到远程仓库" >> "$TEMP_CRON"
    echo "0 10 * * 1 /home/kerfs/AISafeOS64/AISafeOS64/scripts/git_push.sh >> /home/kerfs/AISafeOS64/AISafeOS64/logs/cron_push.log 2>&1" >> "$TEMP_CRON"
    echo "" >> "$TEMP_CRON"
    echo "# 每天凌晨 02:00 - 备份代码和文档" >> "$TEMP_CRON"
    echo "0 2 * * * /home/kerfs/AISafeOS64/AISafeOS64/scripts/backup.sh >> /home/kerfs/AISafeOS64/AISafeOS64/logs/cron_backup.log 2>&1" >> "$TEMP_CRON"
    echo "" >> "$TEMP_CRON"

    # 安装新的 crontab
    crontab "$TEMP_CRON"
    
    echo "✅ Cron 任务已成功配置"
    echo ""
    echo "定时任务列表："
    echo "  - 每日 20:00: 每日进度报告"
    echo "  - 每周日 21:00: 每周进度汇总"
    echo "  - 每月1日 22:00: 月度里程碑评估"
    echo "  - 工作日 09:00-18:00: 自动开发（每小时）"
    echo "  - 每周一 10:00: 自动推送代码"
    echo "  - 每天凌晨 02:00: 备份代码"
    echo ""
    echo "使用以下命令查看 cron 任务："
    echo "  crontab -l"
else
    echo "⚠️ Cron 任务已经存在，跳过配置"
fi

# 清理临时文件
rm -f "$TEMP_CRON"

# 显示当前 crontab
echo ""
echo "当前 crontab 配置："
echo "---"
crontab -l
echo "---"