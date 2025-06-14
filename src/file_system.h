/**
 * @file file_system.h
 * @brief Function prototypes for filesystem operations (loading/saving config).
 */

#ifndef FILE_SYSTEM_H
#define FILE_SYSTEM_H

void initFileSystem();
bool loadConfig();
bool saveConfig();

#endif // FILE_SYSTEM_H