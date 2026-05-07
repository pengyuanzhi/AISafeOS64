/**
 * @brief 创建分区（自动选择 MBR/GPT）
 */
int32_t partition_create(partition_disk_t *disk,
                         const char *partition_name,
                         uint64_t start_lba,
                         uint64_t size_in_sectors,
                         uint32_t partition_type,
                         bool bootable)
{
    if (disk == NULL || partition_name == NULL)
    {
        return -1;
    }

    /* 根据分区表类型选择实现 */
    if (disk->table_type == PARTITION_TYPE_GPT)
    {
        /* GPT 分区 */
        return gpt_create_partition(disk, partition_name, start_lba, size_in_sectors,
                                     partition_type, bootable);
    }
    else if (disk->table_type == PARTITION_TYPE_MBR)
    {
        /* MBR 分区（需要指定分区编号） */
        /* TODO: 实现自动分配分区编号 */
        return -1;
    }
    else
    {
        printf("[Partition] Unknown partition table type\n");
        return -1;
    }
}

/**
 * @brief 删除分区（自动选择 MBR/GPT）
 */
int32_t partition_delete(partition_disk_t *disk, uint32_t partition_number)
{
    if (disk == NULL)
    {
        return -1;
    }

    /* 根据分区表类型选择实现 */
    if (disk->table_type == PARTITION_TYPE_GPT)
    {
        /* GPT 分区 */
        return gpt_delete_partition(disk, partition_number);
    }
    else if (disk->table_type == PARTITION_TYPE_MBR)
    {
        /* MBR 分区 */
        return partition_mbr_delete(disk, partition_number);
    }
    else
    {
        printf("[Partition] Unknown partition table type\n");
        return -1;
    }
}

/**
 * @brief 调整分区大小（自动选择 MBR/GPT）
 */
int32_t partition_resize(partition_disk_t *disk, uint32_t partition_number,
                          uint64_t new_size)
{
    if (disk == NULL)
    {
        return -1;
    }

    /* 根据分区表类型选择实现 */
    if (disk->table_type == PARTITION_TYPE_GPT)
    {
        /* GPT 分区 */
        return gpt_resize_partition(disk, partition_number, new_size);
    }
    else if (disk->table_type == PARTITION_TYPE_MBR)
    {
        /* MBR 分区 */
        return partition_mbr_resize(disk, partition_number, new_size);
    }
    else
    {
        printf("[Partition] Unknown partition table type\n");
        return -1;
    }
}

/**
 * @brief 同步分区表（自动选择 MBR/GPT）
 */
int32_t partition_sync(partition_disk_t *disk)
{
    if (disk == NULL)
    {
        return -1;
    }

    printf("[Partition] Syncing partition table: %s\n", disk->device_path);

    /* 根据分区表类型选择实现 */
    if (disk->table_type == PARTITION_TYPE_GPT)
    {
        /* GPT 分区表 */
        return gpt_sync_partition_table(disk);
    }
    else if (disk->table_type == PARTITION_TYPE_MBR)
    {
        /* MBR 分区表（已写入时同步） */
        printf("[Partition] MBR partition table already synced\n");
        return 0;
    }
    else
    {
        printf("[Partition] Unknown partition table type\n");
        return -1;
    }
}
