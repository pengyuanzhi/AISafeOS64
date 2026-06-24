#ifndef _MUSL_STAT_H
#define _MUSL_STAT_H

#include <sys/types.h>
#include <time.h>

/* 兼容性 typedef：当系统 sys/stat.h 包含此文件时，确保类型可用 */
#ifndef _AISAFE_BLKSIZE_T_DEFINED
typedef int blksize_t;
#define _AISAFE_BLKSIZE_T_DEFINED
#endif

#ifndef _AISAFE_BLKCNT_T_DEFINED
typedef long blkcnt_t;
#define _AISAFE_BLKCNT_T_DEFINED
#endif

/* struct stat 定义（标准 musl aarch64） */
struct stat {
	dev_t st_dev;
	ino_t st_ino;
	mode_t st_mode;
	nlink_t st_nlink;
	uid_t st_uid;
	gid_t st_gid;
	dev_t st_rdev;
	unsigned long __pad;
	off_t st_size;
	blksize_t st_blksize;
	int __pad2;
	blkcnt_t st_blocks;
	struct timespec st_atim;
	struct timespec st_mtim;
	struct timespec st_ctim;
	unsigned __unused[2];
};

#endif /* _MUSL_STAT_H */
