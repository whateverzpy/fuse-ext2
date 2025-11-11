#include "newfs.h"

extern struct newfs_super newfs_super;
extern struct custom_options newfs_options;

char *newfs_get_fname(const char *path)
{
    char ch = '/';
    char *q = strrchr(path, ch);
    return q ? q + 1 : (char *)path;
}

int newfs_calc_lvl(const char *path)
{
    const char *str = path;
    int lvl = 0;
    if (strcmp(path, "/") == 0)
        return 0;
    while (*str != '\0')
    {
        if (*str == '/')
            lvl++;
        str++;
    }
    return lvl;
}

int newfs_driver_read(int offset, uint8_t *out_content, int size)
{
    int offset_aligned = NEWFS_ROUND_DOWN(offset, NEWFS_BLOCK_SZ());
    int bias = offset - offset_aligned;
    int size_aligned = NEWFS_ROUND_UP((size + bias), NEWFS_BLOCK_SZ());

    uint8_t *temp_content = (uint8_t *)malloc(size_aligned);
    uint8_t *cur = temp_content;

    ddriver_seek(NEWFS_DRIVER(), offset_aligned, SEEK_SET);
    while (size_aligned != 0)
    {
        ddriver_read(NEWFS_DRIVER(), cur, NEWFS_IO_SZ());
        cur += NEWFS_IO_SZ();
        size_aligned -= NEWFS_IO_SZ();
    }
    memcpy(out_content, temp_content + bias, size);
    free(temp_content);
    return NEWFS_ERROR_NONE;
}

int newfs_driver_write(int offset, uint8_t *in_content, int size)
{
    int offset_aligned = NEWFS_ROUND_DOWN(offset, NEWFS_BLOCK_SZ());
    int bias = offset - offset_aligned;
    int size_aligned = NEWFS_ROUND_UP((size + bias), NEWFS_BLOCK_SZ());

    uint8_t *temp_content = (uint8_t *)malloc(size_aligned);
    uint8_t *cur = temp_content;

    newfs_driver_read(offset_aligned, temp_content, size_aligned);
    memcpy(temp_content + bias, in_content, size);

    ddriver_seek(NEWFS_DRIVER(), offset_aligned, SEEK_SET);
    while (size_aligned != 0)
    {
        ddriver_write(NEWFS_DRIVER(), cur, NEWFS_IO_SZ());
        cur += NEWFS_IO_SZ();
        size_aligned -= NEWFS_IO_SZ();
    }

    free(temp_content);
    return NEWFS_ERROR_NONE;
}

int newfs_alloc_dentry(struct newfs_inode *inode, struct newfs_dentry *dentry)
{
    if (inode->dir_cnt % NEWFS_DENTRY_PER_BLK() == 0)
    {
        inode->block_pointer[inode->block_allocted] = newfs_alloc_data();
        inode->block_allocted++;
    }
    if (inode->dentrys == NULL)
        inode->dentrys = dentry;
    else
    {
        dentry->brother = inode->dentrys;
        inode->dentrys = dentry;
    }
    inode->dir_cnt++;
    inode->size += sizeof(struct newfs_dentry);
    return inode->dir_cnt;
}

int newfs_drop_dentry(struct newfs_inode *inode, struct newfs_dentry *dentry)
{
    boolean is_find = FALSE;
    struct newfs_dentry *dentry_cursor = inode->dentrys;

    if (dentry_cursor == dentry)
    {
        inode->dentrys = dentry->brother;
        is_find = TRUE;
    }
    else
    {
        while (dentry_cursor)
        {
            if (dentry_cursor->brother == dentry)
            {
                dentry_cursor->brother = dentry->brother;
                is_find = TRUE;
                break;
            }
            dentry_cursor = dentry_cursor->brother;
        }
    }
    if (!is_find)
        return -NEWFS_ERROR_NOTFOUND;
    inode->dir_cnt--;
    return inode->dir_cnt;
}

