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

#include "ftps.h"
#include "FreeRTOS.h"
#include "semaphore.h"

// =========================================================
//
//  FTP server thread.
//
// =========================================================

server_stru_t ftp_links[FTP_NBR_CLIENTS];

void ftp_loop(void *param) {
	// parse parameter
	server_stru_t *ftp = (server_stru_t *) param;

	// save the instance number
	ftp->ftp_data.ftp_con_num = ftp->number;

	// loop until infinity
	while (1) {
		// wait until we may run
		if (xSemaphoreTake(ftp->sem_request, portMAX_DELAY) == pdFALSE)
			continue;

		// give the semaphore back
		xSemaphoreGive(ftp->sem_request);

		// feedback
		printf("FTP %d connected\r\n", ftp->number);

		// service FTP server
		ftp_service(ftp->ftp_connection, &ftp->ftp_data);

		// delete the connection.
		netconn_delete(ftp->ftp_connection);

		// reset the socket to be sure
		ftp->ftp_connection = NULL;

		// feedback
		printf("FTP %d disconnected\r\n", ftp->number);
	}
}

void ftp_server(void) {
	struct netconn * ftpsrvconn;
	uint8_t i = 0;
	char name[12];

	for (i = 0; i < FTP_NBR_CLIENTS; i++) {
		// create semaphore
		ftp_links[i].sem_request = xSemaphoreCreateMutex();

		// Immediately take the semaphore to block the thread that is going to be started
		xSemaphoreTake(ftp_links[i].sem_request, 0);

		// set number
		ftp_links[i].number = i;

		// change name
		snprintf(name, 12, "ftp_task_%d", ftp_links[i].number);

		// start task with parameter
		if (xTaskCreate(ftp_loop, name, 512, &ftp_links[i], 2, NULL) != pdPASS)
			printf("%s not started\r\n", name);
		else
			printf("%s started\r\n", name);
	}

	// Create the TCP connection handle
	ftpsrvconn = netconn_new(NETCONN_TCP);

	// feedback
	if (ftpsrvconn == NULL) {
		// error
		printf("Failed to create socket\r\n");

		// go back
		return;
	}

	// Bind to port 21 (FTP) with default IP address
	netconn_bind(ftpsrvconn, NULL, FTP_SERVER_PORT);

	// put the connection into LISTEN state
	netconn_listen(ftpsrvconn);

	while (1) {
		// Look for the first unused connection
		for (i = 0; i < FTP_NBR_CLIENTS; i++) {
			if (ftp_links[i].ftp_connection == NULL)
				break;
		}

		// all connections in use?
		if (i >= FTP_NBR_CLIENTS) {
			vTaskDelay(500);
		}
		// not all connections used, try and connect
		else if (netconn_accept(ftpsrvconn, &ftp_links[i].ftp_connection) == ERR_OK) {
			// let the loop run
			xSemaphoreGive(ftp_links[i].sem_request);

			// let the ftp thread run and take the semaphore
			vTaskDelay(100);

			// immediately take the semaphore to block the thread when the
			// ftp connection is dropped
			xSemaphoreTake(ftp_links[i].sem_request, portMAX_DELAY);
		}
	}

	// delete the connection.
	netconn_delete(ftpsrvconn);
}
