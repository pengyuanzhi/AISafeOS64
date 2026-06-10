#!/bin/bash
# AISafeOS64 自动备份脚本
# 每天凌晨 02:00 执行

set -e

WORKDIR="/home/kerfs/AISafeOS64/AISafeOS64"
BACKUP_DIR="/home/kerfs/AISafeOS64/backups"
DATE=$(date '+%Y%m%d')
TODAY=$(date '+%Y-%m-%d')
TIME=$(date '+%H:%M:%S')

# 创建备份目录
mkdir -p "$BACKUP_DIR"

cd "$WORKDIR"

echo "💾 开始备份 - $TODAY $TIME" > "$BACKUP_DIR/backup_$DATE.log"

# 1. 备份源代码
echo "📦 备份源代码..." >> "$BACKUP_DIR/backup_$DATE.log"
tar -czf "$BACKUP_DIR/source_$DATE.tar.gz" \
    --exclude='build' \
    --exclude='*.o' \
    --exclude='*.elf' \
    --exclude='*.bin' \
    --exclude='*.dis' \
    --exclude='logs' \
    --exclude='reports' \
    . 2>&1 || echo "⚠️ 源代码备份失败" >> "$BACKUP_DIR/backup_$DATE.log"

if [ -f "$BACKUP_DIR/source_$DATE.tar.gz" ]; then
    SOURCE_SIZE=$(du -h "$BACKUP_DIR/source_$DATE.tar.gz" | cut -f1)
    echo "✅ 源代码备份成功（$SOURCE_SIZE）" >> "$BACKUP_DIR/backup_$DATE.log"
else
    echo "❌ 源代码备份失败" >> "$BACKUP_DIR/backup_$DATE.log"
fi

# 2. 备份文档
echo "📚 备份文档..." >> "$BACKUP_DIR/backup_$DATE.log"
tar -czf "$BACKUP_DIR/docs_$DATE.tar.gz" docs/ 2>&1 || echo "⚠️ 文档备份失败" >> "$BACKUP_DIR/backup_$DATE.log"

if [ -f "$BACKUP_DIR/docs_$DATE.tar.gz" ]; then
    DOCS_SIZE=$(du -h "$BACKUP_DIR/docs_$DATE.tar.gz" | cut -f1)
    echo "✅ 文档备份成功（$DOCS_SIZE）" >> "$BACKUP_DIR/backup_$DATE.log"
else
    echo "❌ 文档备份失败" >> "$BACKUP_DIR/backup_$DATE.log"
fi

