#ifndef _TYPES_H_
#define _TYPES_H_

#include <stdint.h>
#include <sys/ioctl.h>
/******************************************************************************
 * SECTION: Type def
 *******************************************************************************/
typedef int boolean;
typedef uint16_t flag16;

typedef enum newfs_file_type
{
    NEWFS_REG_FILE,
    NEWFS_DIR,
    NEWFS_SYM_LINK
} NEWFS_FILE_TYPE;

/******************************************************************************
 * SECTION: Macro
 *******************************************************************************/
#define TRUE 1
#define FALSE 0
#define UINT32_BITS 32
#define UINT8_BITS 8

#define NEWFS_MAGIC_NUM 0x52415453
#define NEWFS_SUPER_OFS 0
#define NEWFS_ROOT_INO 0
#define NEWFS_DATA_BLK 6

#define NEWFS_SUPER_OFS 0

#define NEWFS_ERROR_NONE 0
#define NEWFS_ERROR_ACCESS EACCES
#define NEWFS_ERROR_SEEK ESPIPE
#define NEWFS_ERROR_ISDIR EISDIR
#define NEWFS_ERROR_NOSPACE ENOSPC
#define NEWFS_ERROR_EXISTS EEXIST
#define NEWFS_ERROR_NOTFOUND ENOENT
#define NEWFS_ERROR_UNSUPPORTED ENXIO
#define NEWFS_ERROR_IO EIO       /* Error Input/Output */
#define NEWFS_ERROR_INVAL EINVAL /* Invalid Args */

#define NEWFS_MAX_FILE_NAME 128
#define NEWFS_INODE_PER_FILE 1
#define NEWFS_DATA_PER_FILE 6 // 一个文件有6个数据块
#define NEWFS_DEFAULT_PERM 0777

#define NEWFS_IOC_MAGIC 'S'
#define NEWFS_IOC_SEEK _IO(NEWFS_IOC_MAGIC, 0)

#define NEWFS_FLAG_BUF_DIRTY 0x1
#define NEWFS_FLAG_BUF_OCCUPY 0x2

// 磁盘布局设计
#define NEWFS_INODE_PER_BLK 8 // 一个逻辑块能放8个inode
#define NEWFS_SUPER_BLKS 1
#define NEWFS_INODE_MAP_BLKS 1
#define NEWFS_DATA_MAP_BLKS 1
#define NEWFS_INODE_BLKS 585  // 4096/7
#define NEWFS_DATA_BLKS 3508 // 4096-1-1-1-585
/******************************************************************************
 * SECTION: Macro Function
 *******************************************************************************/
#define NEWFS_IO_SZ() (newfs_super.sz_io)
#define NEWFS_BLOCK_SZ() (newfs_super.sz_block)
#define NEWFS_DISK_SZ() (newfs_super.sz_disk)
#define NEWFS_DRIVER() (newfs_super.fd)
#define NEWFS_BLKS_SZ(blks) ((blks) * NEWFS_BLOCK_SZ())
#define NEWFS_DENTRY_PER_BLK() (NEWFS_BLOCK_SZ() / sizeof(struct newfs_dentry_d))

#define NEWFS_ROUND_DOWN(value, round) ((value) % (round) == 0 ? (value) : ((value) / (round)) * (round))
#define NEWFS_ROUND_UP(value, round) ((value) % (round) == 0 ? (value) : ((value) / (round) + 1) * (round))

#define NEWFS_ASSIGN_FNAME(pnewfs_dentry, _fname) memcpy(pnewfs_dentry->fname, _fname, strlen(_fname))

// 计算inode和data的偏移量
#define NEWFS_INO_OFS(ino) (newfs_super.inode_offset + NEWFS_BLKS_SZ(ino))
#define NEWFS_DATA_OFS(dno) (newfs_super.data_offset + NEWFS_BLKS_SZ(dno))

#define NEWFS_IS_DIR(pinode) ((pinode)->dentry->ftype == NEWFS_DIR)
#define NEWFS_IS_REG(pinode) ((pinode)->dentry->ftype == NEWFS_REG_FILE)

/******************************************************************************
 * SECTION: FS Specific Structure - In memory structure
 *******************************************************************************/
#define MAX_NAME_LEN 128
struct newfs_dentry;
struct newfs_inode;
struct newfs_super;

struct custom_options
{
    const char *device;
};

struct newfs_super
{
    uint32_t magic;
    int fd;

    int sz_io;
    int sz_block;
    int sz_disk;
    int sz_usage;

    // 索引节点位图
    int max_ino;
    uint8_t *map_inode;
    int map_inode_offset;
    uint32_t map_inode_blks;

    // 数据块位图
    int max_dno;
    uint8_t *map_data;
    int map_data_offset;
    uint32_t map_data_blks;

    // 索引节点
    int inode_offset;

    // 数据块
    int data_offset;

    boolean is_mounted;
    struct newfs_dentry *root_dentry;
};

struct newfs_inode
{
    uint32_t ino;
    int size; /* 文件已占用空间 */

    int dir_cnt;                  /* 目录项数量 */
    struct newfs_dentry *dentry;  /* 指向该inode的dentry */
    struct newfs_dentry *dentrys; /* 所有目录项 */

    int block_pointer[NEWFS_DATA_PER_FILE]; /* 指向分配的数据块的块号 */
    uint8_t *data[NEWFS_DATA_PER_FILE];     /* 数据块在内存中的地址 */
    int block_allocted;                     /* 已分配数据块数量 */
};

struct newfs_dentry
{
    char fname[MAX_NAME_LEN];
    uint32_t ino;
    NEWFS_FILE_TYPE ftype;
    struct newfs_dentry *parent;
    struct newfs_dentry *brother;
    struct newfs_inode *inode;
};

static inline struct newfs_dentry *new_dentry(char *fname, NEWFS_FILE_TYPE ftype)
{
    struct newfs_dentry *dentry = (struct newfs_dentry *)malloc(sizeof(struct newfs_dentry));
    memset(dentry, 0, sizeof(struct newfs_dentry));
    NEWFS_ASSIGN_FNAME(dentry, fname);
    dentry->ftype = ftype;
    dentry->ino = -1;
    dentry->inode = NULL;
    dentry->parent = NULL;
    dentry->brother = NULL;
    return dentry;
}

/******************************************************************************
 * SECTION: NEWFS Specific Structure - Disk structure
 *******************************************************************************/
struct newfs_super_d
{
    uint32_t magic_num;
    uint32_t sz_usage;

    uint32_t max_ino;
    uint32_t max_dno;

    // 索引节点位图
    uint32_t map_inode_blks;
    uint32_t map_inode_offset;
    // 数据块位图
    uint32_t map_data_blks;
    uint32_t map_data_offset;
    // 索引节点
    uint32_t inode_offset;
    // 数据块
    uint32_t data_offset;
};

struct newfs_inode_d
{
    uint32_t ino;
    int size;
    int dir_cnt;
    NEWFS_FILE_TYPE ftype;
    int block_pointer[NEWFS_DATA_PER_FILE];
    int block_allocted;
};

struct newfs_dentry_d
{
    char fname[NEWFS_MAX_FILE_NAME];
    NEWFS_FILE_TYPE ftype;
    uint32_t ino;
};

#endif /* _TYPES_H_ */