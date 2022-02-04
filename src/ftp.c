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

#include "ftp.h"

// ethernet include
#include "api.h"

// RTOS include
#include "FreeRTOS.h"
#include "event_groups.h"

// stdlib
#include <string.h>

// callbacks
__weak void ftp_connected_callback(void) {
}

__weak void ftp_disconnected_callback(void) {
}

// static variables
static const char *no_conn_allowed = "421 No more connections allowed\r\n";
static server_stru_t ftp_links[FTP_NBR_CLIENTS];

// single ftp connection loop
static void ftp_task(void *param) {
	// sanity check
	if (param == NULL)
		return;

	// parse parameter
	server_stru_t *ftp = (server_stru_t*) param;

	// save the instance number
	ftp->ftp_data.ftp_con_num = ftp->number;

	// callback
	ftp_connected_callback();

	// feedback
	log_print("FTP %d connected\r\n", ftp->number);

	// service FTP server
	ftp_service(ftp->ftp_connection, &ftp->ftp_data);

	// delete the connection.
	netconn_delete(ftp->ftp_connection);

	// reset the socket to be sure
	ftp->ftp_connection = NULL;

	// feedback
	log_print("FTP %d disconnected\r\n", ftp->number);

	// callback
	ftp_disconnected_callback();

	// delete this task
	vTaskDelete(NULL);

	// clear handle
	ftp->task_handle = NULL;
}

static void ftp_start_task(server_stru_t *data, uint8_t index) {
	// set number
	data->number = index;

	// change name
	char name[12] = { 0 };
	snprintf(name, 12, "ftp_task_%d", data->number);

	// start task with parameter
#if FTP_TASK_STATIC == 1
	data->task_handle == xTaskCreateStatic(ftp_task, name, FTP_TASK_STACK_SIZE, data, 5, data->task_stack, &data->task_static);
	if (data->task_handle == NULL) {
		// if creation of the task fails, close and clean up the connection
		netconn_close(data->ftp_connection);
		netconn_delete(data->ftp_connection);
		data->ftp_connection = NULL;

		// feedback to CMS log
		log_print("%s not started\r\n", name);
	}
#else
	if (xTaskCreate(ftp_task, name, FTP_TASK_STACK_SIZE, data, 2, data->task_handle) != pdPASS) {
		// if creation of the task fails, close and clean up the connection
		netconn_close(data->ftp_connection);
		netconn_delete(data->ftp_connection);
		data->ftp_connection = NULL;

		// feedback to CMS log
		log_print("%s not started\r\n", name);
	}
#endif
	else {
		// feedback to CMS log
		log_print("%s started\r\n", name);
	}
}

// ftp server task
void ftp_server(void) {
	struct netconn *ftp_srv_conn;
	struct netconn *ftp_client_conn;
	uint8_t index = 0;

	// Create the TCP connection handle
	ftp_srv_conn = netconn_new(NETCONN_TCP);

	// feedback
	if (ftp_srv_conn == NULL) {
		// error
		log_print("Failed to create socket\r\n");

		// go back
		return;
	}

	// Bind to port 21 (FTP) with default IP address
	netconn_bind(ftp_srv_conn, NULL, FTP_SERVER_PORT);

	// put the connection into LISTEN state
	netconn_listen(ftp_srv_conn);

	while (1) {
		// Wait for incoming connections
		if (netconn_accept(ftp_srv_conn, &ftp_client_conn) == ERR_OK) {
			// Look for the first unused connection
			for (index = 0; index < FTP_NBR_CLIENTS; index++) {
				if (ftp_links[index].ftp_connection == NULL && ftp_links[index].task_handle == NULL)
					break;
			}

			// all connections in use?
			if (index >= FTP_NBR_CLIENTS) {
				// tell that no connections are allowed
				netconn_write(ftp_client_conn, no_conn_allowed, strlen(no_conn_allowed), NETCONN_COPY);

				// delete the connection.
				netconn_delete(ftp_client_conn);

				// reset the socket to be sure
				ftp_client_conn = NULL;

				// feedback
				log_print("FTP connection denied, all connections in use\r\n");

				// wait a while
				vTaskDelay(500);
			}
			// not all connections in use
			else {
				// copy client connection
				ftp_links[index].ftp_connection = ftp_client_conn;

				// zero out client connection
				ftp_client_conn = NULL;

				// try and start the FTP task for this connection
				ftp_start_task(&ftp_links[index], index);
			}
		}
	}

	// delete the connection.
	netconn_delete(ftp_srv_conn);
}
