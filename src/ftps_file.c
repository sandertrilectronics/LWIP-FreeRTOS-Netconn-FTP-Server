/*
 * ftps_file.c
 *
 *  Created on: Feb 18, 2020
 *      Author: Sander
 */

#include "ftps_file.h"
#include "cms_file.h"
#include "cms_logging.h"

FRESULT ftps_f_stat(const char* path, FILINFO* nfo) {
	return f_stat(path, nfo);
}

FRESULT ftps_f_opendir(DIR* dir_p, const char* path) {
	return f_opendir(dir_p, path);
}

FRESULT ftps_f_readdir(DIR* dp, FILINFO* fno) {
	return f_readdir(dp, fno);
}

FRESULT ftps_f_unlink(const char* path) {
	return f_remove(path);
}

FRESULT ftps_f_open(FIL* file_p, const char* path, uint8_t mode) {
	return f_open(file_p, path, mode);
}

FSIZE_t ftps_f_size(FIL* file_p) {
	return f_size(file_p);
}

FRESULT ftps_f_close(FIL* file_p) {
	return f_close(file_p);
}

// when using the write and read function with DMA (like with
// an SD card), unaligned addresses cause a lot of trouble.
// The void * buffers areused for sending and reading data
// to the file. A void pointer ensures that the buffer is
// on a 4 byte boundary.
#define p_is_not_aligned(POINTER, BYTE_COUNT) \
    ((uintptr_t)(const void *)(POINTER)) % (BYTE_COUNT)

// buffer sizes
#define BUF_SIZE		1024

FRESULT ftps_f_write(FIL* file_p, const void* buffer, uint32_t len, uint32_t* written) {
	FRESULT res = FR_DISK_ERR;

	// check alignment
	if (p_is_not_aligned(buffer, 4)) {
		// feedback
		printf("wr ptr unaligned\r\n");

		if (len <= BUF_SIZE) {
			// buffer
			void *vfs_write_buf[BUF_SIZE];

			// copy write data
			memcpy(vfs_write_buf, buffer, len);

			// write the data
			res = f_write(file_p, vfs_write_buf, len, written);
		}
	}
	else {
		// write data
		res = f_write(file_p, buffer, len, written);
	}

	return res;
}

FRESULT ftps_f_read(FIL* file_p, const void* buffer, uint32_t len, uint32_t* read) {
	FRESULT res = FR_DISK_ERR;

	// check alignment
	if (p_is_not_aligned(buffer, 4)) {
		// feedback
		printf("rd ptr unaligned\r\n");

		if (len <= BUF_SIZE) {
			// create buffer
			void *vfs_read_buf[BUF_SIZE];

			// read the data
			res = f_read(file_p, vfs_read_buf, len, read);

			// copy read data
			memcpy(buffer, vfs_read_buf, len);
		}
	}
	else {
		// write data
		res = f_read(file_p, buffer, len, read);
	}

	return res;
}

FRESULT ftps_f_mkdir(const char* path) {
	return f_mkdir(path);
}

FRESULT ftps_f_rename(const char* from, const char* to) {
	return f_rename(from, to);
}

FRESULT ftps_f_utime(const TCHAR* path, const FILINFO* fno) {
	return f_utime(path, fno);
}

FRESULT ftps_f_getfree(const TCHAR* path, DWORD* nclst, FATFS** fatfs) {
	return f_getfree(path, nclst, fatfs);
}
