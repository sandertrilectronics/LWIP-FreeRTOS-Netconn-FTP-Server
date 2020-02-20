/*
 * ftps_file.h
 *
 *  Created on: Feb 18, 2020
 *      Author: Sander
 */

#ifndef ETH_FTP_FTPS_FILE_H_
#define ETH_FTP_FTPS_FILE_H_

#include <stdint.h>
#include "fatfs.h"

extern FRESULT ftps_f_stat(const char* path, FILINFO* nfo);

extern FRESULT ftps_f_opendir(DIR* dir_p, const char* path);

extern FRESULT ftps_f_readdir(DIR* dp, FILINFO* fno);

extern FRESULT ftps_f_unlink(const char* path);

extern FRESULT ftps_f_open(FIL* file_p, const char* path, uint8_t mode);

extern FSIZE_t ftps_f_size(FIL* file_p);

extern FRESULT ftps_f_close(FIL* file_p);

extern FRESULT ftps_f_write(FIL* file_p, const void* buffer, uint32_t len, uint32_t* written);

extern FRESULT ftps_f_read(FIL* file_p, const void* buffer, uint32_t len, uint32_t* read);

extern FRESULT ftps_f_mkdir(const char* path);

extern FRESULT ftps_f_rename(const char* from, const char* to);

extern FRESULT ftps_f_utime(const TCHAR* path, const FILINFO* fno);

extern FRESULT ftps_f_getfree(const TCHAR* path, DWORD* nclst, FATFS** fatfs);

#endif /* ETH_FTP_FTPS_FILE_H_ */
