#include "http_server.h"

#include "kernel/os/os.h"
#include "common/framework/platform_init.h"
#include "net/wlan/wlan.h"
#include "common/framework/net_ctrl.h"
#include "net/nopoll/nopoll.h"
#include "lwip/sockets.h"
#include "driver/chip/psram/psram.h"

#include "camera_sensor_x.h"

#define UNUSED(arg)  ((void)arg)

struct query_t {
	char val[24];
};

struct header_property_t {
	char key[32];
	char value[128];
};

struct request_t {
	char method[8];
	char protocol[12];
	char url[128];

	/* query data, very constrained regarding memory resources */
	size_t nquery; /* number of queries */
	struct query_t query[8];
	int content_length;
};

struct response_t {
	char head[256];
};

struct server_t {
	int sock;
	struct sockaddr_in addr;

	int (*func_bad_request)(int, const struct request_t *);
	int (*func_request)(int, const struct request_t *);
};

struct client_t {
	int sock;
	struct sockaddr_in addr;
	socklen_t len;
};

static int str_append(char * s, size_t len, char c)
{
	size_t l = strlen(s);
	if (l < len) {
		s[l]= c;
		return 0;
	}
	return -1;
}


//处理摄像头的尺寸和质量改变指令
int dealHttpCmdCameraSizeAndQuality(const struct request_t * req);

//处理摄像头工作环境改变指令
int dealHttpCmdCameraWorkEnv(const struct request_t * req);


//#define HttpImgBufMaxLen			(100*1024)			
unsigned char *httpImgBuf;
unsigned int httpImgBufLen = 0;


static int method_append(struct request_t * r, char c)
{
	return str_append(r->method, sizeof(r->method)-1, c);
}

static int protocol_append(struct request_t * r, char c)
{
	return str_append(r->protocol, sizeof(r->protocol)-1, c);
}

static void request_clear(struct request_t * r)
{
	memset(r, 0, sizeof(struct request_t));
}

static int url_append(struct request_t * r, char c)
{
	return str_append(r->url, sizeof(r->url)-1, c);
}

static int query_append(struct request_t * r, char c)
{
	if (r->nquery >= sizeof(r->query) / sizeof(struct query_t))
		return -1;
	return str_append(r->query[r->nquery].val, sizeof(r->query[r->nquery].val)-1, c);
}

static int query_next(struct request_t * r)
{
	if (r->nquery >= sizeof(r->query) / sizeof(struct query_t))
		return -1;
	r->nquery++;
	return 0;
}

static void clear(char * s, size_t len)
{
	memset(s, 0, len);
}

static void clear_header_property(struct header_property_t * prop)
{
	clear(prop->key, sizeof(prop->key));
	clear(prop->value, sizeof(prop->value));
}

static int append(char * s, size_t len, char c)
{
	return str_append(s, len, c);
}

/**
 * Parses received data from \c client_sock and sets corresponding
 * fields of the specified request data structure.
 *
 * \param[in] client_sock Client socket to read data from.
 * \param[out] r The request data structure to fill.
 * \return \c 0 on success, the negative state number in which the
 *   error ocurred.
 */
