#include "tcp_server.h"

#include "kernel/os/os.h"
#include "cedarx/os_glue/include/unistd.h"
#include "common/framework/platform_init.h"
#include "net/wlan/wlan.h"
#include "common/framework/net_ctrl.h"
#include "net/nopoll/nopoll.h"
#include "lwip/sockets.h"
#include "driver/chip/psram/psram.h"

#include "camera_sensor_x.h"
#include "http_server.h"

#include "mimamori.h"



extern private_t *private;

static void tcp_server_fun(void *arg)
{
	int sockfd, connfd, len, rc;
	struct sockaddr_in servaddr, cli; 
	
	// socket create and verification 
	sockfd = socket(AF_INET, SOCK_STREAM, 0); 
	if (sockfd == -1) { 
		printf("socket creation failed...\r\n"); 
		return ;
	} 
	else{
		printf("Socket successfully created..\r\n"); 
	}
	
	bzero(&servaddr, sizeof(servaddr)); 
	
	servaddr.sin_family = AF_INET; 
	servaddr.sin_addr.s_addr = htonl(INADDR_ANY); 
	servaddr.sin_port = htons(10101); 
	
	// Binding newly created socket to given IP and verification 
	if ((bind(sockfd, (const struct sockaddr *)&servaddr, sizeof(servaddr))) != 0) { 
		printf("socket bind failed...\r\n"); 
		return ; 
	}
	else{
		printf("Socket successfully binded..\r\n"); 
	}
	
	// Now server is ready to listen and verification 
	if ((listen(sockfd, 5)) != 0) { 
		printf("Listen failed...\r\n"); 
		return ; 
	} else {
		printf("Server listening..\r\n"); 
	}
	len = sizeof(cli); 
	
	while(1){
		connfd = accept(sockfd, (struct sockaddr *)&cli, (unsigned int *)&len); 
		if (connfd < 0) {
			printf("server acccept failed...\r\n"); 
			continue ;
		}
		//get camera data and send again
		if (private->jpeg_buf != NULL) {
			//count = getImgNum();
			//if (count == 3) printf("image queue is full.\n");
			rc = -1;
			while (rc != 0) {
				rc = getImg(private->jpeg_buf, &private->jpeg_size);
				if (rc == 0) break;
				OS_MSleep(3);
			}
			if (private->jpeg_size > 0) {
				rc = write(connfd, private->jpeg_buf, private->jpeg_size);
				if(rc<0){
					printf("write connfd error.\n");
					break;
				}
			}else{
				printf("tcp get img error\r\n");
			}
		}
		shutdown(connfd, SHUT_RDWR);
		close(connfd);
		//printf("socket quit now\r\n");
	}
}





#define TCP_SERVER_THREAD_STACK_SIZE    (1024 * 5)
static OS_Thread_t tcp_server_task_thread;
void initTcpServer(void *arg)
{
	private = (private_t *) arg;
	if (OS_ThreadCreate(&tcp_server_task_thread,
                        "tcp_server",
                        tcp_server_fun,
                        arg,
                        OS_THREAD_PRIO_APP,
                        TCP_SERVER_THREAD_STACK_SIZE) != OS_OK) {
		printf("tcp thread create error\r\n");
		return ;
	}
	printf("tcp server init ok\r\n");
}

