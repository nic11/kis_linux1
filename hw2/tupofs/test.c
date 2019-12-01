#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <memory.h>
#include <assert.h>

#include "tupofs.h"
#include "tfs_errs.h"

TFS_Driver* TFS_Test_Init() {
    FILE* fs_host = fopen("tupofs_test.bin", "wb+");
    TFS_Driver* driver = malloc(sizeof(TFS_Driver));
    TFS_Driver_Init(driver, fs_host, true);

    printf("first inode block = %d\n", TFS_Driver_GetInodeBlockIdx(driver, 1));
    printf("first inode addr = %x\n", TFS_Driver_GetInodeBlockIdx(driver, 1) * TFS_SECTOR_SIZE);
    printf("first data block = %d\n", TFS_Driver_GetDataBlockIdx(driver, 1));
    printf("first data addr = %x\n", TFS_Driver_GetDataBlockIdx(driver, 1) * TFS_SECTOR_SIZE);

    return driver;
}

void TFS_Test_Finish(TFS_Driver* driver) {
    fclose(driver->file);
    free(driver);
}

void TFS_TestDataNodesManagement() {
    TFS_Driver* driver = TFS_Test_Init();

    TFS_Inode* inode = malloc(sizeof(TFS_Inode));
    char file_content[200];
    for (int i = 0; i < 5; ++i) {
        int size = sprintf(file_content, "bakabaka testing filesystem / %d\n", i);
        TFS_Driver_GetFreeInode(driver, inode);
        TFS_Driver_WriteFile(driver, inode, file_content, size);
        TFS_Driver_GetInode(driver, inode->inode_idx, inode);
        assert(inode->type == TFS_INODE_FILE);
    }

    TFS_Driver_FreeInodeByIdx(driver, 5);
    TFS_Driver_FreeInodeByIdx(driver, 3);

    TFS_Driver_GetFreeInode(driver, inode);
    assert(inode->inode_idx == 3);
    TFS_Driver_WriteFile(driver, inode, "hi", 2);

    TFS_Driver_GetFreeInode(driver, inode);
    assert(inode->inode_idx == 5);
    TFS_Driver_WriteFile(driver, inode, "hi", 2);

    free(inode);

    TFS_Test_Finish(driver);
}

#define TFS_TESTBITMAP_SET(BIT, ...) \
    do { \
        const int idxes[] = {__VA_ARGS__}; \
        const int cnt = sizeof(idxes) / sizeof(int); \
        TFS_Bitmap_SetBits(bitmap, 64, idxes, cnt, BIT); \
        for (int i = 0; i < cnt; ++i) { \
            assert(TFS_Bitmap_GetBit(bitmap, 64, idxes[i]) == BIT); \
        } \
    } while (0)

#define TFS_TESTBITMAP_TEST(BIT, ...) \
    do { \
        const int idxes[] = {__VA_ARGS__}; \
        const int cnt = sizeof(idxes) / sizeof(int); \
        for (int i = 0; i < cnt; ++i) { \
            assert(TFS_Bitmap_GetBit(bitmap, 64, idxes[i]) == BIT); \
        } \
    } while (0)

void TFS_TestBitmap() {
    char bitmap[64] = {};

    TFS_TESTBITMAP_SET(0, 0, 7, 128, 134, 300, 511);
    TFS_TESTBITMAP_SET(1, 0, 7, 128, 134, 300, 511);
    TFS_TESTBITMAP_TEST(1, 0, 134, 511);
    TFS_TESTBITMAP_SET(0, 7, 128, 300); 
    TFS_TESTBITMAP_TEST(1, 0, 134, 511);
}