# 3. 备份构建产物（可选）
if [ -d "build" ]; then
    echo "🔨 备份构建产物..." >> "$BACKUP_DIR/backup_$DATE.log"
    tar -czf "$BACKUP_DIR/build_$DATE.tar.gz" build/*.elf build/*.bin 2>/dev/null || echo "⚠️ 构建产物备份失败" >> "$BACKUP_DIR/backup_$DATE.log"
    
    if [ -f "$BACKUP_DIR/build_$DATE.tar.gz" ]; then
        BUILD_SIZE=$(du -h "$BACKUP_DIR/build_$DATE.tar.gz" | cut -f1)
        echo "✅ 构建产物备份成功（$BUILD_SIZE）" >> "$BACKUP_DIR/backup_$DATE.log"
    fi
fi

# 4. 备份 Git 仓库
echo "📊 备份 Git 仓库..." >> "$BACKUP_DIR/backup_$DATE.log"
git bundle create "$BACKUP_DIR/repo_$DATE.bundle" --all 2>&1 || echo "⚠️ Git 仓库备份失败" >> "$BACKUP_DIR/backup_$DATE.log"

if [ -f "$BACKUP_DIR/repo_$DATE.bundle" ]; then
    REPO_SIZE=$(du -h "$BACKUP_DIR/repo_$DATE.bundle" | cut -f1)
    echo "✅ Git 仓库备份成功（$REPO_SIZE）" >> "$BACKUP_DIR/backup_$DATE.log"
else
    echo "❌ Git 仓库备份失败" >> "$BACKUP_DIR/backup_$DATE.log"
fi

# 5. 备份配置文件
echo "⚙️ 备份配置文件..." >> "$BACKUP_DIR/backup_$DATE.log"
tar -czf "$BACKUP_DIR/config_$DATE.tar.gz" \
    CMakeLists.txt \
    cmake/ \
    .clang-format \
    .clang-tidy \
    .gitignore 2>/dev/null || echo "⚠️ 配置文件备份失败" >> "$BACKUP_DIR/backup_$DATE.log"

if [ -f "$BACKUP_DIR/config_$DATE.tar.gz" ]; then
    CONFIG_SIZE=$(du -h "$BACKUP_DIR/config_$DATE.tar.gz" | cut -f1)
    echo "✅ 配置文件备份成功（$CONFIG_SIZE）" >> "$BACKUP_DIR/backup_$DATE.log"
fi

# 6. 创建备份索引
echo "📋 创建备份索引..." >> "$BACKUP_DIR/backup_$DATE.log"
cat > "$BACKUP_DIR/index_$DATE.txt" << EOF
AISafeOS64 备份索引
备份时间：$TODAY $TIME
备份路径：$BACKUP_DIR

备份文件清单：
EOF

ls -lh "$BACKUP_DIR" | grep "$DATE" | awk '{print "- " $9 " (" $5 ")"}' >> "$BACKUP_DIR/index_$DATE.txt"

cat >> "$BACKUP_DIR/index_$DATE.txt" << EOF

备份统计：
- 源代码：$(if [ -f "$BACKUP_DIR/source_$DATE.tar.gz" ]; then echo "✅"; else echo "❌"; fi)
- 文档：$(if [ -f "$BACKUP_DIR/docs_$DATE.tar.gz" ]; then echo "✅"; else echo "❌"; fi)
- 构建产物：$(if [ -f "$BACKUP_DIR/build_$DATE.tar.gz" ]; then echo "✅"; else echo "❌"; fi)
- Git 仓库：$(if [ -f "$BACKUP_DIR/repo_$DATE.bundle" ]; then echo "✅"; else echo "❌"; fi)
- 配置文件：$(if [ -f "$BACKUP_DIR/config_$DATE.tar.gz" ]; then echo "✅"; else echo "❌"; fi)

---
生成时间：$(date '+%Y-%m-%d %H:%M:%S')
EOF

echo "✅ 备份索引创建成功" >> "$BACKUP_DIR/backup_$DATE.log"

# 7. 清理旧备份（保留最近7天）
echo "🧹 清理旧备份..." >> "$BACKUP_DIR/backup_$DATE.log"
find "$BACKUP_DIR" -name "*_$(date -d '7 days ago' '+%Y%m%d').*" -delete 2>/dev/null || true
find "$BACKUP_DIR" -name "*.tar.gz" -mtime +7 -delete 2>/dev/null || true
find "$BACKUP_DIR" -name "*.bundle" -mtime +7 -delete 2>/dev/null || true
echo "✅ 旧备份清理完成" >> "$BACKUP_DIR/backup_$DATE.log"

# 8. 备份统计
echo "" >> "$BACKUP_DIR/backup_$DATE.log"
echo "📊 备份统计" >> "$BACKUP_DIR/backup_$DATE.log"
BACKUP_COUNT=$(ls -1 "$BACKUP_DIR"/*_$DATE.* 2>/dev/null | wc -l)
BACKUP_SIZE=$(du -sh "$BACKUP_DIR"/*_$DATE.* 2>/dev/null | awk '{sum+=$1} END {print sum "MB"}' || echo "未知")
echo "- 备份文件数：$BACKUP_COUNT" >> "$BACKUP_DIR/backup_$DATE.log"
echo "- 备份总大小：$BACKUP_SIZE" >> "$BACKUP_DIR/backup_$DATE.log"

# 9. 验证备份完整性
echo "" >> "$BACKUP_DIR/backup_$DATE.log"
echo "🔍 验证备份完整性..." >> "$BACKUP_DIR/backup_$DATE.log"

if [ -f "$BACKUP_DIR/source_$DATE.tar.gz" ]; then
    if tar -tzf "$BACKUP_DIR/source_$DATE.tar.gz" > /dev/null 2>&1; then
        echo "✅ 源代码备份验证通过" >> "$BACKUP_DIR/backup_$DATE.log"
    else
        echo "❌ 源代码备份验证失败" >> "$BACKUP_DIR/backup_$DATE.log"
    fi
fi

if [ -f "$BACKUP_DIR/repo_$DATE.bundle" ]; then
    if git bundle verify "$BACKUP_DIR/repo_$DATE.bundle" > /dev/null 2>&1; then
        echo "✅ Git 仓库备份验证通过" >> "$BACKUP_DIR/backup_$DATE.log"
    else
        echo "❌ Git 仓库备份验证失败" >> "$BACKUP_DIR/backup_$DATE.log"
    fi
fi

# 输出备份日志
echo "" >> "$BACKUP_DIR/backup_$DATE.log"
echo "✅ 备份完成 - $(date '+%H:%M:%S')" >> "$BACKUP_DIR/backup_$DATE.log"

cat "$BACKUP_DIR/backup_$DATE.log"

# 发送备份通知
echo "💾 备份完成通知" | openclaw exec --agent aisafeos "转发到飞书：备份完成" 2>/dev/null || true