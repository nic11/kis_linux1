/*
  based on https://github.com/libfuse/libfuse/blob/fuse-2_9_bugfix/example/hello.c
  (C) 2019 Roman Nikonov

  use cmake to compile
  use run_fuse.sh to run

    [Original copyright:]
  FUSE: Filesystem in Userspace
  Copyright (C) 2001-2007  Miklos Szeredi <miklos@szeredi.hu>

  This program can be distributed under the terms of the GNU GPL.
*/

#define FUSE_USE_VERSION 26

#include <fuse.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <memory.h>
#include <errno.h>
#include <fcntl.h>

#include "tupofs.h"

TFS_Driver* driver = NULL;

static int hello_getattr(const char *path, struct stat *stbuf)
{
    memset(stbuf, 0, sizeof(struct stat));
    if (strcmp(path, "/") == 0) {
        stbuf->st_mode = S_IFDIR | 0555;
        stbuf->st_nlink = 2;
        return 0;
    } else {
        TFS_Inode* inode = malloc(sizeof(TFS_Inode));
        int ret = TFS_Driver_GetInodeByRawPath(driver, path, inode);
        if (ret <= 0) {
            free(inode);
            return -ENOENT;
        }
        if (inode->type == TFS_INODE_DIR) {
            stbuf->st_mode = S_IFDIR | 0555;
            stbuf->st_nlink = 2;
        } else if (inode->type == TFS_INODE_FILE) {
            stbuf->st_mode = S_IFREG | 0444;
            stbuf->st_nlink = 1;
            stbuf->st_size = inode->file.file_size;
        } else {
            free(inode);
            return -ENOENT;
        }
        free(inode);
        return 0;
    }
}

static int hello_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
             off_t offset, struct fuse_file_info *fi)
{
    (void) offset;
    (void) fi;

    TFS_Inode* inode = malloc(sizeof(TFS_Inode));
    int ret = TFS_Driver_GetInodeByRawPath(driver, path, inode);
    if (ret <= 0 || inode->type != TFS_INODE_DIR) {
        free(inode);
        return -ENOENT;
    }

    filler(buf, ".", NULL, 0);
    filler(buf, "..", NULL, 0);
    for (int i = 0; i < inode->dir.children_cnt; ++i) {
        filler(buf, inode->dir.entries[i].name, NULL, 0);
    }

    return 0;
}

static int hello_open(const char *path, struct fuse_file_info *fi)
{
    TFS_Inode* inode = malloc(sizeof(TFS_Inode));
    int ret = TFS_Driver_GetInodeByRawPath(driver, path, inode);
    if (ret <= 0 || inode->type != TFS_INODE_FILE) {
        free(inode);
        return -ENOENT;
    }

    if ((fi->flags & 3) != O_RDONLY) {
        free(inode);
        return -EACCES;
    }

    return 0;
}

static int hello_read(const char *path, char *buf, size_t size, off_t offset,
              struct fuse_file_info *fi)
{
    (void) fi;
    TFS_Inode* inode = malloc(sizeof(TFS_Inode));
    int ret = TFS_Driver_GetInodeByRawPath(driver, path, inode);
    if (ret <= 0 || inode->type != TFS_INODE_FILE) {
        free(inode);
        return -ENOENT;
    }

    int to_return = inode->file.file_size - offset;
    if ((int)size < to_return) {
        to_return = size;
    }
    if (to_return <= 0) {
        return 0;
    }

    char* whole_file = malloc(inode->file.file_size);
    if (TFS_Driver_ReadFileByRawPath(driver, path, whole_file) <= 0) {
        return -EACCES;
    }
    memcpy(buf, whole_file + offset, to_return);
    return to_return;
}

static struct fuse_operations hello_oper = {
    .getattr    = hello_getattr,
    .readdir    = hello_readdir,
    .open        = hello_open,
    .read        = hello_read,
};

int main(int argc, char *argv[])
{
    FILE* f = fopen("tupofs.bin", "r+");
    if (f == NULL) {
        perror("Error opening FS host (tupofs.bin)");
        return 1;
    }
    driver = malloc(sizeof(TFS_Driver));
    TFS_Driver_Init(driver, f, false);

    return fuse_main(argc, argv, &hello_oper, NULL);
}
