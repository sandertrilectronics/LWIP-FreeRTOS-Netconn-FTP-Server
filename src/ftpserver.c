/*
 FTP Server for STM32-E407 and ChibiOS
 Copyright (C) 2015 Jean-Michel Gallego

 See readme.txt for information

 Licensed under the Apache License, Version 2.0 (the "License");
 you may not use this file except in compliance with the License.
 You may obtain a copy of the License at

 http://www.apache.org/licenses/LICENSE-2.0

 Unless required by applicable law or agreed to in writing, software
 distributed under the License is distributed on an "AS IS" BASIS,
 WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 See the License for the specific language governing permissions and
 limitations under the License.
 */

#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdarg.h>

#include "ftps.h"
#include "ftps_file.h"

#include "lwip.h"

#define COMMAND_PRINT(f, ...)	printf(f, ##__VA_ARGS__)
#define DEBUG_PRINT(f, ...)		printf(f, ##__VA_ARGS__)
#define TICK_HZ					1000

// =========================================================
//
//              Send a response to the client
//
// =========================================================

void ftp_send(struct netconn *con, const char *fmt, ...) {
	// send buffer
	char send_buffer[FTP_BUF_SIZE];

	// Create vaarg list
	va_list args;
	va_start(args, fmt);

	// Write string to buffer
	vsnprintf(send_buffer, FTP_BUF_SIZE, fmt, args);

	// Close vaarg list
	va_end(args);

	// send to endpoint
	netconn_write(con, send_buffer, strlen(send_buffer), NETCONN_COPY);

	// debugging
	COMMAND_PRINT("%s", send_buffer);
}

// Create string YYYYMMDDHHMMSS from date and time
//
// parameters:
//    date, time
//
// return:
//    pointer to string

static char * makeDateTimeStr(char *str, uint16_t date, uint16_t time) {
	snprintf(str, 25, "%04d%02d%02d%02d%02d%02d", ((date & 0xFE00) >> 9) + 1980, (date & 0x01E0) >> 5, date & 0x001F, (time & 0xF800) >> 11,
			(time & 0x07E0) >> 5, (time & 0x001F) << 1);
	return str;
}

// Calculate date and time from first parameter sent by MDTM command (YYYYMMDDHHMMSS)
//
// parameters:
//   pdate, ptime: pointer of variables where to store data
//
// return:
//    length of (time parameter + space) if date/time are ok
//    0 if parameter is not YYYYMMDDHHMMSS

static int8_t getDateTime(char *parameters, uint16_t * pdate, uint16_t * ptime) {
	// Date/time are expressed as a 14 digits long string
	//   terminated by a space and followed by name of file
	if (strlen(parameters) < 15 || parameters[14] != ' ')
		return 0;
	for (uint8_t i = 0; i < 14; i++)
		if (!isdigit(parameters[i]))
			return 0;

	parameters[14] = 0;
	*ptime = atoi(parameters + 12) >> 1;   // seconds
	parameters[12] = 0;
	*ptime |= atoi(parameters + 10) << 5;  // minutes
	parameters[10] = 0;
	*ptime |= atoi(parameters + 8) << 11;  // hours
	parameters[8] = 0;
	*pdate = atoi(parameters + 6);         // days
	parameters[6] = 0;
	*pdate |= atoi(parameters + 4) << 5;   // months
	parameters[4] = 0;
	*pdate |= (atoi(parameters) - 1980) << 9;       // years

	return 15;
}

// =========================================================
//
//             Get a command from the client
//
// =========================================================

// update variables command and parameters
//
// return: -4 time out
//         -3 error receiving data
//         -2 command line too long
//         -1 syntax error
//          0 command without parameters
//          >0 length of parameters

static int8_t readCommand(ftp_data_t *ftp) {
	char * pbuf;
	uint16_t buflen;
	int8_t rc = 0;
	int8_t i;
	char car;
	int8_t net_err = 0;

	ftp->command[0] = 0;
	ftp->parameters[0] = 0;

	net_err = netconn_recv(ftp->ctrlconn, &ftp->inbuf);

	if (net_err == ERR_TIMEOUT)
		return -4;

	if (net_err != ERR_OK)
		return -3;

	netbuf_data(ftp->inbuf, (void **) &pbuf, &buflen);

	if (buflen == 0)
		goto deletebuf;

	i = 0;
	car = pbuf[0];

	do {
		if (!isalpha(car))
			break;
		ftp->command[i++] = car;
		car = pbuf[i];
	}
	while (i < buflen && i < 4);

	ftp->command[i] = 0;

	if (car != ' ')
		goto deletebuf;
	do {
		if (i > buflen + 2)
			goto deletebuf;
	}
	while (pbuf[i++] == ' ');

	rc = i;

	do {
		car = pbuf[rc++];
	}
	while (car != '\n' && car != '\r' && rc < buflen);

	if (rc == buflen) {
		rc = -1;
		goto deletebuf;
	}

	if (rc - i - 1 >= FTP_PARAM_SIZE) {
		rc = -2;
		goto deletebuf;
	}

	strncpy(ftp->parameters, pbuf + i - 1, rc - i);

	ftp->parameters[rc - i] = 0;
	rc = rc - i;

	// delete buf tag
	deletebuf:

	//
	COMMAND_PRINT("Incomming: %s %s\r\n", ftp->command, ftp->parameters);
	netbuf_delete(ftp->inbuf);

	// return error code
	return rc;
}

