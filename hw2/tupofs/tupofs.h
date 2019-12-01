#pragma once

#include <stdio.h>
#include <stdbool.h>

#define TFS_SECTOR_SIZE 2048
#define TFS_INODE_DATA_SIZE 2016 // TFS_SECTOR_SIZE - 32
#define TFS_MAX_BLOCKS_PER_FILE 503 // TFS_INODE_DATA_SIZE / sizeof(int) - 1
#define TFS_MAX_DIR_INODE_CHILDREN 62 // (TFS_INODE_DATA_SIZE - sizeof(int)) / 32

#define TFS_MAX_FILE_SIZE 1030144 // TFS_SECTOR_SIZE * TFS_MAX_BLOCKS_PER_FILE 

#define TFS_INODEMAP_BLOCK_IDX 1
#define TFS_DATAMAP_BLOCK_IDX 2

#define TFS_ROOT_INODE_IDX 1

typedef struct TFS_SuperBlock {
    char magic[16];
    int inode_map_size;
    int data_map_size;
} TFS_SuperBlock;

enum TFS_InodeType {
    TFS_INODE_FREE = 0,
    TFS_INODE_DIR,
    TFS_INODE_FILE,
};

typedef struct TFS_Inode_File {
    int file_size;
    // нумерация с 1 относительно начала data-блоков; null-terminated
    int used_blocks[TFS_MAX_BLOCKS_PER_FILE];
} TFS_Inode_File;

_Static_assert(sizeof(struct TFS_Inode_File) == TFS_INODE_DATA_SIZE, "");

int TFS_Inode_File_GetBlockCnt(const TFS_Inode_File* self);

typedef struct TFS_Inode_DirEnt {
    int inode_idx;
    char name[28];
} TFS_Inode_DirEnt;

_Static_assert(sizeof(struct TFS_Inode_DirEnt) == 32, "");

typedef struct TFS_Inode_Dir {
    int children_cnt;
    TFS_Inode_DirEnt entries[TFS_MAX_DIR_INODE_CHILDREN];
} TFS_Inode_Dir;

_Static_assert(sizeof(struct TFS_Inode_File) == TFS_INODE_DATA_SIZE, "");

typedef struct TFS_Inode TFS_Inode;

// WARNING! Does not write anything back to disk
TFS_Inode_DirEnt* TFS_Inode_Dir_AppendChild(TFS_Inode_Dir* self, TFS_Inode* child, const char* name);
bool TFS_Inode_Dir_DeleteChildAt(TFS_Inode_Dir* self, int idx);

int TFS_Inode_Dir_FindChildIdx(TFS_Inode_Dir* self, const char* name);
TFS_Inode_DirEnt* TFS_Inode_Dir_FindChild(TFS_Inode_Dir* self, const char* name);

typedef struct TFS_Inode {
    char type;  // TFS_InodeType
    char padding[27];
    int inode_idx;
    union {
        TFS_Inode_File file;
        TFS_Inode_Dir dir;
    };
} TFS_Inode;

_Static_assert(sizeof(struct TFS_Inode) == TFS_SECTOR_SIZE, "");

typedef struct TFS_Driver {
    TFS_SuperBlock super_block;
    FILE* file;
} TFS_Driver;

// find first cnt free bits in specified bitmap and save to free_idxes
void TFS_Bitmap_FindFree(const char* bitmap, int size, int* free_idxes, int cnt);

// bitmap[idxes] = bit
// idxes must be sorted
void TFS_Bitmap_SetBits(char* bitmap, int size, const int* idxes, int cnt, bool bit);

void TFS_Bitmap_SetBit(char* bitmap, int size, int idx, bool bit);

bool TFS_Bitmap_GetBit(const char* bitmap, int size, int idx);

// открывает файл на r+, проверяет и загружает основную информацию об ФС
// в случае create создает все
void TFS_Driver_Init(TFS_Driver* self, FILE* file, bool create);
void TFS_Driver_Destruct(TFS_Driver* self);

// читает целиком блок-сектор по адресу (с нуля)
void TFS_Driver_ReadBlock(TFS_Driver* self, int block_idx, void* buf);

// пишет целиком блок-сектор по адресу (с нуля)
void TFS_Driver_WriteBlock(TFS_Driver* self, int block_idx, const void* buf);

