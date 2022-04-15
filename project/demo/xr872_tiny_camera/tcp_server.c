#include "tcp_server.h"

#include "kernel/os/os.h"
#include "common/framework/platform_init.h"
#include "net/wlan/wlan.h"
#include "common/framework/net_ctrl.h"
#include "net/nopoll/nopoll.h"
#include "lwip/sockets.h"
#include "driver/chip/psram/psram.h"

#include "camera_sensor_x.h"
#include "http_server.h"

#include "mimamori.h"



extern unsigned char *httpImgBuf;
extern unsigned int httpImgBufLen;
extern private_t *private;

static void tcp_server_fun(void *arg)
{
	int sockfd, connfd, len, rc; 
    struct sockaddr_in servaddr, cli; 
	
    printf("private %p =? %p\n", private, arg);

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
    } 
    else{
        printf("Server listening..\r\n"); 
    }
	len = sizeof(cli); 
	
	while(1){
		connfd = accept(sockfd, (struct sockaddr *)&cli, (unsigned int *)&len); 
		if (connfd < 0) {
			printf("server acccept failed...\r\n"); 
			continue ;
		}
		else{
			printf("server acccept the client...\r\n"); 
		}
		
		for (;;) { 

			//get camera data and send again
			if((httpImgBuf!=NULL)||(private!=NULL)){
				httpImgBufLen = getCameraSensorImg(httpImgBuf,HttpImgBufMaxLen,private);
				if(httpImgBufLen>0){
					int xlen,curPos;
					printf("get tcp img len:%d\r\n",httpImgBufLen);
					len = (httpImgBufLen+4);
					for (int i = 0; i < 16; i++) {
						printf("%02x", httpImgBuf[i]);
					}
					printf("\n");
					curPos = 0;
					for (;;) {
						xlen = len-curPos;
						if(xlen>4096){
							xlen = 4096;
						}
						if(xlen==0){
							rc = 0;
							break;
						}
						rc = write(connfd, &httpImgBuf[curPos], xlen);
						if (rc < 0) {
							break;
						}
						curPos += xlen;
					}
					if(rc<0){
						printf("write error and quit\r\n");
						break;
					}
				}else{
					printf("tcp get img error\r\n");
				}
			}else{
				break;
			}
		} 
		printf("socket quit now\r\n");
		// After chatting close the socket 
		close(connfd); 
	}
	
	
	
}





#define TCP_SERVER_THREAD_STACK_SIZE    (1024 * 5)
static OS_Thread_t tcp_server_task_thread;
void initTcpServer(void *arg)
{
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








