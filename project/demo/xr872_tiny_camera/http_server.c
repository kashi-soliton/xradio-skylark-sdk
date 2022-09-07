#define _GNU_SOURCE

#include "http_server.h"
#include "kernel/os/os.h"
#include "common/framework/platform_init.h"
#include "net/wlan/wlan.h"
#include "common/framework/net_ctrl.h"
#include "common/framework/sysinfo.h"
#include "net/nopoll/nopoll.h"
#include "lwip/sockets.h"
#include "driver/chip/psram/psram.h"
#include "driver/chip/hal_prcm.h"
#include "driver/chip/hal_wdg.h"

#include "camera_sensor_x.h"
#include "mm_i2s.h"
#include "mimamori.h"
#include "net/ping/ping.h"

#define UDP_H_RPORT	8080
#define UDP_V_RPORT	10101
#define UDP_A_RPORT	10103
#define UDP_M_RPORT	10105
static char MSG[] = "mimamori";
static char MSGAP[] = "mimamori_ap";

#define UNUSED(arg)  ((void)arg)

struct sockaddr_in ar_addr;

static const uint8_t wave_hdr[] = {
	0x52, 0x49, 0x46, 0x46, 0xff, 0xff, 0xff, 0xff,
	0x57, 0x41, 0x56, 0x45, 0x66, 0x6d, 0x74, 0x20,
	0x10, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00,
	0x40, 0x1f, 0x00, 0x00, 0x80, 0x3e, 0x00, 0x00,
	0x02, 0x00, 0x10, 0x00, 0x64, 0x61, 0x74, 0x61,
	0xff, 0xff, 0xff, 0xff,
};

static int send_header_img(int sock,unsigned int imglen);

static void URL_dec(char *out, char* in,  int n)
{
	int i = 0, j = 0;
	char* c = NULL, hex[3] = {0};
	while (i < n) {
		if (in[i] == '%') {
			i++;
			strncpy(hex, &in[i], 2);
			out[j] = (uint8_t) (strtoul(hex, &c, 16) & 0xff);
			i++;
		} else {
			out[j] = in[i];
		}
		i++;
		j++;
	}
}

static int get_send(int fd, int w_header)
{
	uint8_t *jpeg_buf;
	uint32_t jpeg_size;
	int rc = 0;

	jpeg_buf = malloc_jpeg();
	jpeg_size = 0;

	if (jpeg_buf != NULL) {
		rc = -1;
		while (rc != 0) {
			rc = getImg(jpeg_buf, &jpeg_size);
			if (rc == 0) break;
			OS_MSleep(3);
		}
		if (w_header) {
			send_header_img(fd, jpeg_size);
		}
		if (jpeg_size > 0) {
			rc = write(fd, jpeg_buf, jpeg_size);
			if(rc<0){
				printf("write connfd error.\n");
				return -1;
			}
		}else{
			printf("tcp get img error\r\n");
		}
	}
	free_jpeg(jpeg_buf);
	return 0;
}

struct query_t {
	//char val[24];
	char val[32];
};