void TFS_TestMultiblock() {
    TFS_Driver* driver = TFS_Test_Init();
    TFS_Inode* inode = malloc(sizeof(TFS_Inode));

    char file_content[TFS_SECTOR_SIZE * 3] = {};
    strcpy(file_content + TFS_SECTOR_SIZE, "Some extra content");
    file_content[TFS_SECTOR_SIZE * 2 - 1] = '#';

    // write a bunch of 2-block files
    for (int i = 0; i < 10; ++i) {
        sprintf(file_content, "bakabaka testing filesystem / %d\n", i);
        TFS_Driver_GetFreeInode(driver, inode);
        TFS_Driver_WriteFile(driver, inode, file_content, TFS_SECTOR_SIZE * 2);
        TFS_Driver_GetInode(driver, inode->inode_idx, inode);
        assert(inode->type == TFS_INODE_FILE);
        assert(inode->file.used_blocks[0] != 0);
        assert(inode->file.used_blocks[1] != 0);
        assert(inode->file.used_blocks[2] == 0);
    }

    // free some files in between
    TFS_Driver_GetInode(driver, 2, inode);
    TFS_Driver_RmFileInode(driver, inode);

    TFS_Driver_GetInode(driver, 9, inode);
    TFS_Driver_RmFileInode(driver, inode);

    // check datamap
    char* datamap = malloc(TFS_SECTOR_SIZE);
    int datamap_size = driver->super_block.data_map_size;
    TFS_Driver_ReadBlock(driver, TFS_DATAMAP_BLOCK_IDX, datamap);
    assert(TFS_Bitmap_GetBit(datamap, datamap_size, 0) == 0);
    assert(TFS_Bitmap_GetBit(datamap, datamap_size, 1) == 0);
    assert(TFS_Bitmap_GetBit(datamap, datamap_size, 2) == 1);
    assert(TFS_Bitmap_GetBit(datamap, datamap_size, 3) == 1);
    assert(TFS_Bitmap_GetBit(datamap, datamap_size, 14) == 0);
    assert(TFS_Bitmap_GetBit(datamap, datamap_size, 15) == 0);

    // write 3-block file
    strcpy(file_content + TFS_SECTOR_SIZE * 2, "MORE EXTRA CONTENT");
    file_content[TFS_SECTOR_SIZE * 3 - 1] = '#';
    TFS_Driver_GetFreeInode(driver, inode);
    TFS_Driver_WriteFile(driver, inode, file_content, TFS_SECTOR_SIZE * 3);

    // refresh and check datamap
    TFS_Driver_ReadBlock(driver, TFS_DATAMAP_BLOCK_IDX, datamap);
    assert(TFS_Bitmap_GetBit(datamap, datamap_size, 0) == 1); // 1
    assert(TFS_Bitmap_GetBit(datamap, datamap_size, 1) == 1); // 2
    assert(TFS_Bitmap_GetBit(datamap, datamap_size, 2) == 1);
    assert(TFS_Bitmap_GetBit(datamap, datamap_size, 3) == 1);
    assert(TFS_Bitmap_GetBit(datamap, datamap_size, 14) == 1); // 3
    assert(TFS_Bitmap_GetBit(datamap, datamap_size, 15) == 0);

    // try to read
    char* read_content = malloc(TFS_SECTOR_SIZE * 3);
    TFS_Driver_GetInode(driver, inode->inode_idx, inode); // re-read inode just in case
    TFS_Driver_ReadFile(driver, inode, read_content);
    assert(memcmp(file_content, read_content, TFS_SECTOR_SIZE * 3) == 0);

    free(read_content);
    free(inode);
    free(datamap);
    TFS_Test_Finish(driver);
}

void TFS_TestUnevenFileSize() {
    TFS_Driver* driver = TFS_Test_Init();
    TFS_Inode* inode = malloc(sizeof(TFS_Inode));

    // filling buffer with data
    char file_content[TFS_SECTOR_SIZE * 2];
    memset(file_content, '#', TFS_SECTOR_SIZE * 2);

    // but not writing all of it
    const int file_size = 2077;
    TFS_Driver_GetFreeInode(driver, inode);
    TFS_Driver_WriteFile(driver, inode, file_content, file_size);

    // checking that only those bytes will be actually read
    char read_content[TFS_SECTOR_SIZE * 2] = {};
    int read_bytes = TFS_Driver_ReadFile(driver, inode, read_content);
    assert(read_bytes == file_size);
    assert(memcmp(file_content, read_content, file_size) == 0);
    assert(read_content[file_size] == 0);
    assert(read_content[TFS_SECTOR_SIZE * 2 - 1] == 0);

    free(inode);
    TFS_Test_Finish(driver);
}

void TFS_TestPath() {
    TFS_Path path;
    TFS_Path_Init(&path, "/usr/lib/baka/bakalib.so.7");

    printf("path.size = %d\n", path.size);
    for (int i = 0; i < path.size; ++i) {
        printf("path.components[%d] = %s\n", i, path.components[i]);
    }

    assert(path.size == 4);
    assert(strcmp(path.components[0], "usr") == 0);
    assert(strcmp(path.components[1], "lib") == 0);
    assert(strcmp(path.components[2], "baka") == 0);
    assert(strcmp(path.components[3], "bakalib.so.7") == 0);

    TFS_Path_Destruct(&path);
}

