#ifndef XV6FS_API_H
#define XV6FS_API_H

#include "../stdint.h"

struct inode;

struct inode* namei(char *path);
void ilock(struct inode *ip);
void iunlock(struct inode *ip);
void iput(struct inode *ip);
void iupdate(struct inode *ip);
int readi(struct inode *ip, char *dst, uint32_t off, uint32_t n);
int writei(struct inode *ip, char *src, uint32_t off, uint32_t n);

#endif