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

#ifndef _FTPS_H_
#define _FTPS_H_

#include "lwip/opt.h"
#include "lwip/arch.h"
#include "lwip/api.h"

#include "ff.h"

#define FTP_VERSION				"2020-02-19"

#define FTP_USER_NAME			"user"
#define FTP_USER_PASS			"user"
#define FTP_ADMIN_NAME			"oxipack"
#define FTP_ADMIN_PASS			"admin"

#define FTP_SERVER_PORT         21
#define FTP_DATA_PORT           55600         // Data port in passive mode
#define FTP_TIME_OUT            10            // Disconnect client after 5 minutes of inactivity
#define FTP_PARAM_SIZE          _MAX_LFN + 8
#define FTP_CWD_SIZE            _MAX_LFN + 8  // max size of a directory name
#define FTP_CMD_SIZE			5
#define FTP_STR_SIZE			25

// number of clients we want to serve simultaneously
#define FTP_NBR_CLIENTS          2

// size of file buffer for reading a file
#define FTP_BUF_SIZE             512

// Data Connection mode:
typedef enum {
	NOTSET = 0,    //   not set
	PASSIVE = 1,    //   passive
	ACTIVE = 2,    //   active
} dcm_type;

typedef struct {
	// sockets
	struct netconn *listdataconn;
	struct netconn *dataconn;
	struct netconn *ctrlconn;
	struct netbuf *inbuf;

	// ip addresses
	ip4_addr_t ipclient;
	ip4_addr_t ipserver;

	// port
	uint16_t dataPort;

	// file variables
	FIL file;
	FILINFO finfo;
	char lfn[_MAX_LFN + 1];

	// command sent by client
	char command[FTP_CMD_SIZE];

	// parameters sent by client
	char parameters[FTP_PARAM_SIZE];

	// name of current directory
	char cwdName[FTP_CWD_SIZE];

	// name of origin directory for Rename command
	char cwdRNFR[FTP_CWD_SIZE];

	//
	char path[FTP_CWD_SIZE];

	//
	char str[FTP_STR_SIZE];

	// status of ftp command connection
	int8_t cmdStatus;

	// connection mode (active or passive)
	uint8_t ftp_con_num;

	// admin enabled or not
	uint8_t admin_enabled;

	//
	dcm_type dataConnMode;
} ftp_data_t;

// define a structure of parameters for a ftp thread
typedef struct {
	uint8_t number;
	struct netconn *ftp_connection;
	SemaphoreHandle_t sem_request;
	ftp_data_t ftp_data;
} server_stru_t;

void ftp_service(struct netconn *dscn, ftp_data_t *ftp);

#endif // _FTPS_H_