static int parse(int client_sock, struct request_t * r)
{
	int state = 0; /* state machine */
	int read_next = 1; /* indicator to read data */
	char c = 0; /* current character */
	char buffer[16]; /* receive buffer */
	int buffer_index = sizeof(buffer); /* index within the buffer */
	int content_length = -1; /* used only in POST requests */
	struct header_property_t prop; /* temporary space to hold header key/value properties*/

	request_clear(r);
	clear_header_property(&prop);
	while (client_sock >= 0) {

		/* read data */
		if (read_next) {

			/* read new data, buffers at a time */
			if (buffer_index >= (int)sizeof(buffer)) {
				int rc;

				memset(buffer, 0, sizeof(buffer));
				rc = read(client_sock, buffer, sizeof(buffer));
				if (rc < 0)
					return -99; /* read error */
				if (rc == 0)
					return 0; /* no data read */
				buffer_index = 0;
			}
			c = buffer[buffer_index];
			++buffer_index;

			/* state management */
			read_next = 0;
		}

		/* execute state machine */
		switch (state) {
			case 0: /* kill leading spaces */
				if (isspace((int)c)) {
					read_next = 1;
				} else {
					state = 1;
				}
				break;
			case 1: /* method */
				if (isspace((int)c)) {
					state = 2;
				} else {
					if (method_append(r, c))
						return -state;
					read_next = 1;
				}
				break;
			case 2: /* kill spaces */
				if (isspace((int)c)) {
					read_next = 1;
				} else {
					state = 3;
				}
				break;
			case 3: /* url */
				if (isspace((int)c)) {
					state = 5;
				} else if (c == '?') {
					read_next = 1;
					state = 4;
				} else {
					if (url_append(r, c))
						return -state;
					read_next = 1;
				}
				break;
			case 4: /* queries */
				if (isspace((int)c)) {
					if (query_next(r))
						return -state;
					state = 5;
				} else if (c == '&') {
					if (query_next(r))
						return -state;
					read_next = 1;
				} else {
					if (query_append(r, c))
						return -state;
					read_next = 1;
				}
				break;
			case 5: /* kill spaces */
				if (isspace((int)c)) {
					read_next = 1;
				} else {
					state = 6;
				}
				break;
			case 6: /* protocol */
				if (isspace((int)c)) {
					state = 7;
				} else {
					if (protocol_append(r, c))
						return -state;
					read_next = 1;
				}
				break;
			case 7: /* kill spaces */
				if (isspace((int)c)) {
					read_next = 1;
				} else {
					clear_header_property(&prop);
					state = 8;
				}
				break;
			case 8: /* header line key */
				if (c == ':') {
					state = 9;
					read_next = 1;
				} else {
					if (append(prop.key, sizeof(prop.key)-1, c))
						return -state;
					read_next = 1;
				}
				break;
			case 9: /* kill spaces */
				if (isspace((int)c)) {
					read_next = 1;
				} else {
					state = 10;
				}
				break;
			case 10: /* header line value */
				if (c == '\r') {
					if (strcmp("Content-Length", prop.key) == 0)
						content_length = strtol(prop.value, 0, 0);
					clear_header_property(&prop);
					state = 11;
					read_next = 1;
				} else {
					if (append(prop.value, sizeof(prop.value)-1, c))
						return -state;
					read_next = 1;
				}
				break;
			case 11:
				if (c == '\n') {
					read_next = 1;
				} else if (c == '\r') {
					state = 12;
					read_next = 1;
				} else {
					state = 8;
				}
				break;
			case 12: /* end of header */
				if (c == '\n') {
					if (content_length > 0) {
						state = 13;
						read_next = 1;
					} else {
						return 0; /* end of header, no content => end of request */
					}
				} else {
					state = 8;
				}
				break;
			case 13: /* content (POST queries) */
				if (c == '&') {
					if (query_next(r))
						return -state;
					read_next = 1;
				} else if (c == '\r') {
					if (query_next(r))
						return -state;
					read_next = 1;
				} else if (c == '\n') {
					read_next = 1;
				} else if (c == '\0') {
					if (query_next(r))
						return -state;
					return 0; /* end of content */
				} else {
					if (query_append(r, c))
						return -state;
					read_next = 1;
				}
				break;
		}
	}
	return -99;
}


static void print_req(int rc, struct request_t * r)
{
	size_t i;
	if (rc) {
		printf("\nERROR: invalid request: %d", rc);
	} else {
		printf("MET:[%s]\n", r->method);
		printf("PRT:[%s]\n", r->protocol);
		printf("URL:[%s]\n", r->url);
		for (i = 0; i < r->nquery; ++i) 
			printf("QRY:[%s]\n", r->query[i].val);
	}
	printf("\n");
}

