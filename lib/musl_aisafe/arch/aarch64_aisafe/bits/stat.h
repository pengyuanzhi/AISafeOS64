#ifndef _MUSL_STAT_H
#define _MUSL_STAT_H

/* 基本类型定义 */
typedef unsigned long dev_t;
typedef unsigned long ino_t;
typedef unsigned int mode_t;
typedef unsigned int nlink_t;
typedef unsigned int uid_t;
typedef unsigned int gid_t;
typedef long off_t;
typedef long blksize_t;
typedef long blkcnt_t;

/* 时间戳结构体 */
struct timespec {
	long tv_sec;
	long tv_nsec;
};

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
