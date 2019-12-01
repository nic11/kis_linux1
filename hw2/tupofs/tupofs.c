#include "tupofs.h"

#include <string.h>
#include <stdlib.h>
#include <assert.h>

#include "tfs_errs.h"

int TFS_CeilDiv(int a, int b) {
    return a / b + !!(a % b);
}

int TFS_Min(int a, int b) {
    return a < b ? a : b;
}

int TFS_Inode_File_GetBlockCnt(const TFS_Inode_File* self) {
    return TFS_CeilDiv(self->file_size, TFS_SECTOR_SIZE);
}

TFS_Inode_DirEnt* TFS_Inode_Dir_AppendChild(TFS_Inode_Dir* self, TFS_Inode* child, const char* name) {
    assert(self->children_cnt + 1 <= TFS_MAX_DIR_INODE_CHILDREN);
    if (self->children_cnt + 1 == TFS_MAX_DIR_INODE_CHILDREN) {
        return NULL;
    }
    TFS_Inode_DirEnt* new_entry = &self->entries[self->children_cnt++];
    new_entry->inode_idx = child->inode_idx;
    strcpy(new_entry->name, name);
    return new_entry;
}

bool TFS_Inode_Dir_DeleteChildAt(TFS_Inode_Dir* self, int idx) {
    assert(0 <= idx && idx < self->children_cnt);
    for (int i = idx; i < self->children_cnt - 1; ++i) {
        self->entries[i] = self->entries[i + 1];
    }
    --self->children_cnt;
    return true;
}