// нумерация с 1 относительно начала inode-блоков
int TFS_Driver_GetInodeBlockIdx(TFS_Driver* self, int inode_idx);
void TFS_Driver_GetInode(TFS_Driver* self, int inode_idx, TFS_Inode* inode);
void TFS_Driver_PutInode(TFS_Driver* self, int inode_idx, const TFS_Inode* inode);
int TFS_Driver_FindFreeInodeIdx(TFS_Driver* self);
void TFS_Driver_GetFreeInode(TFS_Driver* self, TFS_Inode* inode);
void TFS_Driver_SetInodeOccupied(TFS_Driver* self, int inode_idx, bool occupied);
void TFS_Driver_FreeInode(TFS_Driver* self, TFS_Inode* inode);
void TFS_Driver_FreeInodeByIdx(TFS_Driver* self, int inode_idx);

// нумерация с 1 относительно начала data-блоков
int TFS_Driver_GetDataBlockIdx(TFS_Driver* self, int data_idx);
void TFS_Driver_GetData(TFS_Driver* self, int data_idx, void* data);
void TFS_Driver_PutData(TFS_Driver* self, int data_idx, const void* data);
void TFS_Driver_SetDataBlockOccupied(TFS_Driver* self, int data_idx, bool occupied);
void TFS_Driver_FreeDataBlockByIdx(TFS_Driver* self, int data_idx);

// does nothing to parent inode
int TFS_Driver_CreateInode(TFS_Driver* self, TFS_Inode* inode, enum TFS_InodeType type);

// creates new inode and appends it to dir's children
int TFS_Driver_CreateChildInode(TFS_Driver* self, TFS_Inode* parent, TFS_Inode* child, const char* name, enum TFS_InodeType type);

// deletes inode and erases it from dir's children
bool TFS_Driver_DeleteChildNode(TFS_Driver* self, TFS_Inode* parent, TFS_Inode* child);

// entirely reads file by specified inode into buf and returns its size
// if buf is NULL, just returns size
int TFS_Driver_ReadFile(TFS_Driver* self, TFS_Inode* inode, void* buf);

int TFS_Driver_WriteFile(TFS_Driver* self, TFS_Inode* inode, const void* buf, int size);

// frees file inode and its' associated data blocks
// WARNING! Does not remove ref from parent inode
void TFS_Driver_RmFileInode(TFS_Driver* self, TFS_Inode* inode);

// paths

#define TFS_PATH_MAX_SIZE 50

typedef struct TFS_Path {
    int size;
    char** components;

    char* _buf;
} TFS_Path;

int TFS_Path_Init(TFS_Path* self, const char* path);
void TFS_Path_Destruct(TFS_Path* self);
int TFS_Path_TraverseSlice(const TFS_Path* self, TFS_Inode* start_inode, int begin, int end, TFS_Driver* driver);

int TFS_Driver_GetInodeByPath(TFS_Driver* self, const TFS_Path* path, TFS_Inode* inode);
int TFS_Driver_GetInodeByRawPath(TFS_Driver* self, const char* path, TFS_Inode* inode);
int TFS_Driver_GetInodeIdxByPath(TFS_Driver* self, const TFS_Path* path);
int TFS_Driver_GetInodeIdxByRawPath(TFS_Driver* self, const char* path);

// highlevel operations

// creates directory at path and returns its' inode idx or 0
int TFS_Driver_CreateByPath(TFS_Driver* self, TFS_Inode* inode, const TFS_Path* path, enum TFS_InodeType type);
int TFS_Driver_CreateByRawPath(TFS_Driver* self, TFS_Inode* inode, const char* raw_path, enum TFS_InodeType type);
int TFS_Driver_CreateIdxByRawPath(TFS_Driver* self, const char* raw_path, enum TFS_InodeType type);

int TFS_Driver_ReadFileByRawPath(TFS_Driver* self, const char* path, void* buf);

int TFS_Driver_WriteFileByRawPath(TFS_Driver* self, const char* path, const void* buf, int size);

int TFS_Driver_DeleteByPath(TFS_Driver* self, const TFS_Path* path);
int TFS_Driver_DeleteByRawPath(TFS_Driver* self, const char* path);

int TFS_Driver_MvPath(TFS_Driver* self, const TFS_Path* from_path, const TFS_Path* to_path);
int TFS_Driver_MvRawPath(TFS_Driver* self, const char* from_path, const char* to_path);