struct header_property_t {
	char key[32];
	char value[256];
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
static struct client_t client;

static int str_append(char * s, size_t len, char c)
{
	size_t l = strlen(s);
	if (l < len) {
		s[l]= c;
		return 0;
	}
	return -1;
}


int dealHttpCmdSetSsid(const struct request_t * req);

//处理摄像头的尺寸和质量改变指令
int dealHttpCmdCameraSizeAndQuality(const struct request_t * req);

//处理摄像头工作环境改变指令
int dealHttpCmdCameraWorkEnv(const struct request_t * req);


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
	char buffer[128]; /* receive buffer */
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
				//printf("New recv:%s\n", buffer);
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
					printf("url:%s\n", r->url);
				} else if (c == '?') {
					read_next = 1;
					state = 4;
					printf("url:%s\n", r->url);
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
					if (append(prop.value, sizeof(prop.value)-1, c)) {
						printf("%c,prop.value:%s\n", c, prop.value);
						return -state;
					}
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
	//struct client_t client;
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

/*
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
*/

/*
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
*/

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

/*
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
*/


static int request_response(int sock, const struct request_t * req)
{
	//unsigned int httpImgBufLen = 0;
	static const char * RES_TEMP =
		"HTTP/1.1 200 OK\r\n"
		"Content-Type: text/html\r\n"
		"Pragma: no-cache\r\n"
		"Cache-Control: no-cache\r\n"
		"Connection: close\r\n"
		"\r\n"
		"model: %s\r\n"
		"verion: %s\r\n"
		"width: %u\r\n"
		"height: %u\r\n"
		"quality: %u\r\n";

		//"<html><body>Welcome (default response)</body></html>\r\n";
	
	static char RESPONSE[512] = {0};

	int length = 0;

	UNUSED(req);

	uint32_t width, height, quality;
	getCameraParam(&width, &height, &quality);
	snprintf(RESPONSE, 512, RES_TEMP, mimamori.model, mimamori.version, width, height, quality);

	//printf("get uri:%s  query:%d\r\n",req->url,req->nquery);
	if(strcmp(req->url, "/img.jpg") == 0){
		return get_send(sock, 1);
		//length = strlen(RESPONSE);
		//return (write(sock, RESPONSE, length) == length) ? 0 : -1;		
	
	} else if(strcmp(req->url, "/restart") == 0){
		HAL_PRCM_SetCPUABootFlag(PRCM_CPUA_BOOT_FROM_COLD_RESET);
		HAL_WDG_Reboot();
		return request_send_ok(sock,req);
	} else if(strcmp(req->url, "/set_ssid") == 0){
		dealHttpCmdSetSsid(req);
		//返回OK
		return request_send_ok(sock,req);
	} else if(
			(strcmp(req->url, "/set_whq") == 0) ||
			(strcmp(req->url, "/cmd_whq") == 0)
		 ){
		dealHttpCmdCameraSizeAndQuality(req);
		//返回OK
		return request_send_ok(sock,req);
	//} else if(strcmp(req->url, "/cmd_env") == 0){
		//dealHttpCmdCameraWorkEnv(req);
		////返回OK
		//return request_send_ok(sock,req);
	} else if(strcmp(req->url, "/start") == 0){
		printf("start reqest\n");
		return request_send_ok(sock,req);
	} else if(strcmp(req->url, "/stop") == 0){
		printf("stop reqest\n");
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

int dealHttpCmdSetSsid(const struct request_t * req){

	struct sysinfo *si = sysinfo_get();
	int i = 0;
	char *tmp = NULL;
	char ssid[32] = {0};
	char psk[32] = {0};
	for(i=0;i<req->nquery;i++){
		tmp = strcasestr(req->query[i].val,"S:");
		if(tmp!=NULL){
			tmp = tmp+2;
			memset(ssid, 0, 32);
			URL_dec(ssid, tmp, strlen(tmp));
		}
		tmp = strcasestr(req->query[i].val,"P:");
		if(tmp!=NULL){
			tmp = tmp+2;
			memset(psk, 0, 32);
			URL_dec(psk, tmp, strlen(tmp));
		}
		tmp = strstr(req->query[i].val,"ssid");
		if(tmp!=NULL){
			tmp = tmp+5;
			memset(ssid, 0, 32);
			URL_dec(ssid, tmp, strlen(tmp));
		}
		tmp = strstr(req->query[i].val,"psk");
		if(tmp!=NULL){
			tmp = tmp+4;
			memset(psk, 0, 32);
			URL_dec(psk, tmp, strlen(tmp));
		}
	}
	if ((strlen(ssid) > 0) && (strlen(psk) > 0)) {
		uint8_t *pssid = si->wlan_sta_param.ssid;
		uint8_t *ppsk = si->wlan_sta_param.psk;
		memset(pssid, 0, SYSINFO_SSID_LEN_MAX);
		memset(ppsk, 0, SYSINFO_PSK_LEN_MAX);
		memcpy(pssid, ssid, strlen((char*)ssid));
		memcpy(ppsk, psk, strlen((char*)psk));
		si->wlan_mode = WLAN_MODE_STA;
		sysinfo_save();
		printf("new ssid:%s, psk:%s\n", pssid, ppsk);
	}
	return 0;
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

static const char *mjpeg_s_hdr = 
	"HTTP/1.1 OK\r\n"
	"Server: MJPEGStreamer/1.0\r\n"
	"Content-Type: multipart/x-mixed-replace;boundary=ImgBoundary\r\n"
	"\r\n"
	;
static const char *mjpeg_bou =
	"--ImgBoundary"
	"\r\n"
	;
static const char *mjpeg_hdr =
	"Mime-Type: image/jpeg\r\n"
	"Content-Type: image/jpeg\r\n"
	"Content-Length: %d\r\n"
	"\r\n"
	;
static const char *mjpeg_crlf =
	"\r\n"
	;

static int write_mjpeg_s_hdr(int fd)
{
	char buf[128] = {0};
	int rc, len;
	len = snprintf(buf, 128, "%s%s", mjpeg_s_hdr, mjpeg_bou);
	rc = write(fd, buf, len);
	if (rc != len) {
		printf("%s: write error, %d != %d\n", __func__, rc, len);
		return -1;
	}
	//printf("%s:%d\n", __func__, len);
	return len;
}

static int write_mjpeg_tail(int fd)
{
	int rc, len;
	char buf[64] = {0};
	len = snprintf(buf, 64, "%s%s", mjpeg_crlf, mjpeg_bou);
	rc = write(fd, buf, len);
	if (rc != len) {
		printf("%s: write error, %d != %d\n", __func__, rc, len);
		return -1;
	}
	return len;
}

static int write_mjpeg_hdr(int fd, int size, int *total)
{
	char buf[128];
	int rc;
	int h_size = strlen(mjpeg_hdr);
	*total = size + strlen(mjpeg_hdr) + strlen(mjpeg_crlf) + strlen(mjpeg_bou);
	memset(buf, 0, 128);
	snprintf(buf, 128, mjpeg_hdr, *total);
	while (h_size != strlen(buf)) {
		//printf("h_size: %d total: %d\n", h_size, *total);
		h_size = strlen(buf);
		*total = size + h_size + strlen(mjpeg_crlf) + strlen(mjpeg_bou);
		memset(buf, 0, 128);
		snprintf(buf, 128, mjpeg_hdr, *total);
		//h_size = strlen(buf);
		//*total = size + h_size + strlen(mjpeg_crlf) + strlen(mjpeg_bou);
		//printf("h_size: %d total: %d\n", h_size, *total);
	}
	rc = write(fd, buf, h_size);
	if (rc != h_size) {
		printf("%s: write error, %d != %d\n", __func__, rc, h_size);
		return -1;
	}
	return rc;
}

static int write_mjpeg(int fd, int *pkt_len)
{
	uint8_t *jpeg_buf;
	uint32_t jpeg_size;
	int sent = 0, rc = 0;

	jpeg_buf = malloc_jpeg();
	if (jpeg_buf == NULL) {
		return -1;
	}
	jpeg_size = 0;

	rc = -1;
	while (rc != 0) {
		rc = getImg(jpeg_buf, &jpeg_size);
		if (rc == 0) break;
		OS_MSleep(3);
	}
	if (jpeg_size <= 0) {
		printf("tcp get img error\r\n");
		sent = -1;
		goto exit_0;
	}
	rc = write_mjpeg_hdr(fd, jpeg_size, pkt_len);
	if (rc < 0) {
		sent = rc;
		goto exit_0;
	}
	sent += rc;
	rc = write(fd, jpeg_buf, jpeg_size);
	if (rc < 0) {
		printf("write connfd error.\n");
		sent = rc;
		goto exit_0;
	}
	sent += rc;
	rc = write_mjpeg_tail(fd);
	if (rc < 0) {
		sent = -1;
		goto exit_0;
	}
	sent += rc;

exit_0:

	free_jpeg(jpeg_buf);
	return sent;
}

static in_addr_t s_addr = 0;
static void mjpeg_server_fun(void *arg)
{
	int sockfd, connfd, len, pkt_len, rc;
	struct sockaddr_in servaddr, cli; 
	char rbuf[256];
	const char *GET = "GET";
	int request_get = 0;
	int request_end = 0;
	int i;

 	// socket create and verification 
	sockfd = socket(AF_INET, SOCK_STREAM, 0); 
	if (sockfd == -1) { 
		printf("socket creation failed...\r\n"); 
		return ;
	} 
	else{
		printf("%s: Socket successfully created..\r\n", __func__); 
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
		request_get = 0;
		request_end = 0;
		connfd = accept(sockfd, (struct sockaddr *)&cli, (unsigned int *)&len); 
		if (connfd < 0) {
			printf("server acccept failed...\r\n"); 
			continue ;
		}
		while (connfd >= 0) {
			rc = read(connfd, rbuf, sizeof(rbuf));
			if (rc <= 0) {
				shutdown(connfd, SHUT_RDWR);
				close(connfd);
				connfd = -1;
				s_addr = 0;
				printf("break after read. rc=%d\n", rc);
				break;
			}
			if (rc > strlen(GET)) {
				if (strncmp(rbuf, GET, strlen(GET)) == 0) {
					request_get = 1;
				}
			}
			for (i = 0; i < rc; i++) {
				if (rbuf[i] == '\r') {
					if (request_end == 0) request_end = 1;
					else if (request_end == 2) request_end = 3;
					else request_end = 0;
				} else if (rbuf[i] == '\n') {
					if (request_end == 1) request_end = 2;
					else if (request_end == 3) request_end = 4;
					else request_end = 0;
				} else {
					request_end = 0;
				}
			}
			//printf("rc=%d\tconnfd=%d\t%d\t%d\n", rc, connfd, request_end, request_get);
			if (request_end == 4 && request_get == 1) {
				rc = write_mjpeg_s_hdr(connfd);
				if (rc <= 0) {
					shutdown(connfd, SHUT_RDWR);
					close(connfd);
					connfd = -1;
					s_addr = 0;
					break;
				}
				s_addr = cli.sin_addr.s_addr;
				ar_addr = cli;
				while ((rc = write_mjpeg(connfd, &pkt_len)) > 0) //NULL;
				{
					NULL;
				}
				shutdown(connfd, SHUT_RDWR);
				close(connfd);
				connfd = -1;
				s_addr = 0;
				printf("break after jpeg send done\n");
				break;
			}
		}
	}
}

static void audio_server_fun(void *arg)
{
	int sockfd = -1;
	uint8_t *pos;
	uint32_t size;
	OS_Status ret;
	ssize_t rc;
	int flag = 0;

	while (1) {
		flag = s_addr == 0? 0: 1;
		if ((sockfd != -1) && (flag == 0)) {
			if (sockfd != -1) {
				i2s_stop(); printf("i2s_stop.\n");
				close(sockfd);
				sockfd = -1;
			}
		}
		if ((sockfd == -1) && (flag != 0)) {
			ar_addr.sin_port = htons(UDP_A_RPORT);
			sockfd = socket(AF_INET, SOCK_DGRAM, 0); 
			if (sockfd < 0) { 
				printf("socket creation failed...\r\n"); 
				//return ;
			} else{
				printf("%s: Socket successfully created..\r\n", __func__); 
			}
			i2s_start(); printf("i2s_start.\n");
			i2s_clear_all_flags();
			rc = sendto(sockfd, wave_hdr, sizeof(wave_hdr), 0,
				(struct sockaddr *)&ar_addr,
				sizeof(ar_addr));
			if (rc != size) {
				printf("%s: wite error. ret=%d\n", __func__, rc);
				//s_addr = 0;
			}
		}
		ret = OS_SemaphoreWait(i2s_get_sem(), 500);
		if (ret == OS_OK && s_addr != 0) {
			i2s_get_data(&pos, &size);
			rc = sendto(sockfd, pos, size, 0,
				(struct sockaddr *)&ar_addr,
				sizeof(ar_addr));
		} else {
			OS_MSleep(100);
		}
	}
}

static int send_msg(uint32_t addr, char *msg)
{
	struct sockaddr_in mr_addr = {0};
	int sockfd = socket(AF_INET, SOCK_DGRAM, 0); 
	if (sockfd < 0) { 
		printf("socket creation failed...\r\n"); 
		return -1;
	} else {
		mr_addr.sin_addr.s_addr = addr;
		mr_addr.sin_family = AF_INET;
		mr_addr.sin_port = htons(UDP_M_RPORT);
		sendto(sockfd, msg, strlen(msg), 0,
				(struct sockaddr *)&mr_addr,
				sizeof(mr_addr));
		close(sockfd);
		return 0;
	}
}

void mimamori_udpmsg()
{
	struct netif *nif = NULL;
	int num, ret;

	nif = g_wlan_netif;
	if ((nif == NULL) || (netif_is_link_up(nif) == 0)) return;
	if (wlan_if_get_mode(nif) == 0) {	// STA mode
		send_msg(nif->gw.addr, MSG);
	} else {				// AP mode
		int i;
		ip_addr_t sin_addr = nif->ip_addr;
		ret = wlan_ap_sta_num(&num);
		if ((ret == 0)&&(num > 0)) {
			for (i = 100; i < 110; i++) {
				sin_addr.addr = sin_addr.addr & 0x00ffffff;
				sin_addr.addr += i << 24;
				send_msg(sin_addr.addr, MSGAP);
			}
		}
	}
}

static void announce_fun(void *arg)
{
	while (1) {
		mimamori_udpmsg();
		OS_Sleep(1);
	}
}

#define HTTP_SERVER_THREAD_STACK_SIZE    (1024 * 2)
#define MJPEG_SERVER_THREAD_STACK_SIZE    (1024 * 2)
#define AUDIO_SERVER_THREAD_STACK_SIZE    (1024 * 1)
#define ANNOUNCE_SERVER_THREAD_STACK_SIZE    (1024 * 1)
static OS_Thread_t http_server_task_thread;
static OS_Thread_t mjpeg_server_task_thread;
static OS_Thread_t audio_server_task_thread;
//static OS_Thread_t announce_server_task_thread;
void initHttpServer(void *arg)
{
	if (OS_ThreadCreate(&http_server_task_thread,
				"http_server",
				http_server_fun,
				arg,
				OS_THREAD_PRIO_APP,
				HTTP_SERVER_THREAD_STACK_SIZE) != OS_OK) {
		printf("http server thread create error\r\n");
	}
	printf("http server init ok\r\n");
	if (OS_ThreadCreate(&mjpeg_server_task_thread,
                        "mjpeg_server",
                        mjpeg_server_fun,
			arg,
                        OS_THREAD_PRIO_APP,
                        MJPEG_SERVER_THREAD_STACK_SIZE) != OS_OK) {
		printf("tcp thread create error\r\n");
		return ;
	}
	printf("tcp server init ok\r\n");
	if (OS_ThreadCreate(&audio_server_task_thread,
                        "audio_server",
                        audio_server_fun,
			arg,
                        OS_THREAD_PRIO_APP,
                        AUDIO_SERVER_THREAD_STACK_SIZE) != OS_OK) {
		printf("audio thread create error\r\n");
		return ;
	}
	printf("audio server init ok\r\n");
	/*
	if (OS_ThreadCreate(&announce_server_task_thread,
                        "announce_server",
                        announce_fun,
			arg,
                        OS_THREAD_PRIO_APP,
                        ANNOUNCE_SERVER_THREAD_STACK_SIZE) != OS_OK) {
		printf("announce thread create error\r\n");
		return ;
	}
	printf("announce server init ok\r\n");
	*/
}