static int run_server(struct server_t * server)
{
	struct client_t client;
	struct request_t r;
	int rc;

	for (;;) {
		client.sock = -1;
		client.len = sizeof(client.addr);

		client.sock = accept(server->sock, (struct sockaddr *)&client.addr, &client.len);
		if (client.sock < 0)
			return -1;
		rc = parse(client.sock, &r);
		print_req(rc, &r);
		if (rc == 0) {
			if (server->func_request)
				server->func_request(client.sock, &r);
		} else {
			if (server->func_bad_request)
				server->func_bad_request(client.sock, &r);
		}
		shutdown(client.sock, SHUT_WR);
		close(client.sock);
		//OS_MSleep(10);
	}
}

static int request_bad(int sock, const struct request_t * req)
{
	static const char * RESPONSE =
		"HTTP/1.1 400 Bad Request\r\n"
		"Content-Type: text/html\r\n"
		"Pragma: no-cache\r\n"
		"Cache-Control: no-cache\r\n"
		"Connection: close\r\n"
		"\r\n"
		"<html><body>Bad Request</body></html>\r\n";

	int length = 0;

	UNUSED(req);

	length = strlen(RESPONSE);
	return write(sock, RESPONSE, length) == length ? 0 : -1;
}

static void response_init(struct response_t * res)
{
	memset(res->head, 0, sizeof(res->head));
}

static int response_append_content_type(struct response_t * res, const char * mime)
{
	static const char * TEXT = "Content-Type: ";

	if (strlen(res->head) > (sizeof(res->head) - strlen(TEXT) - strlen(mime) - 2))
		return -1;
	strcat(res->head, TEXT);
	strcat(res->head, mime);
	strcat(res->head, "\r\n");
	return 0;
}

static int response_append(struct response_t * res, const char * text, size_t len)
{
	const size_t n = sizeof(res->head) - strlen(res->head);
	if (len > n)
		return -1;
	strncat(res->head, text, n);
	return 0;
}

static int response_content_length(struct response_t * res, unsigned int content_len)
{
	char TEXT[64];
	memset(TEXT,0,64);
	sprintf(TEXT,"Content-Length: %d\r\n",content_len);
	return response_append(res, TEXT, strlen(TEXT));;
}

static int response_append_no_cache(struct response_t * res)
{
	static const char * TEXT =
		"Pragma: no-cache\r\n"
		"Cache-Control: no-cache\r\n"
		"Access-Control-Allow-Origin: *\r\n";
	return response_append(res, TEXT, strlen(TEXT));
}

static int response_append_connection_close(struct response_t * res)
{
	static const char * TEXT = "Connection: close\r\n";
	return response_append(res, TEXT, strlen(TEXT));
}

static int response_append_header_start(struct response_t * res)
{
	static const char * TEXT = "HTTP/1.1 200 OK\r\n";
	return response_append(res, TEXT, strlen(TEXT));
}

static int response_append_header_end(struct response_t * res)
{
	static const char * TEXT = "\r\n";
	return response_append(res, TEXT, strlen(TEXT));
}

static int send_header_img(int sock,unsigned int imglen)
{
	int len;
	struct response_t res;
	
	response_init(&res);
	response_append_header_start(&res);
	response_append_content_type(&res,"image/jpeg");
	response_content_length(&res,imglen);
	response_append_no_cache(&res);
	response_append_connection_close(&res);
	response_append_header_end(&res);
	len = (int)strlen(res.head);
	return write(sock, res.head, len) == len ? 0 : -1;
}

static int send_img_file(int sock,unsigned char *buf,unsigned int len)
{
	unsigned int xlen = 0;
	unsigned int curPos = 0;
	int rc;
	if (send_header_img(sock, len) >= 0) {
		for (;;) {
			xlen = len-curPos;
			if(xlen>4096){
				xlen = 4096;
			}
			if(xlen==0)
				break;
			rc = write(sock, &buf[curPos], xlen);
			if (rc < 0) {
				break;
			}
			curPos += xlen;
		}
	}
	if(curPos == len){
		return 0;
	}else{
		printf("send http img error %d:%d\r\n",curPos,len);
		return -1;
	}
	
}

