/**
 * @file    file_system.h
 * @author  Eng: Anas Alhawija
 * @brief   Function prototypes for filesystem operations.
 * @version 2.1
 * @date    2025-07-04
 * 
 * @project Smart GSM Gateway
 * @license MIT License
 * 
 * @description Declares the public interface for initializing the LittleFS filesystem
 *              and for loading/saving the gateway's configuration file.
 */




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