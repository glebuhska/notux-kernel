#ifndef XV6_FS_H
#define XV6_FS_H

#include <stdint.h>

#define BSIZE 512
#define XV6_MAGIC 0x10203040
#define NDIRECT 12
#define NINDIRECT (BSIZE / sizeof(uint32_t))
#define MAXFILE (NDIRECT + NINDIRECT)

#define ROOTINO 1
struct superblock {
    uint32_t magic;      // Must be XV6_MAGIC
    uint32_t size;       // Size of file system image (blocks)
    uint32_t nblocks;    // Number of data blocks
    uint32_t ninodes;    // Number of inodes
    uint32_t nlog;       // Number of log blocks
    uint32_t logstart;   // Block number of first log block
    uint32_t inodestart; // Block number of first inode block
    uint32_t bmapstart;  // Block number of first free map block
};
struct dinode {
    int16_t type;               // File type (0 = free, 1 = DIR, 2 = FILE, 3 = DEV)
    int16_t major;              // Major device number (for DEV)
    int16_t minor;              // Minor device number (for DEV)
    int16_t nlink;              // Number of links to inode in file system
    uint32_t size;              // Size of file (bytes)
    uint32_t addrs[NDIRECT+1];  // Data block addresses (12 direct + 1 indirect)
};
#define IPB (BSIZE / sizeof(struct dinode))
#define IBLOCK(i, sb) ((i) / IPB + (sb).inodestart)
#define BPB (BSIZE * 8)
#define BBLOCK(b, sb) ((b) / BPB + (sb).bmapstart)
#define DIRSIZ 14
struct dirent {
    uint16_t inum;
    char name[DIRSIZ];
};

#endif