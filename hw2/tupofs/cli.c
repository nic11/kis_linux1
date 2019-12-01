#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <memory.h>
#include <assert.h>

#include "tupofs.h"

TFS_Driver* driver = NULL;

#define CHECK_OPEN do { \
        if (driver == NULL) { \
            fprintf(stderr, "FS must be initialized! Use open <file>\n"); \
            return; \
        } \
    } while (0)

void cmd_open(const char* holder_path) {
    FILE* file = fopen(holder_path, "r+");
    if (file == NULL) {
        perror("Couldn't open holder file");
    }
    driver = malloc(sizeof(TFS_Driver));
    TFS_Driver_Init(driver, file, false);
}

void print_inode(int idx) {
    CHECK_OPEN;

    TFS_Inode* inode = malloc(sizeof(TFS_Inode));
    TFS_Driver_GetInode(driver, idx, inode); // TODO: prepare for errcodes
    printf("inode %d\n", idx);
    printf("type=%d ", inode->type);
    switch (inode->type) {
        case TFS_INODE_FREE:
            printf("[free]\n");
            break;
        case TFS_INODE_DIR:
            printf("[dir]\n");
            printf("dir->children_cnt=%d\n", inode->dir.children_cnt);
            break;
        case TFS_INODE_FILE:
            printf("[file]\n");
            printf("file->size=%d\n", inode->file.file_size);
            break;
        default:
            printf("[UNKNOWN!]\n");
    }
    free(inode);
}

void cmd_mk(const char* path, enum TFS_InodeType type) {
    if (path == NULL) {
        printf("Usage: cmd <path>\n");
        return;
    }

    CHECK_OPEN;

    int inode_idx = TFS_Driver_CreateIdxByRawPath(driver, path, type);
    if (inode_idx > 0) {
        printf("Created inode %d\n", inode_idx);
    } else {
        printf("Error\n");
    }
}

void cmd_ls(const char* path) {
    if (path == NULL) {
        printf("Usage: ls <path>\n");
        return;
    }

    CHECK_OPEN;

    TFS_Inode* inode = malloc(sizeof(TFS_Inode));
    if (TFS_Driver_GetInodeByRawPath(driver, path, inode) <= 0) {
        printf("Error\n");
        free(inode);
        return;
    }

    if (inode->type != TFS_INODE_DIR) {
        printf("Not a directory\n");
        free(inode);
        return;
    }

    for (int i = 0; i < inode->dir.children_cnt; ++i) {
        TFS_Inode_DirEnt* ent = &inode->dir.entries[i];
        printf("%d %s\n", ent->inode_idx, ent->name);
    }

    free(inode);
}

void cmd_rm(const char* path/*, enum TFS_InodeType type*/) {
    if (path == NULL) {
        printf("Usage: rm <path>\n");
        return;
    }

    CHECK_OPEN;

    int inode_idx = TFS_Driver_DeleteByRawPath(driver, path);
    if (inode_idx > 0) {
        printf("Removed inode %d\n", inode_idx);
    } else {
        printf("Error\n");
    }
}

void cmd_put(char* from, char* to) {
    if (to == NULL) {
        printf("Usage: put <local path> <tupofs path>\n");
        return;
    }

    CHECK_OPEN;

    FILE* file = fopen(from, "rb");
    if (file == NULL) {
        perror("Couldn't open source file");
        return;
    }
    fseek(file, 0, SEEK_END);
    int file_size = ftell(file);
    fseek(file, 0, SEEK_SET);
    char* buf = malloc(file_size + 1);
    int read = fread(buf, file_size, 1, file);
    fclose(file);
    if (read != 1) {
        perror("Couldn't read source file");
        return;
    }

    TFS_Inode inode;

    int inode_idx = TFS_Driver_CreateByRawPath(driver, &inode, to, TFS_INODE_FILE);
    if (inode_idx <= 0) {
        printf("Error\n");
        return;
    }
    int written = TFS_Driver_WriteFile(driver, &inode, buf, file_size);
    if (written <= 0) {
        printf("Error\n");
        return;
    }
}

void cmd_cat(char* path, FILE* to) {
    if (path == NULL) {
        printf("Usage: cat <path>\n");
        return;
    }

    CHECK_OPEN;

    int size = TFS_Driver_ReadFileByRawPath(driver, path, NULL);
    if (size < 0) {
        printf("Error\n");
        return;
    }
    char* buf = malloc(size);
    TFS_Driver_ReadFileByRawPath(driver, path, buf);
    fwrite(buf, size, 1, to);
    free(buf);
}

void cmd_get(char* from, char* to) {
    if (to == NULL) {
        printf("Usage: get <tupofs path> <local path>\n");
        return;
    }
    assert(from != NULL);

    CHECK_OPEN;

    FILE* file = fopen(to, "wb");
    if (file == NULL) {
        perror("Couldn't open source file");
        return;
    }

    cmd_cat(from, file); // TODO: don't create file on failure
    fclose(file);
}


char* read_cmd() {
    static char* line = NULL;
    static size_t len = 0;
    ssize_t read;

    printf("> ");
    fflush(stdout);
    read = getline(&line, &len, stdin);
    if (read == -1) {
        free(line);
        return NULL;
    }
    return line;
}

void handle_cmd(char* cmd) {
    const char* delim = " \n";
    char* state;
    char* token = strtok_r(cmd, delim, &state);
    if (strcmp(token, "open") == 0) {
        token = strtok_r(NULL, delim, &state);
        cmd_open(token);
    } else if (strcmp(token, "inode") == 0) {
        token = strtok_r(NULL, delim, &state);
        int idx = atoi(token); // TODO: strtol
        print_inode(idx);
    } else if (strcmp(token, "mkdir") == 0) {
        token = strtok_r(NULL, delim, &state);
        cmd_mk(token, TFS_INODE_DIR);
    } else if (strcmp(token, "ls") == 0) {
        token = strtok_r(NULL, delim, &state);
        cmd_ls(token);
    } else if (strcmp(token, "touch") == 0) { // XXX: duplicates
        token = strtok_r(NULL, delim, &state);
        cmd_mk(token, TFS_INODE_FILE);
    } else if (strcmp(token, "rm") == 0) {
        token = strtok_r(NULL, delim, &state);
        cmd_rm(token);
    } else if (strcmp(token, "rmdir") == 0) { // TODO: check type ==
        token = strtok_r(NULL, delim, &state);
        cmd_rm(token);
    } else if (strcmp(token, "put") == 0) {
        char* from = strtok_r(NULL, delim, &state);
        char* to = strtok_r(NULL, delim, &state);
        cmd_put(from, to);
    } else if (strcmp(token, "get") == 0) {
        char* from = strtok_r(NULL, delim, &state);
        char* to = strtok_r(NULL, delim, &state);
        cmd_get(from, to);
    } else if (strcmp(token, "cat") == 0) {
        token = strtok_r(NULL, delim, &state);
        cmd_cat(token, stdout);
    } else {
        printf("unknown command\n");
    }
}

int main(int argc, char** argv) {
    if (argc == 2) {
        cmd_open(argv[1]);
    }

    char* line; // managed by read_cmd
    while ((line = read_cmd()) != NULL) {
        handle_cmd(line);
    }

    if (driver != NULL) {
        TFS_Driver_Destruct(driver);
        free(driver);
    }
}