static int send_header_mime(int sock, const char * mime)
{
	int len;
	struct response_t res;

	response_init(&res);
	response_append_header_start(&res);
	response_append_content_type(&res, mime);
	response_append_no_cache(&res);
	response_append_connection_close(&res);
	response_append_header_end(&res);

	len = (int)strlen(res.head);
	return write(sock, res.head, len) == len ? 0 : -1;
}

static int request_send_file(int sock, const struct request_t * req, const char * filename)
{
	//int fd = 0;
	//int rc;
	//char buf[256];

	UNUSED(req);
	//这里返回需要的网页服务
	/*
	fd = open(filename, O_RDONLY);
	if (fd < 0)
		return -1;

	if (send_header_mime(sock, "text/html") >= 0) {
		for (;;) {
			rc = read(fd, buf, sizeof(buf));
			if (rc <= 0) break;
			rc = write(sock, buf, rc);
			if (rc < 0) break;
		}
	}
	close(fd);
	*/
	return 0;
}

static int request_send_ok(int sock, const struct request_t * req)
{
	UNUSED(req);
	int length = 0;
	const char * OK_RESPONSE =
		"HTTP/1.1 200 OK\r\n"
		"Content-Type: text/html\r\n"
		"Pragma: no-cache\r\n"
		"Cache-Control: no-cache\r\n"
		"Connection: close\r\n"
		"\r\n"
		"OK\r\n";
	length = strlen(OK_RESPONSE);
	return (write(sock, OK_RESPONSE, length) == length) ? 0 : -1;
}

static int request_send_fail(int sock, const struct request_t * req)
{
	UNUSED(req);
	int length = 0;
	const char * FAIL_RESPONSE =
		"HTTP/1.1 200 OK\r\n"
		"Content-Type: text/html\r\n"
		"Pragma: no-cache\r\n"
		"Cache-Control: no-cache\r\n"
		"Connection: close\r\n"
		"\r\n"
		"FAIL\r\n";
	length = strlen(FAIL_RESPONSE);
	return (write(sock, FAIL_RESPONSE, length) == length) ? 0 : -1;
}


static int request_response(int sock, const struct request_t * req)
{
	static const char * RESPONSE =
		"HTTP/1.1 200 OK\r\n"
		"Content-Type: text/html\r\n"
		"Pragma: no-cache\r\n"
		"Cache-Control: no-cache\r\n"
		"Connection: close\r\n"
		"\r\n"
		"<html><body>Welcome (default response)</body></html>\r\n";

	int length = 0;

	UNUSED(req);

	printf("get uri:%s  query:%d\r\n",req->url,req->nquery);
	if (strcmp(req->url, "/") == 0) {
	
		return request_send_file(sock, req, "index.html");
	
	} else if(strcmp(req->url, "/img.jpg") == 0){
		
		
		if(httpImgBuf!=NULL){
			httpImgBufLen = getCameraSensorImg(httpImgBuf,HttpImgBufMaxLen);
			if(httpImgBufLen>0){
				printf("get http img uri:%d\r\n",httpImgBufLen);
				return send_img_file(sock, &httpImgBuf[4],httpImgBufLen);
			}else{
				printf("http get img error\r\n");
			}
		}else{
			printf("http img buf error\r\n");
		}
		
		length = strlen(RESPONSE);
		return (write(sock, RESPONSE, length) == length) ? 0 : -1;		
	
	} else if(strcmp(req->url, "/cmd_whq") == 0){
		dealHttpCmdCameraSizeAndQuality(req);
		//返回OK
		return request_send_ok(sock,req);
	} else if(strcmp(req->url, "/cmd_env") == 0){
		dealHttpCmdCameraWorkEnv(req);
		//返回OK
		return request_send_ok(sock,req);
	}else {
	
		length = strlen(RESPONSE);
		return (write(sock, RESPONSE, length) == length) ? 0 : -1;
	
	}
}