int newfs_alloc_data()
{
    int byte_cursor = 0, bit_cursor = 0;
    int available_block_idx = 0;
    boolean is_find = FALSE;

    for (byte_cursor = 0; byte_cursor < NEWFS_BLKS_SZ(newfs_super.map_data_blks); byte_cursor++)
    {
        for (bit_cursor = 0; bit_cursor < UINT8_BITS; bit_cursor++)
        {
            if ((newfs_super.map_data[byte_cursor] & (0x1 << bit_cursor)) == 0)
            {
                newfs_super.map_data[byte_cursor] |= (0x1 << bit_cursor);
                is_find = TRUE;
                break;
            }
        }
        if (is_find)
            break;
    }
    available_block_idx = byte_cursor * 8 + bit_cursor;
    if (!is_find || available_block_idx >= newfs_super.max_dno)
        return -NEWFS_ERROR_NOSPACE;
    return available_block_idx;
}

struct newfs_inode *newfs_alloc_inode(struct newfs_dentry *dentry)
{
    struct newfs_inode *inode;
    int byte_cursor = 0, bit_cursor = 0, available_inode_idx = 0;
    boolean is_find_free_entry = FALSE;
    for (byte_cursor = 0; byte_cursor < newfs_super.sz_io; byte_cursor++)
    {
        for (bit_cursor = 0; bit_cursor < UINT8_BITS; bit_cursor++)
        {
            if ((newfs_super.map_inode[byte_cursor] & (0x1 << bit_cursor)) == 0)
            {
                newfs_super.map_inode[byte_cursor] |= (0x1 << bit_cursor);
                is_find_free_entry = TRUE;
                break;
            }
        }
        if (is_find_free_entry)
            break;
    }
    available_inode_idx = byte_cursor * 8 + bit_cursor;
    if (!is_find_free_entry || available_inode_idx >= newfs_super.max_ino)
        return (struct newfs_inode *)(intptr_t)(-NEWFS_ERROR_NOSPACE);

    inode = (struct newfs_inode *)malloc(sizeof(struct newfs_inode));
    memset(inode, 0, sizeof(*inode));
    inode->ino = available_inode_idx;
    inode->size = 0;
    inode->block_allocted = 0;

    dentry->inode = inode;
    dentry->ino = inode->ino;
    inode->dentry = dentry;
    inode->dir_cnt = 0;
    inode->dentrys = NULL;

    if (NEWFS_IS_REG(inode))
    {
        for (int i = 0; i < NEWFS_DATA_PER_FILE; i++)
        {
            inode->data[i] = (uint8_t *)malloc(NEWFS_BLOCK_SZ());
            memset(inode->data[i], 0, NEWFS_BLOCK_SZ());
        }
    }
    return inode;
}

int newfs_sync_inode(struct newfs_inode *inode)
{
    struct newfs_inode_d inode_d;
    struct newfs_dentry *dentry_cursor;
    struct newfs_dentry_d dentry_d;
    int ino = inode->ino;

    inode_d.ino = ino;
    inode_d.size = inode->size;
    inode_d.ftype = inode->dentry->ftype;
    inode_d.dir_cnt = inode->dir_cnt;
    inode_d.block_allocted = inode->block_allocted;
    for (int i = 0; i < inode->block_allocted; i++)
    {
        inode_d.block_pointer[i] = inode->block_pointer[i];
    }

    if (newfs_driver_write(NEWFS_INO_OFS(ino), (uint8_t *)&inode_d, sizeof(struct newfs_inode_d)) != NEWFS_ERROR_NONE)
        return -NEWFS_ERROR_IO;

    if (NEWFS_IS_DIR(inode))
    {
        dentry_cursor = inode->dentrys;
        int i = 0;
        while ((dentry_cursor != NULL) && (i < inode->block_allocted))
        {
            int offset = NEWFS_DATA_OFS(inode->block_pointer[i]);
            int current_dentry_cnt = 0;
            while ((dentry_cursor != NULL) && (current_dentry_cnt < NEWFS_DENTRY_PER_BLK()))
            {
                memcpy(dentry_d.fname, dentry_cursor->fname, NEWFS_MAX_FILE_NAME);
                dentry_d.ftype = dentry_cursor->ftype;
                dentry_d.ino = dentry_cursor->ino;
                if (newfs_driver_write(offset, (uint8_t *)&dentry_d, sizeof(struct newfs_dentry_d)) != NEWFS_ERROR_NONE)
                    return -NEWFS_ERROR_IO;

                newfs_sync_inode(dentry_cursor->inode);
                dentry_cursor = dentry_cursor->brother;
                offset += sizeof(struct newfs_dentry_d);
                current_dentry_cnt++;
            }
            i++;
        }
    }
    else if (NEWFS_IS_REG(inode))
    {
        for (int i = 0; i < inode->block_allocted; i++)
        {
            if (newfs_driver_write(NEWFS_DATA_OFS(inode->block_pointer[i]),
                                   inode->data[i], NEWFS_BLOCK_SZ()) != NEWFS_ERROR_NONE)
                return -NEWFS_ERROR_IO;
        }
    }
    return NEWFS_ERROR_NONE;
}