// =========================================================
//
//               Functions for data connection
//
// =========================================================

static bool open_listen(struct netconn *con, uint16_t port) {
	// If this is not already done, create the TCP connection handle
	//   to listen to client to open data connection
	if (con != NULL)
		return true;

	// create new socket
	con = netconn_new(NETCONN_TCP);

	// create was ok?
	if (con == NULL) {
		DEBUG_PRINT("Error in listenDataConn()\r\n");
		return false;
	}

	// Bind listdataconn to port (FTP_DATA_PORT + num) with default IP address
	if (netconn_bind(con, IP_ADDR_ANY, port) != ERR_OK) {
		DEBUG_PRINT("Error in listenDataConn()\r\n");
		return false;
	}

	// Put the connection into LISTEN state
	if (netconn_listen(con) != ERR_OK) {
		DEBUG_PRINT("Error in listenDataConn()\r\n");
		return false;
	}

	// all good
	return true;
}

static bool data_con_open(ftp_data_t *ftp) {
	//
	if (ftp->dataConnMode == NOTSET) {
		DEBUG_PRINT("No connecting mode defined\r\n");
		goto error;
	}

	// feedback
	DEBUG_PRINT("Connecting in %s mode\r\n", (ftp->dataConnMode == PASSIVE ? "passive" : "active"));

	// are we in passive mode?
	if (ftp->dataConnMode == PASSIVE) {
		//
		if (ftp->listdataconn == NULL)
			goto error;

		// Wait for connection from client for 500ms
		netconn_set_recvtimeout(ftp->listdataconn, 500);

		// accept connection
		if (netconn_accept(ftp->listdataconn, &ftp->dataconn) != ERR_OK) {
			DEBUG_PRINT("Error in dataConnect(): netconn_accept\r\n");
			goto error;
		}
	}
	// we are in active mode
	else {
		//  Create a new TCP connection handle
		ftp->dataconn = netconn_new(NETCONN_TCP);
		if (ftp->dataconn == NULL) {
			DEBUG_PRINT("Error in dataConnect(): netconn_new\r\n");
			// goto delconn;
			goto error;
		}

		//  Connect to data port with client IP address
		if (netconn_bind(ftp->dataconn, IP_ADDR_ANY, ftp->dataPort) != ERR_OK) {
			DEBUG_PRINT("Error in dataConnect(): netconn_bind\r\n");
			// goto error;
			goto delconn;
		}

		//
		if (netconn_connect(ftp->dataconn, &ftp->ipclient, ftp->dataPort) != ERR_OK) {
			DEBUG_PRINT("Error in dataConnect(): netconn_connect\r\n");
			// goto error;
			goto delconn;
		}
	}

	// all good
	return true;

	delconn:

	if (ftp->dataconn != NULL) {
		netconn_delete(ftp->dataconn);
		ftp->dataconn = NULL;
	}

	error:

	ftp_send(ftp->ctrlconn, "425 No data connection\r\n");

	return false;
}

static void data_con_close(ftp_data_t *ftp) {
	ftp->dataConnMode = NOTSET;
	if (ftp->dataconn == NULL)
		return;
	netconn_close(ftp->dataconn);
	netconn_delete(ftp->dataconn);
	ftp->dataconn = NULL;
}

static void data_con_write(struct netconn *dataconn, const char * data) {
	netconn_write(dataconn, data, strlen(data), NETCONN_COPY);
	// COMMAND_PRINT( data );
}

// =========================================================
//
//                  Functions on files
//
// =========================================================

// Make complete path/name from cwdName and parameters
//
// 3 possible cases:
//   parameters can be absolute path, relative path or only the name
//
// parameters:
//   fullName : where to store the path/name
//
// return:
//   true, if done

static bool build_path(char * fullName, char * param, char *cwdName) {
	// Root or empty?
	if (!strcmp(param, "/") || strlen(param) == 0) {
		strcpy(fullName, "/");
		return true;
	}
	// If relative path, concatenate with current dir
	if (param[0] != '/') {
		strcpy(fullName, cwdName);
		if (fullName[strlen(fullName) - 1] != '/')
			strncat(fullName, "/", FTP_CWD_SIZE);
		strncat(fullName, param, FTP_CWD_SIZE);
	}
	else
		strcpy(fullName, param);
	// If ends with '/', remove it
	uint16_t strl = strlen(fullName) - 1;
	if (fullName[strl] == '/' && strl > 1)
		fullName[strl] = 0;
	if (strlen(fullName) < FTP_CWD_SIZE)
		return true;

	return false;
}

// Return true if a file or directory exists
//
// parameters:
//   path : absolute name of file or directory

static bool fs_exists(char * path, FILINFO *finfo) {
	if (!strcmp(path, "/"))
		return true;

	char * path0 = path;

	return ftps_f_stat(path0, finfo) == FR_OK;
}

// Open a directory
//
// parameters:
//   path : absolute name of directory
//
// return true if opened