static void http_server_fun(void *arg)
{
	static struct server_t server;
	const int reuse = 1;

	memset(&server, 0, sizeof(server));
	server.sock = -1;

	server.sock = socket(AF_INET, SOCK_STREAM, 0);
	if (server.sock < 0) {
		printf("socket");
		return ;
	}

	memset(&server.addr, 0, sizeof(server.addr));
	server.addr.sin_family = AF_INET;
	server.addr.sin_port = htons(8080);
	server.addr.sin_addr.s_addr = htonl(INADDR_ANY);
	if (bind(server.sock, (const struct sockaddr *)&server.addr, sizeof(server.addr)) < 0) {
		printf("bind");
		return ;
	}

	if (setsockopt(server.sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
		printf("setsockopt");
		return ;
	}

	if (listen(server.sock, 2) < 0) {
		printf("listen");
		return ;
	}

	server.func_bad_request = request_bad;
	server.func_request = request_response;
	
	run_server(&server);
	return ;
}



//处理摄像头的尺寸和质量改变指令
int dealHttpCmdCameraSizeAndQuality(const struct request_t * req){

	int i = 0;
	char *tmp = NULL;
	int width = 0;
	int height = 0;
	int quality = 0;
	for(i=0;i<req->nquery;i++){
		printf("query %d str:%s\r\n",i,req->query[i].val);
		tmp = strstr(req->query[i].val,"width");
		if(tmp!=NULL){
			tmp = tmp+6;
			width = atoi(tmp);
		}
		tmp = strstr(req->query[i].val,"height");
		if(tmp!=NULL){
			tmp = tmp+7;
			height = atoi(tmp);
		}
		tmp = strstr(req->query[i].val,"quality");
		if(tmp!=NULL){
			tmp = tmp+8;
			quality = atoi(tmp);
		}
	}
	if(width!=0 && height!=0 && quality!=0){
		restartCameraByParam(width,height,quality);
	}
	
	printf("CameraRestart  widht:%d height:%d  quality:%d\r\n",width,height,quality);
	return 0;
}

//处理摄像头工作环境改变指令
int dealHttpCmdCameraWorkEnv(const struct request_t * req){
	int i = 0;
	char *tmp = NULL;
	
	int cmd = 0;
	int value = 0;
	
	for(i=0;i<req->nquery;i++){
		printf("query %d str:%s\r\n",i,req->query[i].val);
		tmp = strstr(req->query[i].val,"cmd");
		if(tmp!=NULL){
			tmp = tmp+4;
			cmd = atoi(tmp);
		}
		tmp = strstr(req->query[i].val,"value");
		if(tmp!=NULL){
			tmp = tmp+6;
			value = atoi(tmp);
		}
	}
	
	if(cmd!=0){
		switch(cmd){
			case 1:
				cameraSetLightMode(value);
				break;
			case 2:
				cameraSetColorSaturation(value);
				break;
			case 3:
				cameraSetBrightness(value);
				break;
			case 4:
				cameraSetContrast(value);
				break;
			case 5:
				cameraSpecialEffects(value);
				break;
		}
	}
	
	printf("Camera Cmd:%d Value:%d\r\n",cmd,value);
	
	return 0;
}


#define HTTP_SERVER_THREAD_STACK_SIZE    (1024 * 5)
static OS_Thread_t http_server_task_thread;
void initHttpServer(void)
{
	//httpImgBuf = (uint8_t*)dma_malloc(HttpImgBufMaxLen, DMAHEAP_PSRAM);

	httpImgBuf = (uint8_t*)psram_malloc(HttpImgBufMaxLen);
	if (httpImgBuf == NULL) {
		printf("malloc httpImgBuf fail\r\n");
	}
	if (OS_ThreadCreate(&http_server_task_thread,
                        "http_server",
                        http_server_fun,
                        NULL,
                        OS_THREAD_PRIO_APP,
                        HTTP_SERVER_THREAD_STACK_SIZE) != OS_OK) {
        printf("http server thread create error\r\n");
    }
	printf("http server init ok\r\n");
}






