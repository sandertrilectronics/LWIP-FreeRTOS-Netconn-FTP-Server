# LWIP-FreeRTOS-Netconn-FTP-Server
 FTP Server for LWIP Netconn API. To be used in combination with FreeRTOS. Currently a work in progress.
 Started working from: https://github.com/gallegojm/STM32-E407-FtpServer
 This code was written in CPP. For every connection a thread is made and this thread is blocked by a semaphore. Each thread had it's own FTP class in the CPP code. I am rewriting this class to a structure. This structure contains all variables for that thread.
 Currently it is not a nice piece of code and a work in progress.