int newfs_drop_inode(struct newfs_inode *inode)
{
    struct newfs_dentry *dentry_cursor;
    struct newfs_dentry *dentry_to_free;
    struct newfs_inode *inode_cursor;

    int byte_cursor = 0, bit_cursor = 0, ino_cursor = 0;
    boolean is_find = FALSE;

    if (inode == newfs_super.root_dentry->inode)
        return NEWFS_ERROR_INVAL;

    if (NEWFS_IS_DIR(inode))
    {
        dentry_cursor = inode->dentrys;
        while (dentry_cursor)
        {
            inode_cursor = dentry_cursor->inode;
            newfs_drop_inode(inode_cursor);
            newfs_drop_dentry(inode, dentry_cursor);
            dentry_to_free = dentry_cursor;
            dentry_cursor = dentry_cursor->brother;
            free(dentry_to_free);
        }
        for (byte_cursor = 0; byte_cursor < NEWFS_BLKS_SZ(newfs_super.map_inode_blks); byte_cursor++)
        {
            for (bit_cursor = 0; bit_cursor < UINT8_BITS; bit_cursor++)
            {
                if (ino_cursor == inode->ino)
                {
                    newfs_super.map_inode[byte_cursor] &= (uint8_t)(~(0x1 << bit_cursor));
                    is_find = TRUE;
                    break;
                }
                ino_cursor++;
            }
            if (is_find == TRUE)
                break;
        }
    }
    else
    {
        for (byte_cursor = 0; byte_cursor < NEWFS_BLKS_SZ(newfs_super.map_inode_blks); byte_cursor++)
        {
            for (bit_cursor = 0; bit_cursor < UINT8_BITS; bit_cursor++)
            {
                if (ino_cursor == inode->ino)
                {
                    newfs_super.map_inode[byte_cursor] &= (uint8_t)(~(0x1 << bit_cursor));
                    is_find = TRUE;
                    break;
                }
                ino_cursor++;
            }
            if (is_find == TRUE)
                break;
        }
        for (int i = 0; i < inode->block_allocted; i++)
            free(inode->data[i]);
        free(inode);
    }
    return NEWFS_ERROR_NONE;
}

struct newfs_inode *newfs_read_inode(struct newfs_dentry *dentry, int ino)
{
    struct newfs_inode *inode = (struct newfs_inode *)malloc(sizeof(struct newfs_inode));
    memset(inode, 0, sizeof(*inode));
    struct newfs_inode_d inode_d;
    struct newfs_dentry *sub_dentry;
    struct newfs_dentry_d dentry_d;

    if (newfs_driver_read(NEWFS_INO_OFS(ino), (uint8_t *)&inode_d, sizeof(struct newfs_inode_d)) != NEWFS_ERROR_NONE)
        return NULL;

    inode->ino = inode_d.ino;
    inode->size = inode_d.size;
    inode->dentry = dentry;
    inode->dentrys = NULL;
    inode->block_allocted = inode_d.block_allocted;
    for (int i = 0; i < NEWFS_DATA_PER_FILE; i++)
        inode->block_pointer[i] = inode_d.block_pointer[i];

    if (inode_d.ftype == NEWFS_DIR)
    {
        int dir_cnt = inode_d.dir_cnt;
        int i = 0;
        while ((dir_cnt > 0) && (i < inode->block_allocted))
        {
            int base = NEWFS_DATA_OFS(inode->block_pointer[i]);
            int offset = base;
            int cnt = 0;
            while ((dir_cnt > 0) && (cnt < NEWFS_DENTRY_PER_BLK()))
            {
                if (newfs_driver_read(offset, (uint8_t *)&dentry_d, sizeof(struct newfs_dentry_d)) != NEWFS_ERROR_NONE)
                    return NULL;
                sub_dentry = new_dentry(dentry_d.fname, dentry_d.ftype);
                sub_dentry->parent = inode->dentry;
                sub_dentry->ino = dentry_d.ino;
                newfs_alloc_dentry(inode, sub_dentry);
                offset += sizeof(struct newfs_dentry_d);
                dir_cnt--;
                cnt++;
            }
            i++;
        }
    }
    else
    {
        for (int i = 0; i < inode->block_allocted; i++)
        {
            inode->data[i] = (uint8_t *)malloc(NEWFS_BLOCK_SZ());
            if (newfs_driver_read(NEWFS_DATA_OFS(inode->block_pointer[i]), (uint8_t *)inode->data[i],
                                  NEWFS_BLOCK_SZ()) != NEWFS_ERROR_NONE)
                return NULL;
        }
    }
    return inode;
}