void TFS_TestCreateChildInode() {
    TFS_Driver* driver = TFS_Test_Init();
    TFS_Inode* inodes = malloc(sizeof(TFS_Inode) * 4);
    TFS_Inode* inode_root = inodes + 0;
    TFS_Inode* inode_foo = inodes + 1;
    TFS_Inode* inode_bar = inodes + 2;
    TFS_Inode* inode_bar_baz = inodes + 3;

    TFS_Driver_GetInode(driver, TFS_ROOT_INODE_IDX, inode_root);
    int foo_idx = TFS_Driver_CreateChildInode(driver, inode_root, inode_foo, "foo", TFS_INODE_DIR);
    int bar_idx = TFS_Driver_CreateChildInode(driver, inode_root, inode_bar, "bar", TFS_INODE_DIR);
    int baz_idx = TFS_Driver_CreateChildInode(driver, inode_bar, inode_bar_baz, "baz", TFS_INODE_FILE);

    TFS_Driver_GetInode(driver, foo_idx, inode_foo);
    TFS_Driver_GetInode(driver, bar_idx, inode_bar);
    TFS_Driver_GetInode(driver, baz_idx, inode_bar_baz);
    
    assert(inode_root->dir.children_cnt == 2);
    assert(inode_root->dir.entries[0].inode_idx == inode_foo->inode_idx);
    assert(inode_root->dir.entries[1].inode_idx == inode_bar->inode_idx);
    assert(TFS_Inode_Dir_FindChild(&inode_root->dir, "foo")->inode_idx == inode_foo->inode_idx);
    assert(TFS_Inode_Dir_FindChild(&inode_root->dir, "bar")->inode_idx == inode_bar->inode_idx);

    assert(inode_bar->dir.children_cnt == 1);
    assert(inode_bar->dir.entries[0].inode_idx == inode_bar_baz->inode_idx);
    assert(TFS_Inode_Dir_FindChild(&inode_bar->dir, "baz")->inode_idx == inode_bar_baz->inode_idx);

    free(inodes);
    TFS_Test_Finish(driver);
}

void TFS_TestPathWalk() {
    TFS_Driver* driver = TFS_Test_Init();
    TFS_Inode* inode = malloc(sizeof(TFS_Inode));
    TFS_Path path;
    TFS_Path_Init(&path, "/");

    assert(TFS_Driver_GetInodeByPath(driver, &path, inode) == TFS_ROOT_INODE_IDX);

    TFS_Path_Destruct(&path);
    free(inode);
    TFS_Test_Finish(driver);
}

void TFS_TestCreateByPath() {
    TFS_Driver* driver = TFS_Test_Init();
    TFS_Inode* inode = malloc(sizeof(TFS_Inode));

    int foo_idx = TFS_Driver_CreateIdxByRawPath(driver, "/foo", TFS_INODE_DIR);
    int bar_idx = TFS_Driver_CreateIdxByRawPath(driver, "/bar", TFS_INODE_DIR);
    int baz_idx = TFS_Driver_CreateIdxByRawPath(driver, "/bar/baz", TFS_INODE_FILE);

    int bad_idx = TFS_Driver_CreateIdxByRawPath(driver, "/not_exists/puf", TFS_INODE_DIR);
    assert(bad_idx == TFS_ENOENT);

    assert(TFS_Driver_GetInodeIdxByRawPath(driver, "/foo") == foo_idx);
    assert(TFS_Driver_GetInodeIdxByRawPath(driver, "/bar") == bar_idx);
    assert(TFS_Driver_GetInodeIdxByRawPath(driver, "/bar/baz") == baz_idx);

    free(inode);
    TFS_Test_Finish(driver);
}

void TFS_TestBasicFileOps() {
    TFS_Driver* driver = TFS_Test_Init();
    TFS_Inode* inode = malloc(sizeof(TFS_Inode));
    char* buf = malloc(TFS_SECTOR_SIZE);

    int idx_foo = TFS_Driver_CreateIdxByRawPath(driver, "/foo", TFS_INODE_DIR);
    int idx_bar = TFS_Driver_CreateIdxByRawPath(driver, "/foo/bar", TFS_INODE_DIR);
    int idx_baz = TFS_Driver_CreateIdxByRawPath(driver, "/foo/bar/hardbass", TFS_INODE_FILE);

    assert(TFS_Driver_MvRawPath(driver, "/foo/bar/hardbass", "/foo/bar/baz") == idx_baz);

    TFS_Driver_WriteFileByRawPath(driver, "/foo/bar/baz", "test", 4);
    assert(TFS_Driver_ReadFileByRawPath(driver, "/foo/bar/baz", buf) == 4);
    assert(memcmp(buf, "test", 4) == 0);

    assert(TFS_Driver_DeleteByRawPath(driver, "/foo") == 0);
    assert(TFS_Driver_DeleteByRawPath(driver, "/foo/bar") == 0);
    assert(TFS_Driver_DeleteByRawPath(driver, "/foo/bar/baz") == idx_baz);
    assert(TFS_Driver_DeleteByRawPath(driver, "/foo/bar") == idx_bar);
    assert(TFS_Driver_DeleteByRawPath(driver, "/foo") == idx_foo);

    TFS_Driver_ReadBlock(driver, TFS_INODEMAP_BLOCK_IDX, buf);
    assert(buf[0] == 1);
    TFS_Driver_ReadBlock(driver, TFS_DATAMAP_BLOCK_IDX, buf);
    assert(buf[0] == 0);

    free(buf);
    free(inode);
    TFS_Test_Finish(driver);
}

int main() {
    TFS_TestBitmap();
    TFS_TestDataNodesManagement();
    TFS_TestMultiblock();
    TFS_TestUnevenFileSize();
    TFS_TestPath();
    TFS_TestCreateChildInode();
    TFS_TestPathWalk();
    TFS_TestCreateByPath();
    TFS_TestBasicFileOps();
    // TODO: error handling
    // create child for non-dir

    return 0;
}