int TFS_Inode_Dir_FindChildIdx(TFS_Inode_Dir* self, const char* name) {
    for (int i = 0; i < self->children_cnt; ++i) {
        if (strcmp(self->entries[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

TFS_Inode_DirEnt* TFS_Inode_Dir_FindChild(TFS_Inode_Dir* self, const char* name) {
    int idx = TFS_Inode_Dir_FindChildIdx(self, name);
    return idx != -1 ? &self->entries[idx] : NULL;
}

void TFS_Bitmap_FindFree(const char* bitmap, int size, int* free_idxes, int cnt) {
    int found = 0;
    for (int i = 0; i < size; ++i) {
        int byte = bitmap[i];
        for (int bit_idx = 0; bit_idx < 8; ++bit_idx) {
            if (!(byte & (1 << bit_idx))) {
                free_idxes[found++] = i * 8 + bit_idx;
                if (found == cnt) {
                    return;
                }
            }
        }
    }
    assert(found == cnt);
}

void TFS_Bitmap_SetBits(char* bitmap, int size, const int* idxes, int cnt, bool bit) {
    int j = 0;
    for (int i = 0; i < size && j < cnt; ++i) {
        int byte = bitmap[i];
        for (int bit_idx = 0; bit_idx < 8; ++bit_idx) {
            if (i * 8 + bit_idx == idxes[j]) {
                if (bit) {
                    byte |= 1 << bit_idx;
                } else {
                    byte &= ~(1 << bit_idx);
                }
                ++j;
                if (j == cnt) {
                    break;
                }
            }
        }
        bitmap[i] = byte;
    }
    assert(j == cnt);
}

void TFS_Bitmap_SetBit(char* bitmap, int size, int idx, bool bit) {
    TFS_Bitmap_SetBits(bitmap, size, &idx, 1, bit);
}

bool TFS_Bitmap_GetBit(const char* bitmap, int size, int idx) {
    assert(idx < size * 8);
    int byte_idx = idx / 8;
    return !!(bitmap[byte_idx] & (1 << (idx - byte_idx * 8)));
}

const char TFS_MAGIC[16] = "\0\x13\x37\0TupoFS";
static char block_buf[TFS_SECTOR_SIZE];

void TFS_Driver_Init(TFS_Driver* self, FILE* file, bool create) {
    self->file = file;
    if (create) {
        // prepare clean superblock
        memset(&self->super_block, 0, sizeof(TFS_SuperBlock));
        memcpy(self->super_block.magic, TFS_MAGIC, 16);
        self->super_block.inode_map_size = TFS_SECTOR_SIZE;
        self->super_block.data_map_size = TFS_SECTOR_SIZE;

        // create and write file
        fseek(file, 0, SEEK_SET);

        // write superblock
        memcpy(block_buf, &self->super_block, sizeof(TFS_SuperBlock));
        fwrite(block_buf, TFS_SECTOR_SIZE, 1, file);

        // write rest of file 0-filled
        memset(block_buf, 0, TFS_SECTOR_SIZE);
        for (int i = 0; i < 2 + 8 * self->super_block.inode_map_size + 8 * self->super_block.data_map_size; ++i) {
            fwrite(block_buf, TFS_SECTOR_SIZE, 1, file);
        }

        // fill inode indices
        for (int i = 1; i <= 8 * self->super_block.inode_map_size; ++i) {
            TFS_Inode* inode = (TFS_Inode*)block_buf;
            int block_idx = TFS_Driver_GetInodeBlockIdx(self, i);

            TFS_Driver_ReadBlock(self, block_idx, inode);
            inode->inode_idx = i;
            TFS_Driver_PutInode(self, i, inode);
            TFS_Driver_GetInode(self, i, inode);
        }

        // create root inode
        TFS_Inode* inode = (TFS_Inode*)block_buf;
        TFS_Driver_CreateInode(self, inode, TFS_INODE_DIR);
        assert(inode->inode_idx == TFS_ROOT_INODE_IDX);
    }
    fseek(file, 0, SEEK_SET);
    fread(&self->super_block, sizeof(TFS_SuperBlock), 1, file);
}

void TFS_Driver_Destruct(TFS_Driver* self) {
    fclose(self->file);
}

void TFS_Driver_ReadBlock(TFS_Driver* self, int block_idx, void* buf) {
    int offset = block_idx * TFS_SECTOR_SIZE;
    fseek(self->file, offset, SEEK_SET);
    int read = fread(buf, TFS_SECTOR_SIZE, 1, self->file);
    assert(read == 1);
}

void TFS_Driver_WriteBlock(TFS_Driver* self, int block_idx, const void* buf) {
    int offset = block_idx * TFS_SECTOR_SIZE;
    fseek(self->file, offset, SEEK_SET);
    int written = fwrite(buf, TFS_SECTOR_SIZE, 1, self->file);
    assert(written == 1);
}

int TFS_Driver_GetInodeBlockIdx(TFS_Driver* self, int inode_idx) {
    (void)self; // unused
    assert(inode_idx);
    return 3 + inode_idx - 1;
}

void TFS_Driver_GetInode(TFS_Driver* self, int inode_idx, TFS_Inode* inode) {
    int block_idx = TFS_Driver_GetInodeBlockIdx(self, inode_idx);
    TFS_Driver_ReadBlock(self, block_idx, inode);
    assert(inode->inode_idx == inode_idx);
}

void TFS_Driver_PutInode(TFS_Driver* self, int inode_idx, const TFS_Inode* inode) {
    int block_idx = TFS_Driver_GetInodeBlockIdx(self, inode_idx);
    TFS_Driver_WriteBlock(self, block_idx, inode);
}

int TFS_Driver_FindFreeInodeIdx(TFS_Driver* self) {
    int result0;
    TFS_Driver_ReadBlock(self, TFS_INODEMAP_BLOCK_IDX, block_buf);
    TFS_Bitmap_FindFree(block_buf, self->super_block.inode_map_size, &result0, 1);
    ++result0;
    return result0;
}

void TFS_Driver_GetFreeInode(TFS_Driver* self, TFS_Inode* inode) {
    int inode_idx = TFS_Driver_FindFreeInodeIdx(self);
    TFS_Driver_GetInode(self, inode_idx, inode);
    assert(inode->type == TFS_INODE_FREE);
}

void TFS_Driver_SetInodeOccupied(TFS_Driver* self, int inode_idx, bool occupied) {
    char* inode_map = malloc(TFS_SECTOR_SIZE);
    int inode_idx0 = inode_idx - 1;
    TFS_Driver_ReadBlock(self, TFS_INODEMAP_BLOCK_IDX, inode_map);
    TFS_Bitmap_SetBits(inode_map, self->super_block.inode_map_size, &inode_idx0, 1, occupied);
    TFS_Driver_WriteBlock(self, TFS_INODEMAP_BLOCK_IDX, inode_map);
    free(inode_map);
}

void TFS_Driver_FreeInode(TFS_Driver* self, TFS_Inode* inode) {
    TFS_Driver_SetInodeOccupied(self, inode->inode_idx, false);

    inode->type = TFS_INODE_FREE;
    TFS_Driver_PutInode(self, inode->inode_idx, inode);
}

void TFS_Driver_FreeInodeByIdx(TFS_Driver* self, int inode_idx) {
    TFS_Inode* inode = malloc(sizeof(TFS_Inode));
    TFS_Driver_GetInode(self, inode_idx, inode);
    TFS_Driver_FreeInode(self, inode);
    // FreeInode also puts it
    free(inode);
}

int TFS_Driver_GetDataBlockIdx(TFS_Driver* self, int data_idx) {
    assert(data_idx);
    return 3 + 8 * self->super_block.inode_map_size + data_idx - 1;
}

void TFS_Driver_GetData(TFS_Driver* self, int data_idx, void* data) {
    int block_idx = TFS_Driver_GetDataBlockIdx(self, data_idx);
    TFS_Driver_ReadBlock(self, block_idx, data);
}

void TFS_Driver_PutData(TFS_Driver* self, int data_idx, const void* data) {
    int block_idx = TFS_Driver_GetDataBlockIdx(self, data_idx);
    TFS_Driver_WriteBlock(self, block_idx, data);
}

int TFS_Driver_CreateInode(TFS_Driver* self, TFS_Inode* inode, enum TFS_InodeType type) {
    TFS_Driver_GetFreeInode(self, inode);
    inode->type = type;
    switch (type) {
        case TFS_INODE_DIR:
            inode->dir.children_cnt = 0;
            break;
        case TFS_INODE_FILE:
            inode->file.file_size = 0;
            break;
        default:
            assert(false);
    }
    TFS_Driver_PutInode(self, inode->inode_idx, inode);
    TFS_Driver_SetInodeOccupied(self, inode->inode_idx, true);
    return inode->inode_idx;
}

int TFS_Driver_CreateChildInode(TFS_Driver* self, TFS_Inode* parent, TFS_Inode* child, const char* name, enum TFS_InodeType type) {
    if (parent->type != TFS_INODE_DIR) {
        return TFS_ENOENT;
    }
    TFS_Driver_CreateInode(self, child, type); // assign child
    if (TFS_Inode_Dir_FindChildIdx(&parent->dir, name) != -1) {
        return TFS_ENOENT;
    }
    if (TFS_Inode_Dir_AppendChild(&parent->dir, child, name) == NULL) {
        return TFS_ENOENT;
    }
    TFS_Driver_PutInode(self, parent->inode_idx, parent);
    return child->inode_idx;
}

int TFS_Driver_ReadFile(TFS_Driver* self, TFS_Inode* inode, void* buf) {
    assert(inode->type == TFS_INODE_FILE);
    int size = inode->file.file_size;

    if (buf == NULL) {
        return size;
    }

    int blocks = TFS_CeilDiv(size, TFS_SECTOR_SIZE);

    for (int i = 0, size_left = size; i < blocks; ++i, size_left -= TFS_SECTOR_SIZE) {
        TFS_Driver_GetData(self, inode->file.used_blocks[i], block_buf);
        memcpy(buf + i * TFS_SECTOR_SIZE, block_buf, TFS_Min(size_left, TFS_SECTOR_SIZE));
    }

    return size;
}

int TFS_Driver_WriteFile(TFS_Driver* self, TFS_Inode* inode, const void* buf, const int size) {
    int need_blocks = TFS_CeilDiv(size, TFS_SECTOR_SIZE);
    char* datamap = malloc(TFS_SECTOR_SIZE);
    int datamap_size = self->super_block.data_map_size;
    int* free_idxes0 = malloc(sizeof(int) * (need_blocks + 1));

    inode->type = TFS_INODE_FILE;

    TFS_Driver_ReadBlock(self, TFS_DATAMAP_BLOCK_IDX, datamap);
    TFS_Bitmap_FindFree(datamap, datamap_size, free_idxes0, need_blocks);

    for (int i = 0, size_left = size; i < need_blocks; ++i, size_left -= TFS_SECTOR_SIZE) {
        memcpy(block_buf, buf + i * TFS_SECTOR_SIZE, TFS_Min(size_left, TFS_SECTOR_SIZE));
        TFS_Driver_PutData(self, free_idxes0[i] + 1, block_buf);
        inode->file.used_blocks[i] = free_idxes0[i] + 1;
    }

    TFS_Bitmap_SetBits(datamap, datamap_size, free_idxes0, need_blocks, 1);
    TFS_Driver_WriteBlock(self, TFS_DATAMAP_BLOCK_IDX, datamap);

    inode->file.file_size = size;
    TFS_Driver_PutInode(self, inode->inode_idx, inode);
    TFS_Driver_SetInodeOccupied(self, inode->inode_idx, true);

    free(datamap);
    free(free_idxes0);

    return size;
}

void TFS_Driver_RmFileInode(TFS_Driver* self, TFS_Inode* inode) {
    assert(inode->type == TFS_INODE_FILE);
    int block_cnt = TFS_Inode_File_GetBlockCnt(&inode->file);
    int* data_blocks0 = malloc(sizeof(int) * block_cnt);
    for (int i = 0; i < block_cnt; ++i) {
        data_blocks0[i] = inode->file.used_blocks[i] - 1;
    }
    char* datamap = malloc(TFS_SECTOR_SIZE);
    int datamap_size = self->super_block.data_map_size;
    TFS_Driver_ReadBlock(self, TFS_DATAMAP_BLOCK_IDX, datamap);
    TFS_Bitmap_SetBits(datamap, datamap_size, data_blocks0, block_cnt, false);
    TFS_Driver_WriteBlock(self, TFS_DATAMAP_BLOCK_IDX, datamap);

    free(data_blocks0);
    free(datamap);
    TFS_Driver_FreeInode(self, inode);
}

int TFS_Path_Init(TFS_Path* self, const char* path_orig) {
    if (path_orig[0] != '/') {
        self->size = 0;
        return TFS_ENOENT;
    }

    char* path = malloc(strlen(path_orig) + 1);
    strcpy(path, path_orig);
    self->_buf = path;

    self->components = malloc(sizeof(char*) * TFS_PATH_MAX_SIZE);
    int i = 0;

    char* state;
    char* token = strtok_r(path, "/", &state);
    while (token != NULL) {
        if (i + 1 == TFS_PATH_MAX_SIZE) {
            free(path);
            free(self->components);
            return TFS_ENOENT;
        }
        self->components[i++] = token;

        token = strtok_r(NULL, "/", &state);
    }
    self->size = i;
    return TFS_ESUCC;
}

void TFS_Path_Destruct(TFS_Path* self) {
    free(self->components);
    free(self->_buf);
}

int TFS_Path_TraverseSlice(const TFS_Path* path, TFS_Inode* inode, int begin, int end, TFS_Driver* driver) {
    // TODO: differ '/' from unexisting path
    // -1?
    // if (path->size == 0) {
    //     return false;
    // }

    if (path->size < 0) {
        return TFS_ENOENT;
    }
    if (!(0 <= begin && begin <= end && end <= path->size)) {
        return TFS_ENOENT;
    }
    for (int i = begin; i < end; ++i) {
        assert(inode != NULL);
        if (inode->type != TFS_INODE_DIR) {
            return TFS_ENOENT;
        }

        TFS_Inode_DirEnt* dirent = TFS_Inode_Dir_FindChild(&inode->dir, path->components[i]);
        if (dirent == NULL) {
            return TFS_ENOENT;
        }
        TFS_Driver_GetInode(driver, dirent->inode_idx, inode);
    }
    return inode->inode_idx;
}

int TFS_Driver_GetInodeByPath(TFS_Driver* self, const TFS_Path* path, TFS_Inode* inode) {
    TFS_Driver_GetInode(self, TFS_ROOT_INODE_IDX, inode);
    int inode_idx = TFS_Path_TraverseSlice(path, inode, 0, path->size, self);
    return inode_idx;
}

int TFS_Driver_GetInodeByRawPath(TFS_Driver* self, const char* raw_path, TFS_Inode* inode) {
    TFS_Path path;
    int path_init_code = TFS_Path_Init(&path, raw_path);
    if (path_init_code <= 0) {
        return path_init_code;
    }
    int inode_idx = TFS_Driver_GetInodeByPath(self, &path, inode);
    TFS_Path_Destruct(&path);
    return inode_idx;
}

int TFS_Driver_GetInodeIdxByPath(TFS_Driver* self, const TFS_Path* path) {
    TFS_Inode* inode = malloc(sizeof(TFS_Inode));
    int inode_idx = TFS_Driver_GetInodeByPath(self, path, inode);
    free(inode);
    return inode_idx;
}

int TFS_Driver_GetInodeIdxByRawPath(TFS_Driver* self, const char* raw_path) {
    TFS_Path path;
    int path_init_code = TFS_Path_Init(&path, raw_path);
    if (path_init_code <= 0) {
        return path_init_code;
    }
    int inode_idx = TFS_Driver_GetInodeIdxByPath(self, &path);
    TFS_Path_Destruct(&path);
    return inode_idx;
}

int TFS_Driver_CreateByPath(TFS_Driver* self, TFS_Inode* inode, const TFS_Path* path, enum TFS_InodeType type) {
    if (path->size < 1) {
        return TFS_EEXISTS;
    }

    TFS_Driver_GetInode(self, TFS_ROOT_INODE_IDX, inode);
    int inode_idx = TFS_Path_TraverseSlice(path, inode, 0, path->size - 1, self);
    if (inode_idx <= 0) {
        return inode_idx;
    }

    TFS_Inode* child = malloc(sizeof(TFS_Inode));
    inode_idx = TFS_Driver_CreateChildInode(self, inode, child, path->components[path->size - 1], type);
    if (inode_idx <= 0) {
        free(child);
        return inode_idx;
    }

    *inode = *child;
    assert(inode->inode_idx == inode_idx);

    free(child);
    return inode_idx;
}

int TFS_Driver_CreateByRawPath(TFS_Driver* self, TFS_Inode* inode, const char* raw_path, enum TFS_InodeType type) {
    TFS_Path path;
    int path_init_code = TFS_Path_Init(&path, raw_path);
    if (path_init_code <= 0) {
        return path_init_code;
    }
    int inode_idx = TFS_Driver_CreateByPath(self, inode, &path, type);
    TFS_Path_Destruct(&path);
    return inode_idx;
}

int TFS_Driver_CreateIdxByRawPath(TFS_Driver* self, const char* raw_path, enum TFS_InodeType type) {
    TFS_Inode* inode = malloc(sizeof(TFS_Inode));
    int inode_idx = TFS_Driver_CreateByRawPath(self, inode, raw_path, type);
    free(inode);
    return inode_idx;
}

int TFS_Driver_ReadFileByRawPath(TFS_Driver* self, const char* path, void* buf) {
    TFS_Inode* inode = malloc(sizeof(TFS_Inode));
    int inode_idx = TFS_Driver_GetInodeByRawPath(self, path, inode);
    int read = inode_idx != 0? TFS_Driver_ReadFile(self, inode, buf) : -1;
    free(inode);
    return read;
}

int TFS_Driver_WriteFileByRawPath(TFS_Driver* self, const char* path, const void* buf, int size) {
    TFS_Inode* inode = malloc(sizeof(TFS_Inode));
    int inode_idx = TFS_Driver_GetInodeByRawPath(self, path, inode);
    int written = inode_idx != 0? TFS_Driver_WriteFile(self, inode, buf, size) : -1;
    free(inode);
    return written;
}

int TFS_Driver_DeleteByPath(TFS_Driver* self, const TFS_Path* path) {
    if (path->size < 1) {
        return TFS_ENOENT;
    }

    TFS_Inode* inode = malloc(sizeof(TFS_Inode));
    TFS_Driver_GetInode(self, TFS_ROOT_INODE_IDX, inode);
    int inode_idx = TFS_Path_TraverseSlice(path, inode, 0, path->size - 1, self);
    if (inode_idx <= 0 || inode->type != TFS_INODE_DIR) {
        free(inode);
        return TFS_ENOENT; // if not enoent?
    }

    int dirent_idx = TFS_Inode_Dir_FindChildIdx(&inode->dir, path->components[path->size - 1]);
    if (dirent_idx == -1) {
        free(inode);
        return TFS_ENOENT;
    }
    
    TFS_Inode* child = malloc(sizeof(TFS_Inode));
    int child_idx = inode->dir.entries[dirent_idx].inode_idx;
    TFS_Driver_GetInode(self, child_idx, child);

    switch (child->type) {
        case TFS_INODE_DIR:
            if (child->dir.children_cnt != 0) {
                free(inode);
                free(child);
                return 0;
            }
            break;
        case TFS_INODE_FILE:
            TFS_Driver_RmFileInode(self, child);
            break;
        default:
            assert(false);
    }

    TFS_Driver_FreeInode(self, child); // XXX: duplicate call for file
    assert(TFS_Inode_Dir_DeleteChildAt(&inode->dir, dirent_idx));
    TFS_Driver_PutInode(self, inode_idx, inode);

    free(inode);
    free(child);
    return child_idx;
}

int TFS_Driver_DeleteByRawPath(TFS_Driver* self, const char* raw_path) {
    TFS_Path path;
    int path_init_code = TFS_Path_Init(&path, raw_path);
    if (path_init_code <= 0) {
        return path_init_code;
    }
    int result = TFS_Driver_DeleteByPath(self, &path);
    TFS_Path_Destruct(&path);
    return result;
}

int TFS_Driver_MvPath(TFS_Driver* self, const TFS_Path* from_path, const TFS_Path* to_path) {
    if (from_path->size < 1 || to_path->size < 1) {
        return TFS_ENOENT;
    }
    TFS_Inode* inodes = malloc(sizeof(TFS_Inode) * 4);

    TFS_Inode* from_parent = inodes;
    //TFS_Inode* from_child = inodes + 1;
    TFS_Inode* to_parent = inodes + 2;
    //TFS_Inode* to_child = inodes + 3;

    TFS_Driver_GetInode(self, TFS_ROOT_INODE_IDX, from_parent);
    int from_parent_idx = TFS_Path_TraverseSlice(from_path, from_parent, 0, from_path->size - 1, self);
    if (from_parent_idx == 0 || from_parent->type != TFS_INODE_DIR) {
        free(inodes);
        return TFS_ENOENT;
    }

    TFS_Driver_GetInode(self, TFS_ROOT_INODE_IDX, to_parent);
    int to_parent_idx = TFS_Path_TraverseSlice(to_path, to_parent, 0, to_path->size - 1, self);
    if (to_parent_idx == 0 || to_parent->type != TFS_INODE_DIR) {
        free(inodes);
        return TFS_ENOENT;
    }

    if (from_parent_idx != to_parent_idx) {
        // TODO: not implemented
        free(inodes);
        return TFS_ENOENT;
    }

    TFS_Inode_DirEnt* dirent = TFS_Inode_Dir_FindChild(&from_parent->dir, from_path->components[from_path->size - 1]);
    strcpy(dirent->name, to_path->components[to_path->size - 1]);
    TFS_Driver_PutInode(self, from_parent_idx, from_parent);

    int result = dirent->inode_idx;
    free(inodes);
    return result;
}

int TFS_Driver_MvRawPath(TFS_Driver* self, const char* from_path_raw, const char* to_path_raw) {
    TFS_Path from_path, to_path;
    int path_init_code = TFS_Path_Init(&from_path, from_path_raw);
    if (path_init_code <= 0) {
        return path_init_code;
    }
    path_init_code = TFS_Path_Init(&to_path, to_path_raw);
    if (path_init_code <= 0) {
        return path_init_code;
    }
    int result = TFS_Driver_MvPath(self, &from_path, &to_path);
    TFS_Path_Destruct(&from_path);
    TFS_Path_Destruct(&to_path);
    return result;
}