struct newfs_dentry *newfs_get_dentry(struct newfs_inode *inode, int dir)
{
    struct newfs_dentry *dentry_cursor = inode->dentrys;
    int cnt = 0;
    while (dentry_cursor)
    {
        if (dir == cnt)
            return dentry_cursor;
        cnt++;
        dentry_cursor = dentry_cursor->brother;
    }
    return NULL;
}

struct newfs_dentry *newfs_lookup(const char *path, boolean *is_find, boolean *is_root)
{
    struct newfs_dentry *dentry_cursor = newfs_super.root_dentry;
    struct newfs_dentry *dentry_ret = NULL;
    struct newfs_inode *inode;
    int total_lvl = newfs_calc_lvl(path);
    int lvl = 0;
    boolean is_hit;
    char *fname = NULL;
    char *path_cpy = (char *)malloc(strlen(path) + 1);
    strcpy(path_cpy, path);

    *is_root = FALSE;
    if (total_lvl == 0)
    {
        *is_find = TRUE;
        *is_root = TRUE;
        dentry_ret = newfs_super.root_dentry;
        free(path_cpy);
        return dentry_ret;
    }
    fname = strtok(path_cpy, "/");
    while (fname)
    {
        lvl++;
        if (dentry_cursor->inode == NULL)
            newfs_read_inode(dentry_cursor, dentry_cursor->ino);
        inode = dentry_cursor->inode;

        if (NEWFS_IS_REG(inode) && lvl < total_lvl)
        {
            dentry_ret = inode->dentry;
            break;
        }
        if (NEWFS_IS_DIR(inode))
        {
            dentry_cursor = inode->dentrys;
            is_hit = FALSE;
            while (dentry_cursor)
            {
                if (memcmp(dentry_cursor->fname, fname, strlen(fname)) == 0)
                {
                    is_hit = TRUE;
                    break;
                }
                dentry_cursor = dentry_cursor->brother;
            }
            if (!is_hit)
            {
                *is_find = FALSE;
                dentry_ret = inode->dentry;
                break;
            }
            if (is_hit && lvl == total_lvl)
            {
                *is_find = TRUE;
                dentry_ret = dentry_cursor;
                break;
            }
        }
        fname = strtok(NULL, "/");
    }

    if (dentry_ret && dentry_ret->inode == NULL)
    {
        dentry_ret->inode = newfs_read_inode(dentry_ret, dentry_ret->ino);
    }
    free(path_cpy);
    return dentry_ret;
}