static bool fs_opendir(DIR * pdir, char * dirName) {
	char * dirName0 = dirName;
	uint8_t ffs_result;

	ffs_result = ftps_f_opendir(pdir, dirName0);
	return ffs_result == FR_OK;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void ftp_cmd_pwd(ftp_data_t *ftp) {
	ftp_send(ftp->ctrlconn, "257 \"%s\" is your current directory\r\n", ftp->cwdName);
}

void ftp_cmd_cwd(ftp_data_t *ftp) {
	// no parmeters given?
	if (strlen(ftp->parameters) == 0) {
		ftp_send(ftp->ctrlconn, "501 No directory name\r\n");
		return;
	}

	// can we build a path from the parameters?
	if (!build_path(ftp->path, ftp->parameters, ftp->cwdName)) {
		ftp_send(ftp->ctrlconn, "500 Command line too long\r\n");
		return;
	}

	// does the path exist?
	if (!fs_exists(ftp->path, &ftp->finfo)) {
		ftp_send(ftp->ctrlconn, "550 Failed to change directory.\r\n");
		return;
	}

	// copy working directory name
	strcpy(ftp->cwdName, ftp->path);

	// send directory to client
	ftp_send(ftp->ctrlconn, "250 Directory successfully changed.\r\n");
}

void ftp_cmd_cdup(ftp_data_t *ftp) {
	// valid parameter?
	if (strlen(ftp->parameters) == 0) {
		ftp_send(ftp->ctrlconn, "501 No directory name\r\n");
		return;
	}

	// can we build a path from the parameter?
	if (!build_path(ftp->path, ftp->parameters, ftp->cwdName)) {
		ftp_send(ftp->ctrlconn, "500 Command line too long\r\n");
		return;
	}

	// does this path exist?
	if (!fs_exists(ftp->path, &ftp->finfo)) {
		ftp_send(ftp->ctrlconn, "550 Failed to change directory.\r\n");
		return;
	}

	// copy path
	strcpy(ftp->cwdName, ftp->path);

	// send ack to client
	ftp_send(ftp->ctrlconn, "250 Directory successfully changed.\r\n");
}

void ftp_cmd_mode(ftp_data_t *ftp) {
	if (!strcmp(ftp->parameters, "S"))
		ftp_send(ftp->ctrlconn, "200 S Ok\r\n");
	// else if( ! strcmp( parameters, "B" ))
	//  ftp_send(ftp->ctrlconn, "200 B Ok\r\n");
	else
		ftp_send(ftp->ctrlconn, "504 Only S(tream) is suported\r\n");
}

void ftp_cmd_stru(ftp_data_t *ftp) {
	if (!strcmp(ftp->parameters, "F"))
		ftp_send(ftp->ctrlconn, "200 F Ok\r\n");
	else
		ftp_send(ftp->ctrlconn, "504 Only F(ile) is suported\r\n");
}

void ftp_cmd_type(ftp_data_t *ftp) {
	if (!strcmp(ftp->parameters, "A"))
		ftp_send(ftp->ctrlconn, "200 TYPE is now ASCII\r\n");
	else if (!strcmp(ftp->parameters, "I"))
		ftp_send(ftp->ctrlconn, "200 TYPE is now 8-bit binary\r\n");
	else
		ftp_send(ftp->ctrlconn, "504 Unknow TYPE\r\n");
}

void ftp_cmd_pasv(ftp_data_t *ftp) {
	if (!open_listen(ftp->listdataconn, ftp->dataPort)) {
		//
		ftp_send(ftp->ctrlconn, "425 Can't set connection management to passive\r\n");
		//
		ftp->dataConnMode = NOTSET;
		//
		return;
	}

	// close data connection
	data_con_close(ftp);

	// reply that we are entering passive mode
	ftp_send(ftp->ctrlconn, "227 Entering Passive Mode (%s,%d,%d).\r\n", ipaddr_ntoa(&ftp->ipserver), ftp->dataPort >> 8, ftp->dataPort & 255);

	// feedback
	DEBUG_PRINT("Data port set to %u\r\n", ftp->dataPort);

	// set state
	ftp->dataConnMode = PASSIVE;
}

void ftp_cmd_port(ftp_data_t *ftp) {
	uint8_t ip[4];
	uint8_t i;

	// close data connection just to be sure
	data_con_close(ftp);

	// parameter valid?
	if (strlen(ftp->parameters) == 0) {
		// send error to client
		ftp_send(ftp->ctrlconn, "501 no parameters given\r\n");
		ftp->dataConnMode = NOTSET;
		return;
	}

	// Start building IP
	char *p = ftp->parameters - 1;

	// build IP
	for (i = 0; i < 4 && p != NULL; i++) {
		// valid pointer?
		if (p == NULL)
			break;

		// convert number
		ip[i] = atoi(++p);

		// find next comma
		p = strchr(p, ',');
	}

	// get port
	if (p != NULL) {
		// read upper octet of port
		if (i == 4)
			ftp->dataPort = 256 * atoi(++p);

		// go to next comma
		p = strchr(p, ',');

		// read lower octet of port
		if (p != NULL)
			ftp->dataPort += atoi(++p);
	}

	// error parsing IP and port?
	if (p == NULL) {
		ftp_send(ftp->ctrlconn, "501 Can't interpret parameters\r\n");
		ftp->dataConnMode = NOTSET;
		return;
	}

	// build IP address
	IP4_ADDR(&ftp->ipclient, ip[0], ip[1], ip[2], ip[3]);

	// send ack to client
	ftp_send(ftp->ctrlconn, "200 PORT command successful\r\n");

	// feedback
	DEBUG_PRINT("Data IP set to %u:%u:%u:%u\r\n", ip[0], ip[1], ip[2], ip[3]);
	DEBUG_PRINT("Data port set to %u\r\n", ftp->dataPort);

	// set data connection mode
	ftp->dataConnMode = ACTIVE;

}

void ftp_cmd_list(ftp_data_t *ftp) {
	DIR dir;

	// can we open the directory?
	if (!fs_opendir(&dir, ftp->cwdName)) {
		ftp_send(ftp->ctrlconn, "550 Can't open directory %s\r\n", ftp->cwdName);
		return;
	}

	// open data connection
	if (!data_con_open(ftp)) {
		ftp_send(ftp->ctrlconn, "425 Can't create connection\r\n");
		return;
	}

	// accept the command
	ftp_send(ftp->ctrlconn, "150 Accepted data connection\r\n");

	// working buffer
	char dir_name_buf[FTP_BUF_SIZE];

	// loop until errors occur
	while (ftps_f_readdir(&dir, &ftp->finfo) == FR_OK) {
		// last entry read?
		if (ftp->finfo.fname[0] == 0)
			break;

		// file name is not valid?
		if (ftp->finfo.fname[0] == '.')
			continue;

		// list command given? (to give support for NLST)
		if (strcmp(ftp->command, "LIST"))
			snprintf(dir_name_buf, FTP_BUF_SIZE, "%s\r\n", ftp->lfn[0] == 0 ? ftp->finfo.fname : ftp->lfn);
		// is it a directory?
		else if (ftp->finfo.fattrib & AM_DIR)
			snprintf(dir_name_buf, FTP_BUF_SIZE, "+/,\t%s\r\n", ftp->lfn[0] == 0 ? ftp->finfo.fname : ftp->lfn);
		// just a file
		else
			snprintf(dir_name_buf, FTP_BUF_SIZE, "+r,s%d,\t%s\r\n", ftp->finfo.fsize, ftp->lfn[0] == 0 ? ftp->finfo.fname : ftp->lfn);

		// write data to endpoint
		data_con_write(ftp->dataconn, dir_name_buf);
	}

	// close data connection
	data_con_close(ftp);

	// all was good
	ftp_send(ftp->ctrlconn, "226 Directory send OK.\r\n");
}

void ftp_cmd_mlsd(ftp_data_t *ftp) {
	DIR dir;
	uint16_t nm = 0;

	// can we open the directory?
	if (!fs_opendir(&dir, ftp->cwdName)) {
		ftp_send(ftp->ctrlconn, "550 Can't open directory %s\r\n", ftp->parameters);
		return;
	}

	// open data connection
	if (!data_con_open(ftp)) {
		ftp_send(ftp->ctrlconn, "425 Can't create connection\r\n");
		return;
	}

	// all good
	ftp_send(ftp->ctrlconn, "150 Accepted data connection\r\n");

	// working buffer
	char buf[FTP_BUF_SIZE];

	// loop while we read without errors
	while (ftps_f_readdir(&dir, &ftp->finfo) == FR_OK) {
		// end of directory found?
		if (ftp->finfo.fname[0] == 0)
			break;

		// entry valid?
		if (ftp->finfo.fname[0] == '.')
			continue;

		// does the file have a date or not?
		if (ftp->finfo.fdate != 0)
			snprintf(buf, FTP_BUF_SIZE, "Type=%s;Size=%d;Modify=%s; %s\r\n", ftp->finfo.fattrib & AM_DIR ? "dir" : "file", ftp->finfo.fsize,
					makeDateTimeStr(ftp->str, ftp->finfo.fdate, ftp->finfo.ftime), ftp->lfn[0] == 0 ? ftp->finfo.fname : ftp->lfn);
		else
			snprintf(buf, FTP_BUF_SIZE, "Type=%s;Size=%d; %s\r\n", ftp->finfo.fattrib & AM_DIR ? "dir" : "file", ftp->finfo.fsize,
					ftp->lfn[0] == 0 ? ftp->finfo.fname : ftp->lfn);

		// write the data
		data_con_write(ftp->dataconn, buf);

		// increment variable
		nm++;
	}

	// close data connection
	data_con_close(ftp);

	// all was good
	ftp_send(ftp->ctrlconn, "226 Options: -a -l, %d matches total\r\n", nm);
}

void ftp_cmd_dele(ftp_data_t *ftp) {
	// parameters valid?
	if (strlen(ftp->parameters) == 0) {
		ftp_send(ftp->ctrlconn, "501 No file name\r\n");
		return;
	}

	// can we build a valid path?
	if (!build_path(ftp->path, ftp->parameters, ftp->cwdName)) {
		ftp_send(ftp->ctrlconn, "500 Command line too long\r\n");
		return;
	}

	// does the file exist?
	if (!fs_exists(ftp->path, &ftp->finfo)) {
		ftp_send(ftp->ctrlconn, "550 file %s not found\r\n", ftp->parameters);
		return;
	}

	// can we delete the file?
	if (ftps_f_unlink(ftp->path) != FR_OK) {
		ftp_send(ftp->ctrlconn, "450 Can't delete %s\r\n", ftp->parameters);
		return;
	}

	// all good
	ftp_send(ftp->ctrlconn, "250 Deleted %s\r\n", ftp->parameters);

}

void ftp_cmd_noop(ftp_data_t *ftp) {
	ftp_send(ftp->ctrlconn, "200 Zzz...\r\n");
}

void ftp_cmd_retr(ftp_data_t *ftp) {
	// parmeter ok?
	if (strlen(ftp->parameters) == 0) {
		ftp_send(ftp->ctrlconn, "501 No file name\r\n");
		return;
	}

	// can we create a valid path from the parameter?
	if (!build_path(ftp->path, ftp->parameters, ftp->cwdName)) {
		ftp_send(ftp->ctrlconn, "500 Command line too long\r\n");
		return;
	}

	// does the chosen file exists?
	if (!fs_exists(ftp->path, &ftp->finfo)) {
		ftp_send(ftp->ctrlconn, "550 File %s not found\r\n", ftp->parameters);
		return;
	}

	// can we open the file?
	if (ftps_f_open(&ftp->file, ftp->path, FA_READ) != FR_OK) {
		ftp_send(ftp->ctrlconn, "450 Can't open %s\r\n", ftp->parameters);
		return;
	}

	// can we connect to the client?
	if (!data_con_open(ftp)) {
		ftps_f_close(&ftp->file);
		return;
	}

	// feedback
	DEBUG_PRINT("Sending %s\r\n", ftp->parameters);

	// send accept to client
	ftp_send(ftp->ctrlconn, "150 Connected to port %u, %lu bytes to download\r\n", ftp->dataPort, ftps_f_size(&ftp->file));

	// variables used in loop
	int bytes_transfered = 0;
	uint32_t bytes_read = 1;
	uint8_t buf[FTP_BUF_SIZE];

	// loop while reading is OK
	while (bytes_read > 0) {
		// read from file ok?
		if (ftps_f_read(&ftp->file, buf, FTP_BUF_SIZE, (UINT *) &bytes_read) != FR_OK) {
			ftp_send(ftp->ctrlconn, "451 Communication error during transfer\r\n");
			break;
		}

		// write data to socket
		if (netconn_write(ftp->dataconn, buf, bytes_read, NETCONN_COPY) != ERR_OK) {
			ftp_send(ftp->ctrlconn, "426 Error during file transfer\r\n");
			break;
		}

		// increment variable
		bytes_transfered += bytes_read;
	}

	// feedback
	DEBUG_PRINT("Sent %u bytes\r\n", bytes_transfered);

	// close file
	ftps_f_close(&ftp->file);

	// close data socket
	data_con_close(ftp);

	// stop transfer
	ftp_send(ftp->ctrlconn, "226 File successfully transferred\r\n");
}

void ftp_cmd_stor(ftp_data_t *ftp) {
	// argument valid?
	if (strlen(ftp->parameters) == 0) {
		ftp_send(ftp->ctrlconn, "501 No file name\r\n");
		return;
	}

	// is the path valid?
	if (!build_path(ftp->path, ftp->parameters, ftp->cwdName)) {
		ftp_send(ftp->ctrlconn, "500 Command line too long\r\n");
		return;
	}

	// does the path exist?
	if (ftps_f_open(&ftp->file, ftp->path, FA_CREATE_ALWAYS | FA_WRITE) != FR_OK) {
		ftp_send(ftp->ctrlconn, "450 Can't open/create %s\r\n", ftp->parameters);
		return;
	}

	// can we set up a data connection?
	if (!data_con_open(ftp)) {
		ftps_f_close(&ftp->file);
		return;
	}

	// feedback
	DEBUG_PRINT("Receiving %s\r\n", ftp->parameters);

	// reply to ftp client that we are ready
	ftp_send(ftp->ctrlconn, "150 Connected to port %u\r\n", ftp->dataPort);

	//
	struct pbuf * rcvbuf = NULL;
	void * prcvbuf;
	uint16_t buflen = 0;
	uint16_t off = 0;
	uint16_t copylen;

	UINT nb;
	int8_t file_err = 0;
	int8_t con_err = 0;
	int bytes_transfered = 0;
	uint8_t buf[FTP_BUF_SIZE];

	while (true) {
		// receive data from ftp client ok?
		con_err = netconn_recv_tcp_pbuf(ftp->dataconn, &rcvbuf);

		// socket closed? (end of file)
		if (con_err == ERR_CLSD)
			break;
		// other error?
		else if (con_err != ERR_OK) {
			ftp_send(ftp->ctrlconn, "426 Error during file transfer\r\n");
			break;
		}

		// housekeeping
		prcvbuf = rcvbuf->payload;
		buflen = rcvbuf->tot_len;

		// loop untill all data is written
		while (buflen > 0) {
			// copy complete buffer or part of it?
			if (buflen <= FTP_BUF_SIZE - off)
				copylen = buflen;
			else
				copylen = FTP_BUF_SIZE - off;

			// decrement buffer
			buflen -= copylen;

			// copy data
			memcpy(buf + off, prcvbuf, copylen);

			prcvbuf += copylen;
			off += copylen;
			if (off == FTP_BUF_SIZE) {
				if (file_err == 0)
					file_err = ftps_f_write(&ftp->file, buf, FTP_BUF_SIZE, (UINT *) &nb);
				else
					break;

				off = 0;
			}

			bytes_transfered += copylen;
		}

		// free pbuf
		pbuf_free(rcvbuf);

		// error in nested loop?
		if (file_err != 0) {
			ftp_send(ftp->ctrlconn, "451 Communication error during transfer\r\n");
			break;
		}

		DEBUG_PRINT("Received %u bytes\r\n", bytes_transfered);
	}

	// write the remaining data to file
	if (off > 0 && file_err == 0) {
		file_err = ftps_f_write(&ftp->file, buf, off, (UINT *) &nb);
	}

	// close file
	ftps_f_close(&ftp->file);

	// close data connection
	data_con_close(ftp);

	// all was good
	ftp_send(ftp->ctrlconn, "226 File successfully transferred\r\n");
}

void ftp_cmd_mkd(ftp_data_t *ftp) {
	// valid parameters?
	if (strlen(ftp->parameters) == 0) {
		ftp_send(ftp->ctrlconn, "501 No directory name\r\n");
		return;
	}

	// can we build a path?
	if (!build_path(ftp->path, ftp->parameters, ftp->cwdName)) {
		ftp_send(ftp->ctrlconn, "500 Command line too long\r\n");
		return;
	}

	// does the path not exist already?
	if (!fs_exists(ftp->path, &ftp->finfo)) {
		ftp_send(ftp->ctrlconn, "521 \"%s\" directory already exists\r\n", ftp->parameters);
		return;
	}

	// make the directory
	if (ftps_f_mkdir(ftp->path) != FR_OK) {
		ftp_send(ftp->ctrlconn, "550 Can't create \"%s\"\r\n", ftp->parameters);
		return;
	}

	// feedback
	DEBUG_PRINT("Creating directory %s\r\n", ftp->parameters);

	// check the result
	ftp_send(ftp->ctrlconn, "257 \"%s\" created\r\n", ftp->parameters);
}

void ftp_cmd_rmd(ftp_data_t *ftp) {
	// valid parameter?
	if (strlen(ftp->parameters) == 0) {
		ftp_send(ftp->ctrlconn, "501 No directory name\r\n");
		return;
	}

	// Can we build path?
	if (!build_path(ftp->path, ftp->parameters, ftp->cwdName)) {
		ftp_send(ftp->ctrlconn, "500 Command line too long\r\n");
		return;
	}

	// feedback
	DEBUG_PRINT("Deleting %s\r\n", ftp->path);

	// file does exist?
	if (!fs_exists(ftp->path, &ftp->finfo)) {
		ftp_send(ftp->ctrlconn, "550 Directory \"%s\" not found\r\n", ftp->parameters);
		return;
	}

	// remove file ok?
	if (ftps_f_unlink(ftp->path) != FR_OK) {
		ftp_send(ftp->ctrlconn, "501 Can't delete \"%s\"\r\n", ftp->parameters);
		return;
	}

	// all good
	ftp_send(ftp->ctrlconn, "250 \"%s\" removed\r\n", ftp->parameters);
}

void ftp_cmd_rnfr(ftp_data_t *ftp) {
	ftp->cwdRNFR[0] = 0;

	// parameters ok?
	if (strlen(ftp->parameters) == 0) {
		ftp_send(ftp->ctrlconn, "501 No file name\r\n");
		return;
	}

	// can we build a path?
	if (!build_path(ftp->cwdRNFR, ftp->parameters, ftp->cwdName)) {
		ftp_send(ftp->ctrlconn, "500 Command line too long\r\n");
		return;
	}

	// does the file exist?
	if (!fs_exists(ftp->cwdRNFR, &ftp->finfo)) {
		ftp_send(ftp->ctrlconn, "550 file \"%s\" not found\r\n", ftp->parameters);
		return;
	}

	// feedback
	DEBUG_PRINT("Renaming %s\r\n", ftp->cwdRNFR);

	// reply to client
	ftp_send(ftp->ctrlconn, "350 RNFR accepted - file exists, ready for destination\r\n");
}

void ftp_cmd_rnto(ftp_data_t *ftp) {
	char sdir[FTP_CWD_SIZE];
	if (strlen(ftp->cwdRNFR) == 0) {
		ftp_send(ftp->ctrlconn, "503 Need RNFR before RNTO\r\n");
		return;
	}
	if (strlen(ftp->parameters) == 0) {
		ftp_send(ftp->ctrlconn, "501 No file name\r\n");
		return;
	}
	if (!build_path(ftp->path, ftp->parameters, ftp->cwdName)) {
		ftp_send(ftp->ctrlconn, "500 Command line too long\r\n");
		return;
	}

	if (fs_exists(ftp->path, &ftp->finfo)) {
		ftp_send(ftp->ctrlconn, "553 \"%s\" already exists\r\n", ftp->parameters);
		return;
	}

	strcpy(sdir, ftp->path);
	char * psep = strrchr(sdir, '/');
	bool fail = (psep == NULL);
	if (!fail) {
		if (psep == sdir)
			psep++;
		*psep = 0;
		fail = !(fs_exists(sdir, &ftp->finfo) && ((ftp->finfo.fattrib & AM_DIR) || !strcmp(sdir, "/")));
		if (fail) {
			ftp_send(ftp->ctrlconn, "550 \"%s\" is not a directory\r\n", sdir);
		}
		else {
			DEBUG_PRINT("Renaming %s to %s\r\n", ftp->cwdRNFR, ftp->path);
			if (ftps_f_rename(ftp->cwdRNFR, ftp->path) == FR_OK)
				ftp_send(ftp->ctrlconn, "250 File successfully renamed or moved\r\n");
			else
				fail = true;
		}
	}

	if (fail) {
		ftp_send(ftp->ctrlconn, "451 Rename/move failure\r\n");
	}
}

void ftp_cmd_feat(ftp_data_t *ftp) {
	ftp_send(ftp->ctrlconn, "211 Extensions supported:\r\n MDTM\r\n MLSD\r\n SIZE\r\n SITE FREE\r\n211 End.\r\n");
}

void ftp_cmd_mdtm(ftp_data_t *ftp) {
	char * fname;
	uint16_t date, time;
	uint8_t gettime;

	gettime = getDateTime(ftp->parameters, &date, &time);
	fname = ftp->parameters + gettime;

	if (strlen(fname) == 0) {
		ftp_send(ftp->ctrlconn, "501 No file name\r\n");
	}

	if (!build_path(ftp->path, fname, ftp->cwdName)) {
		ftp_send(ftp->ctrlconn, "500 Command line too long\r\n");
		return;
	}

	if (!fs_exists(ftp->path, &ftp->finfo)) {
		ftp_send(ftp->ctrlconn, "550 file \"%s\" not found\r\n", ftp->parameters);
		return;
	}

	if (!gettime) {
		ftp_send(ftp->ctrlconn, "213 %s\r\n", makeDateTimeStr(ftp->str, ftp->finfo.fdate, ftp->finfo.ftime));
		return;
	}

	ftp->finfo.fdate = date;
	ftp->finfo.ftime = time;
	if (ftps_f_utime(ftp->path, &ftp->finfo) == FR_OK)
		ftp_send(ftp->ctrlconn, "200 Ok\r\n");
	else
		ftp_send(ftp->ctrlconn, "550 Unable to modify time\r\n");
}

void ftp_cmd_size(ftp_data_t *ftp) {
	if (strlen(ftp->parameters) == 0) {
		ftp_send(ftp->ctrlconn, "501 No file name\r\n");
		return;
	}

	if (!build_path(ftp->path, ftp->parameters, ftp->cwdName)) {
		ftp_send(ftp->ctrlconn, "500 Command line too long\r\n");
		return;
	}

	if (!fs_exists(ftp->path, &ftp->finfo) || (ftp->finfo.fattrib & AM_DIR)) {
		ftp_send(ftp->ctrlconn, "550 No such file\r\n");
		return;
	}

	ftp_send(ftp->ctrlconn, "213 %lu\r\n", ftp->finfo.fsize);
	ftps_f_close(&ftp->file);
}

void ftp_cmd_site(ftp_data_t *ftp) {
	if (!strcmp(ftp->parameters, "FREE")) {
		FATFS * fs;
		uint32_t free_clust;
		ftps_f_getfree("0:", &free_clust, &fs);
		ftp_send(ftp->ctrlconn, "211 %lu MB free of %lu MB capacity\r\n", free_clust * fs->csize >> 11, (fs->n_fatent - 2) * fs->csize >> 11);
	}
	else {
		ftp_send(ftp->ctrlconn, "550 Unknown SITE command %s\r\n", ftp->parameters);
	}
}

void ftp_cmd_stat(ftp_data_t *ftp) {
	ftp_send(ftp->ctrlconn, "221 FTP Server status: you will be disconnected after %d minutes of inactivity\r\n",
	FTP_TIME_OUT);
	//sendBegin("211-FTP server status\r\n");
#warning commented out
	//sendCat(" Local time is ");
	//sendCat(strLocalTime(str));
	//sendCat("\r\n ");
	//sendCat("You will be disconnected after ");
	//sendCat(i2str(ftp->str, FTP_TIME_OUT));
	//sendCat(" minutes of inactivity\r\n");
	//sendCatWrite(ftp->ctrlconn, "211 End.");
}

typedef struct {
	const char *cmd;
	void (*func)(ftp_data_t *ftp);
} ftp_cmd_t;

static ftp_cmd_t ftpd_commands[] =
		{ { "PWD", ftp_cmd_pwd }, { "CWD", ftp_cmd_cwd }, { "CDUP", ftp_cmd_cdup }, { "MODE", ftp_cmd_mode }, { "STRU", ftp_cmd_stru }, { "TYPE",
				ftp_cmd_type }, { "PASV", ftp_cmd_pasv }, { "PORT", ftp_cmd_port }, { "NLST", ftp_cmd_list }, { "LIST", ftp_cmd_list }, { "MLSD",
				ftp_cmd_mlsd }, { "DELE", ftp_cmd_dele }, { "NOOP", ftp_cmd_noop }, { "RETR", ftp_cmd_retr }, { "STOR", ftp_cmd_stor }, { "MKD",
				ftp_cmd_mkd }, { "RMD", ftp_cmd_rmd }, { "RNFR", ftp_cmd_rnfr }, { "RNTO", ftp_cmd_rnto }, { "FEAT", ftp_cmd_feat }, { "MDTM",
				ftp_cmd_mdtm }, { "SIZE", ftp_cmd_size }, { "SITE", ftp_cmd_site }, { "STAT", ftp_cmd_stat }, { NULL, NULL } };
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// =========================================================
//
//                   Process a command
//
// =========================================================

static bool processCommand(ftp_data_t *ftp) {
	// quit command given?
	if (!strcmp(ftp->command, "QUIT"))
		return false;

	// command pointer
	ftp_cmd_t *cmd = ftpd_commands;

	// loop through all known commands
	while (cmd->cmd != NULL && cmd->func != NULL) {
		// is this the expected command?
		if (!strcmp(cmd->cmd, ftp->command))
			break;

		// increment
		cmd++;
	}

	// did we find a command?
	if (cmd->cmd != NULL && cmd->func != NULL)
		cmd->func(ftp);
	// no command found, unknown
	else
		ftp_send(ftp->ctrlconn, "500 Unknown command\r\n");

	// ftp is still running
	return true;
}

static uint8_t ftp_log_in(ftp_data_t *ftp) {
	//  Wait for user name during 10 seconds
	netconn_set_recvtimeout(ftp->ctrlconn, 10000);

	// read a command
	if (readCommand(ftp) < 0) {
		DEBUG_PRINT("Username timeout\r\n");
		return 0;
	}

	// is this the user command?
	if (strcmp(ftp->command, "USER")) {
		ftp_send(ftp->ctrlconn, "500 Syntax error\r\n");
		return 0;
	}

	// is this the normal user?
	if (!strcmp(ftp->parameters, FTP_USER_NAME)) {
		// all good
		ftp_send(ftp->ctrlconn, "331 OK. Password required\r\n");
	}
	else if (!strcmp(ftp->parameters, FTP_ADMIN_NAME)) {
		// all good
		ftp_send(ftp->ctrlconn, "331 OK. Password required\r\n");

		// waiting for admin password
		ftp->admin_enabled = 2;
	}
	else {
		// not a user and not an admin, error
		ftp_send(ftp->ctrlconn, "530 \r\n");
		return 0;
	}

	// read a command
	if (readCommand(ftp) < 0) {
		DEBUG_PRINT("Password timeout\r\n");
		return 0;
	}

	// password?
	if (strcmp(ftp->command, "PASS")) {
		ftp_send(ftp->ctrlconn, "500 Syntax error\r\n");
		return 0;
	}

	// is this the normal user?
	if (ftp->admin_enabled == 0 && !strcmp(ftp->parameters, FTP_USER_PASS)) {
		// username and password accepted
		ftp_send(ftp->ctrlconn, "230 OK, logged in as user\r\n");
	}
	else if (ftp->admin_enabled == 2 && !strcmp(ftp->parameters, FTP_ADMIN_PASS)) {
		// username and password accepted
		ftp_send(ftp->ctrlconn, "230 OK, logged in as admin\r\n");

		// admin enabled
		ftp->admin_enabled = 1;
	}
	else {
		// error, return
		ftp_send(ftp->ctrlconn, "530 \r\n");
		return 0;
	}

	// all good
	return 1;
}

// =========================================================
//
//                       Ftp server
//
// =========================================================

void ftp_service(struct netconn *ctrlcn, ftp_data_t *ftp) {
	uint16_t dummy;
	ip4_addr_t ippeer;
	time_t systemTimeBeginConnect;

	// variables initialization
	systemTimeBeginConnect = xTaskGetTickCount();
	strcpy(ftp->cwdName, "/");
	ftp->cwdRNFR[0] = 0;
	ftp->ctrlconn = ctrlcn;
	ftp->listdataconn = NULL;
	ftp->dataconn = NULL;
	ftp->dataPort = FTP_DATA_PORT + ftp->ftp_con_num;
	ftp->cmdStatus = 0;
	ftp->dataConnMode = NOTSET;
	ftp->admin_enabled = 0;

	//  Get the local and peer IP
	netconn_addr(ftp->ctrlconn, &ftp->ipserver, &dummy);
	netconn_peer(ftp->ctrlconn, &ippeer, &dummy);

	// send welcome message
	ftp_send(ftp->ctrlconn, "220 -> CMS FTP Server, FTP Version %s\r\n", FTP_VERSION);

	// feedback
	DEBUG_PRINT("Client connected!\r\n");

	// try and log in
	if (!ftp_log_in(ftp))
		goto close;

	//  Disconnect if FTP_TIME_OUT minutes of inactivity
	netconn_set_recvtimeout(ftp->ctrlconn, (FTP_TIME_OUT * 60000));

	// loop is exited using goto's
	while (true) {
		//  Wait for user commands
		int8_t err = readCommand(ftp);

		// time out?
		if (err == -4)
			goto close;

		// other error?
		if (err < 0)
			goto close;

		// processing error?
		if (!processCommand(ftp))
			goto bye;
	}

	// bye tag
	bye:

	// send goodbye command
	ftp_send(ftp->ctrlconn, "221 Goodbye\r\n");

	// close tag
	close:

	// Close the connections (to be sure)
	data_con_close(ftp);

	// delete listdataconn socket
	if (ftp->listdataconn != NULL) {
		netconn_close(ftp->listdataconn);
		netconn_delete(ftp->listdataconn);
	}

	//  Write data to log
	uint32_t timeConnect = (uint32_t) (xTaskGetTickCount() - systemTimeBeginConnect);
#warning commented out
	//strRTCDateTime(str, &rtcBeginTime);
	//strcpy(buf, "Connected at ");
	//strcat(buf, str);
	//strcat(buf, " for");
	//strSec2hms(str, timeConnect / ConfigTick_RATE_HZ, (timeConnect % ConfigTick_RATE_HZ) / 10);
	//strcat(buf, str);
	//strcat(buf, "\r\n");
	//sdl.line = buf;
	//strcpy(str, "/Log/");
	//strcat(str, ipaddr_ntoa(&ippeer));
	//strcat(str, ".log");
	//sdl.file = str;
	//sdl.append = false;
	//chMsgSend(tsdlog, (msg_t) & sdl);

	DEBUG_PRINT("Client disconnected\r\n");
}