int newfs_mount(struct custom_options options)
{
    int ret = NEWFS_ERROR_NONE;
    int driver_fd;
    struct newfs_super_d newfs_super_d;
    struct newfs_dentry *root_dentry;
    struct newfs_inode *root_inode;

    int inode_blks;
    int data_blks;
    int map_inode_blks;
    int map_data_blks;

    int super_blks;
    boolean is_init = FALSE;

    newfs_super.is_mounted = FALSE;

    driver_fd = ddriver_open((char *)options.device);
    if (driver_fd < 0)
        return driver_fd;

    newfs_super.fd = driver_fd;
    ddriver_ioctl(NEWFS_DRIVER(), IOC_REQ_DEVICE_SIZE, &newfs_super.sz_disk);
    ddriver_ioctl(NEWFS_DRIVER(), IOC_REQ_DEVICE_IO_SZ, &newfs_super.sz_io);
    newfs_super.sz_block = 2 * newfs_super.sz_io;

    root_dentry = new_dentry("/", NEWFS_DIR);

    if (newfs_driver_read(NEWFS_SUPER_OFS, (uint8_t *)(&newfs_super_d), sizeof(struct newfs_super_d)) != NEWFS_ERROR_NONE)
        return -NEWFS_ERROR_IO;

    if (newfs_super_d.magic_num != NEWFS_MAGIC_NUM)
    {
        super_blks = 1;
        map_inode_blks = 1;
        map_data_blks = 1;
        inode_blks = NEWFS_INODE_BLKS;
        data_blks = NEWFS_DATA_BLKS;

        newfs_super.max_ino = inode_blks;
        newfs_super.max_dno = data_blks;

        newfs_super_d.map_inode_blks = map_inode_blks;
        newfs_super_d.map_data_blks = map_data_blks;

        newfs_super_d.map_inode_offset = NEWFS_SUPER_OFS + super_blks * newfs_super.sz_block;
        newfs_super_d.map_data_offset = newfs_super_d.map_inode_offset + map_inode_blks * newfs_super.sz_block;
        newfs_super_d.inode_offset = newfs_super_d.map_data_offset + map_data_blks * newfs_super.sz_block;
        newfs_super_d.data_offset = newfs_super_d.inode_offset + inode_blks * newfs_super.sz_block;

        newfs_super_d.magic_num = NEWFS_MAGIC_NUM;
        newfs_super_d.sz_usage = 0;
        is_init = TRUE;
    }

    newfs_super.sz_usage = newfs_super_d.sz_usage;
    newfs_super.map_inode = (uint8_t *)malloc(newfs_super.sz_block);
    newfs_super.map_inode_offset = newfs_super_d.map_inode_offset;
    newfs_super.map_inode_blks = newfs_super_d.map_inode_blks;

    newfs_super.map_data = (uint8_t *)malloc(newfs_super.sz_block);
    newfs_super.map_data_offset = newfs_super_d.map_data_offset;
    newfs_super.map_data_blks = newfs_super_d.map_data_blks;

    newfs_super.inode_offset = newfs_super_d.inode_offset;
    newfs_super.data_offset = newfs_super_d.data_offset;

    if (newfs_driver_read(newfs_super_d.map_inode_offset, (uint8_t *)(newfs_super.map_inode), newfs_super.sz_block) != NEWFS_ERROR_NONE)
        return -NEWFS_ERROR_IO;

    if (newfs_driver_read(newfs_super_d.map_data_offset, (uint8_t *)(newfs_super.map_data), newfs_super.sz_block) != NEWFS_ERROR_NONE)
        return -NEWFS_ERROR_IO;

    if (is_init)
    {
        root_inode = newfs_alloc_inode(root_dentry);
        newfs_sync_inode(root_inode);
    }

    root_inode = newfs_read_inode(root_dentry, NEWFS_ROOT_INO);
    root_dentry->inode = root_inode;
    newfs_super.root_dentry = root_dentry;
    newfs_super.is_mounted = TRUE;

    return ret;
}

int newfs_umount()
{
    struct newfs_super_d newfs_super_d;
    if (!newfs_super.is_mounted)
        return NEWFS_ERROR_NONE;

    newfs_sync_inode(newfs_super.root_dentry->inode);

    newfs_super_d.magic_num = NEWFS_MAGIC_NUM;
    newfs_super_d.sz_usage = newfs_super.sz_usage;
    newfs_super_d.map_inode_blks = newfs_super.map_inode_blks;
    newfs_super_d.map_inode_offset = newfs_super.map_inode_offset;
    newfs_super_d.inode_offset = newfs_super.inode_offset;
    newfs_super_d.map_data_blks = newfs_super.map_data_blks;
    newfs_super_d.map_data_offset = newfs_super.map_data_offset;
    newfs_super_d.data_offset = newfs_super.data_offset;

    if (newfs_driver_write(NEWFS_SUPER_OFS, (uint8_t *)&newfs_super_d, sizeof(struct newfs_super_d)) != NEWFS_ERROR_NONE)
        return -NEWFS_ERROR_IO;

    if (newfs_driver_write(newfs_super_d.map_inode_offset, (uint8_t *)(newfs_super.map_inode), newfs_super.sz_block) != NEWFS_ERROR_NONE)
        return -NEWFS_ERROR_IO;

    if (newfs_driver_write(newfs_super_d.map_data_offset, (uint8_t *)(newfs_super.map_data), newfs_super.sz_block) != NEWFS_ERROR_NONE)
        return -NEWFS_ERROR_IO;

    free(newfs_super.map_inode);
    free(newfs_super.map_data);

    ddriver_close(NEWFS_DRIVER());
    return NEWFS_ERROR_NONE;
}