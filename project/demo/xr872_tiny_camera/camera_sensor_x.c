#include "camera_sensor_x.h"


#include <stdio.h>
#include <string.h>
#include "stdlib.h"

#include "FreeRTOS.h"
#include "queue.h"

#include "kernel/os/os_time.h"

#include "driver/component/csi_camera/camera.h"
#include "driver/chip/hal_i2c.h"

#include "driver/component/csi_camera/gc0308/drv_gc0308.h"

#include "mimamori.h"

#define JPEG_PSRAM_EN			(1)
#if JPEG_PSRAM_EN
#include "driver/chip/psram/psram.h"
#include "sys/dma_heap.h"

#define JPEG_PSRAM_SIZE			(620*1024)
#endif

#define SENSOR_I2C_ID 		I2C0_ID
#define SENSOR_RESET_PIN        GPIO_PIN_15
#define SENSOR_RESET_PORT       GPIO_PORT_A
#define SENSOR_POWERDOWN_PIN    GPIO_PIN_14
#define SENSOR_POWERDOWN_PORT   GPIO_PORT_A

#define JPEG_ONLINE_EN			(1)
#define JPEG_SRAM_SIZE 			(180*1024)

#define JPEG_MPART_EN			(0)
#if JPEG_MPART_EN
#define JPEG_BUFF_SIZE 			(8*1024)
#else
#define JPEG_BUFF_SIZE  		(50*1024)
#endif

#define JPEG_IMAGE_WIDTH		(640)
#define JPEG_IMAGE_HEIGHT		(480)

#define SENSOR_FUNC_INIT	HAL_GC0308_Init
#define SENSOR_FUNC_DEINIT	HAL_GC0308_DeInit
#define SENSOR_FUNC_IOCTL	HAL_GC0308_IoCtl

static CAMERA_Cfg camera_cfg = {
	/* jpeg config */
	.jpeg_cfg.jpeg_en = 1,
	.jpeg_cfg.quality = 60,
	.jpeg_cfg.jpeg_clk  = 0, //no use
	.jpeg_cfg.memPartEn = JPEG_MPART_EN,
	.jpeg_cfg.memPartNum = JPEG_MPART_EN ? JPEG_MEM_BLOCK4 : 0,
	.jpeg_cfg.jpeg_mode = JPEG_ONLINE_EN ? JPEG_MOD_ONLINE : JPEG_MOD_OFFLINE,
	.jpeg_cfg.width = JPEG_IMAGE_WIDTH,
	.jpeg_cfg.height = JPEG_IMAGE_HEIGHT,

	/* csi config */
	.csi_cfg.csi_clk = 24000000, // no use

	/* sensor config */
	.sensor_cfg.i2c_id = SENSOR_I2C_ID,
	.sensor_cfg.pwcfg.Pwdn_Port = SENSOR_POWERDOWN_PORT,
	.sensor_cfg.pwcfg.Reset_Port = SENSOR_RESET_PORT,
	.sensor_cfg.pwcfg.Pwdn_Pin = SENSOR_POWERDOWN_PIN,
	.sensor_cfg.pwcfg.Reset_Pin = SENSOR_RESET_PIN,

	.sensor_func.init = SENSOR_FUNC_INIT,
	.sensor_func.deinit = SENSOR_FUNC_DEINIT,
	.sensor_func.ioctl = SENSOR_FUNC_IOCTL,
};

static uint8_t* gmemaddr;
static CAMERA_Mgmt mem_mgmt;
static OS_Semaphore_t sem;

#if JPEG_MPART_EN
static uint8_t* gmpartaddr;
static uint32_t gmpartsize;
#endif

volatile unsigned char camera_restart_flag = 0;

void GC0308_SetLightMode(SENSOR_LightMode light_mode);
void GC0308_SetColorSaturation(SENSOR_ColorSaturation sat);
void GC0308_SetBrightness(SENSOR_Brightness bright);
void GC0308_SetContrast(SENSOR_Contarst contrast);
void GC0308_SetSpecialEffects(SENSOR_SpecailEffects eft);

static int camera_mem_create(CAMERA_JpegCfg *jpeg_cfg, CAMERA_Mgmt *mgmt)
{
	uint8_t* addr;
	uint8_t* end_addr;

	if (JPEG_PSRAM_EN) {
		addr = (uint8_t*)dma_malloc(JPEG_PSRAM_SIZE, DMAHEAP_PSRAM);
		if (addr == NULL) {
			printf("malloc fail\n");
			return -1;
		}
		memset(addr, 0 , JPEG_PSRAM_SIZE);
		end_addr = addr + JPEG_PSRAM_SIZE;
	} else {
		addr = (uint8_t*)malloc(JPEG_SRAM_SIZE);//imgbuf;
		if (addr == NULL) {
			printf("malloc fail\n");
				return -1;
		}
		memset(addr, 0 , JPEG_SRAM_SIZE);
		end_addr = addr + JPEG_SRAM_SIZE;
	}
	printf("malloc addr: %p -> %p\n", addr, end_addr);

	mgmt->yuv_buf.addr = (uint8_t *)ALIGN_16B((uint32_t)addr);
	mgmt->yuv_buf.size = camera_cfg.jpeg_cfg.width*camera_cfg.jpeg_cfg.height*3/2;
#if JPEG_ONLINE_EN
	mgmt->jpeg_buf[0].addr = (uint8_t *)ALIGN_1K((uint32_t)addr + CAMERA_JPEG_HEADER_LEN);
#else
	mgmt->jpeg_buf[0].addr = (uint8_t *)ALIGN_1K((uint32_t)mgmt->yuv_buf.addr + CAMERA_JPEG_HEADER_LEN +
							mgmt->yuv_buf.size);//after yuv data
#endif
	mgmt->jpeg_buf[0].size = JPEG_BUFF_SIZE;

	if ((mgmt->yuv_buf.addr + mgmt->yuv_buf.size) > end_addr ||
			(mgmt->jpeg_buf[0].addr + mgmt->jpeg_buf[0].size) > end_addr) {
		printf("aadr exceeded\n");
		return -1;
	}
	gmemaddr = addr;

	return 0;
}

static void camera_mem_destroy()
{
	if (gmemaddr) {
		if (JPEG_PSRAM_EN)
			dma_free(gmemaddr, DMAHEAP_PSRAM);
		else
			free(gmemaddr);
		gmemaddr = NULL;
	}
}

#if JPEG_MPART_EN
static void camera_cb(CAMERA_CapStatus status, void *arg)
{
	switch (status) {
		case CAMERA_STATUS_MPART:
		{
			static uint32_t offset = 0;
			uint8_t* addr = gmpartaddr;
			CAMERA_MpartBuffInfo *stream = (CAMERA_MpartBuffInfo*)arg;

			memcpy(addr+offset, mem_mgmt.jpeg_buf[stream->buff_index].addr+(uint32_t)stream->buff_offset, stream->size);
			offset += stream->size;
			if (stream->tail) { /* encode one jpeg finish */
				gmpartsize = offset;
				offset = 0;
				OS_SemaphoreRelease(&sem);
			}
			break;
		}
		default:
			break;
	}
}

#if 0
int camera_get_mpart_jpeg()
{
	int res = 0;
	FIL fp;
	uint32_t bw;
	gmpartaddr = (uint8_t*)dma_malloc(45*1024, DMAHEAP_PSRAM);
	if (gmpartaddr == NULL) {
		printf("malloc fail\n");
		return -1;
	}

	f_unlink("test.jpg");
	res = f_open(&fp, "test.jpg", FA_WRITE | FA_CREATE_NEW);
	if (res != FR_OK) {
		printf("open file error %d\n", res);
		res = -1;
		goto exit_2;
	}

	res = f_write(&fp, mem_mgmt.jpeg_buf[0].addr-CAMERA_JPEG_HEADER_LEN, CAMERA_JPEG_HEADER_LEN, &bw);
	if (res != FR_OK || bw < CAMERA_JPEG_HEADER_LEN) {
		printf("write fail(%d), line%d..\n", res, __LINE__);
		res = -1;
		goto exit_1;
	}

	HAL_CAMERA_CaptureStream();

	if (OS_SemaphoreWait(&sem, 5000) != OS_OK) {
		printf("sem wait fail\n");
		res = -1;
		goto exit_1;
	}

	res = f_write(&fp, gmpartaddr, gmpartsize, &bw);
	if (res != FR_OK || bw < gmpartsize) {
		printf("write fail(%d), line%d..\n", res, __LINE__);
		res = -1;
		goto exit_1;
	}
    printf("write jpeg ok\n");
exit_1:
	f_close(&fp);
exit_2:
	dma_free(gmpartaddr, DMAHEAP_PSRAM);

	return res;
}
#endif

int camera_get_mpart_jpeg()
{
	int res = 0;

	gmpartaddr = (uint8_t*)dma_malloc(45*1024, DMAHEAP_PSRAM);
	if (gmpartaddr == NULL) {
		printf("malloc fail\n");
		return -1;
	}
	memset(gmpartaddr, 0 , 45*1024);

	HAL_CAMERA_CaptureMpartStart(0);

	if (OS_SemaphoreWait(&sem, 5000) != OS_OK) {
		printf("sem wait fail\n");
		dma_free(gmpartaddr, DMAHEAP_PSRAM);
		return -1;
	}

	for (int i =0; i < (CAMERA_JPEG_HEADER_LEN); i++)
		printf("%02x ", *(mem_mgmt.jpeg_buf[0].addr+i-CAMERA_JPEG_HEADER_LEN));
	for (int i =0; i < (gmpartsize); i++)
		printf("%02x ", *(gmpartaddr+i));
	printf("\nprint jpeg ok\n");

	OS_MSleep(300);

	dma_free(gmpartaddr, DMAHEAP_PSRAM);

	return res;
}

#endif

static int camera_init()
{
    if (OS_SemaphoreCreateBinary(&sem) != OS_OK) {
        printf("sem create fail\n");
        return -1;
    }

    /* malloc mem */
	memset(&mem_mgmt, 0, sizeof(CAMERA_Mgmt));
	if (camera_mem_create(&camera_cfg.jpeg_cfg, &mem_mgmt) != 0)
		return -1;

	/* camera init */
	camera_cfg.mgmt = &mem_mgmt;
#if JPEG_MPART_EN
	camera_cfg.cb = &camera_cb;
#endif
	if (HAL_CAMERA_Init(&camera_cfg) != HAL_OK) {
		printf("%s fail, %d\n", __func__, __LINE__);
		return -1;
	}

	GC0308_SetLightMode(LIGHT_HOME);

	return 0;
}

//static uint32_t max_cost = 0;
//static uint32_t max_size = 0;
int camera_get_image(private_t *p)
{
	uint8_t *addr = NULL;
	OS_Status status;
	CAMERA_JpegBuffInfo jpeg_info;
	CAMERA_OutFmt fmt;
	fmt = CAMERA_OUT_YUV420;

	//これは元の画像を取得するためのものです
	//uint32_t time = OS_TicksToMSecs(OS_GetTicks());
	int ret = HAL_CAMERA_CaptureImage(fmt, &jpeg_info, 1);
	//uint32_t cost = OS_TicksToMSecs(OS_GetTicks()) - time;

	if (ret == -1) {
		printf("capture image failed\r\n");
		return -1;
	}
	//if (cost > max_cost) max_cost = cost;

	if (camera_cfg.jpeg_cfg.jpeg_en) {
		ret = HAL_CAMERA_CaptureImage(CAMERA_OUT_JPEG, &jpeg_info, 0);
		if (ret == -1) {
			printf("CAMERA_OUT_JPEG failed\r\n");
			return -1;
		}
		//printf("Q:%d, jpeg image cost: %d ms, size: %d bytes\n", camera_cfg.jpeg_cfg.quality, cost, jpeg_info.size);
		//if (jpeg_info.size > max_size) max_size = jpeg_info.size;

		//printf("jpeg_info.buff_index: %d\n", jpeg_info.buff_index);


		/* jpeg data*/
		jpeg_info.size += CAMERA_JPEG_HEADER_LEN - 4;
		addr = mem_mgmt.jpeg_buf[jpeg_info.buff_index].addr - CAMERA_JPEG_HEADER_LEN + 4;

		if (jpeg_info.size <= JPEG_BUFF_SIZE) {
			status = OS_MutexLock(&p->jpeg_mu, 5000);
			if (status != OS_OK) {
				printf("Failed: OS_MutexLock\n");
				return -1;
			}
			memcpy(p->jpeg_buf, addr, jpeg_info.size);
			p->jpeg_len = jpeg_info.size;
			//if (p->jpeg_ready) printf("Thrown a picture!\n");
			p->jpeg_ready = 1;
			OS_MutexUnlock(&p->jpeg_mu);
		} else {
			printf("JPEG_BUFF_SIZE small for img:%d : %d\r\n", jpeg_info.size, JPEG_BUFF_SIZE);
			camera_cfg.jpeg_cfg.quality = 30; 
			camera_restart_flag = 1;
			return -1;
		}
	}

	return 0;
}

/*
int getCameraStatus(void *arg)
{
	private_t *p = (private_t*) arg;
	p->max_cost = max_cost;
	max_cost = 0;
	p->max_size = max_size;
	max_size = 0;
	p->quality = camera_cfg.jpeg_cfg.quality;
	return 0;
}
*/

static void camera_deinit()
{
	OS_SemaphoreDelete(&sem);

	HAL_CAMERA_DeInit();

	camera_mem_destroy();
}



unsigned char frameCount = 0;
///thread
static void thread_camera_Fun(void *arg){
	private_t *p = (private_t*) arg;
	OS_Status ret;
	int32_t do_capture = 0;
	p->jpeg_buf = malloc(JPEG_BUFF_SIZE);
	if (p->jpeg_buf == NULL) {
		printf("malloc");
		exit(EXIT_FAILURE);
	}
	memset(p->jpeg_buf, 0, JPEG_BUFF_SIZE);
	camera_init();
	while(1){
		if(camera_restart_flag!=0){
			printf("thread_camera_Fun restart camera now width:%d  height:%d quality:%d\r\n",camera_cfg.jpeg_cfg.width, camera_cfg.jpeg_cfg.height, camera_cfg.jpeg_cfg.quality );
			camera_deinit();
			OS_MSleep(100);
			camera_init();
			OS_MSleep(100);
			camera_restart_flag = 0;
		}
		do {
			ret = OS_MutexLock(&p->mu, 5000);
			if (ret != OS_OK) {
				printf("Failed: OS_MutexLock\n");
				break;
			}
			do_capture = p->do_capture;
			p->do_capture = 0;
			OS_MutexUnlock(&p->mu);
			if (do_capture == 1) break;	// every 100 ms
			OS_MSleep(1);
		} while (1);
		camera_get_image(p);
		frameCount++;
	}
	
	free(p->jpeg_buf);
	camera_deinit();
}

unsigned char getCameraFrameCount(void)
{
	unsigned char x = 0;
	x = frameCount;
	frameCount = 0;
	return x;
}


#define THREAD_CAMERA_STACK_SIZE	(1024 * 5)
OS_Thread_t thread_camera;

//カメラを初期化します
void initCameraSensor(void *arg)
{
	if (OS_ThreadCreate(&thread_camera,"thread_camera",thread_camera_Fun,arg,OS_PRIORITY_NORMAL,THREAD_CAMERA_STACK_SIZE) != OS_OK) {			
		printf("thread camera create error\n");
	}
	
	printf("init camera thread ok\r\n");
}

//jpeg画像を取得します
int getCameraSensorImg(unsigned char *buf,unsigned int maxLen, void *private)
{
	private_t *p = (private_t*) private;
	unsigned int imgLen = 0;
	OS_Status ret;
	uint32_t ready = 0;
	if (p == NULL) {
		printf("Failed: p == NULL\n");
		return -1;
	}
	uint32_t time = OS_TicksToMSecs(OS_GetTicks());
	while (ready == 0) {
		ret = OS_MutexLock(&p->jpeg_mu, 5000);
		if (ret != OS_OK) {
			printf("Failed: OS_MutexLock\n");
			OS_MSleep(1);
			return -1;
		}
		ready = p->jpeg_ready;
		if (ready) {
			memcpy(buf, p->jpeg_buf, p->jpeg_len);
			p->jpeg_ready = 0;
			imgLen = p->jpeg_len;
		}
		OS_MutexUnlock(&p->jpeg_mu);
		if (ready == 0) OS_MSleep(1);
	}
	uint32_t cost = OS_TicksToMSecs(OS_GetTicks()) - time;
	printf("getCameraSensorImg cost:%u\n", cost);
	return imgLen;
}
//カメラの初期化を終了します
void deinitCameraSensor(void)
{
	
}

void restartCameraByParam(unsigned int width,unsigned int height,unsigned int quality)
{
	camera_cfg.jpeg_cfg.quality = quality;
	camera_cfg.jpeg_cfg.width = width;
	camera_cfg.jpeg_cfg.height = height ;
	camera_restart_flag = 1;
}


//设置灯光模式	0自动	1日照模式	2阴天模式	3办公室模式		4居家模式
void cameraSetLightMode(unsigned char mode)
{
	switch(mode){
		case 0:
		printf("LIGHT_AUTO\r\n");
		GC0308_SetLightMode(LIGHT_AUTO);
		break;
		case 1:
		printf("LIGHT_SUNNY\r\n");
		GC0308_SetLightMode(LIGHT_SUNNY);
		break;
		case 2:
		printf("LIGHT_COLUDY\r\n");
		GC0308_SetLightMode(LIGHT_COLUDY);
		break;
		case 3:
		printf("LIGHT_OFFICE\r\n");
		GC0308_SetLightMode(LIGHT_OFFICE);
		break;
		case 4:
		printf("LIGHT_HOME\r\n");
		GC0308_SetLightMode(LIGHT_HOME);
		break;
		default:
		printf("LIGHT_AUTO\r\n");
		GC0308_SetLightMode(LIGHT_AUTO);
		break;
	}
}

//设置色彩饱和度		0(-2)	1(-1)	2(0)	3(1)	4(2)
void cameraSetColorSaturation(unsigned char cs)
{
	switch(cs){
		case 0:
		printf("COLOR_SATURATION_0\r\n");
		GC0308_SetColorSaturation(COLOR_SATURATION_0);
		break;
		case 1:
		printf("COLOR_SATURATION_1\r\n");
		GC0308_SetColorSaturation(COLOR_SATURATION_1);
		break;
		case 2:
		printf("COLOR_SATURATION_2\r\n");
		GC0308_SetColorSaturation(COLOR_SATURATION_2);
		break;
		case 3:
		printf("COLOR_SATURATION_3\r\n");
		GC0308_SetColorSaturation(COLOR_SATURATION_3);
		break;
		case 4:
		printf("COLOR_SATURATION_4\r\n");
		GC0308_SetColorSaturation(COLOR_SATURATION_4);
		break;
		default:
		printf("COLOR_SATURATION_2\r\n");
		GC0308_SetColorSaturation(COLOR_SATURATION_2);
		break;
	}
}

//设置亮度		0(-2)	1(-1)	2(0)	3(1)	4(2)
void cameraSetBrightness(unsigned char brightness)
{
	switch(brightness){
		case 0:
		printf("BRIGHT_0\r\n");
		GC0308_SetBrightness(BRIGHT_0);
		break;
		case 1:
		printf("BRIGHT_1\r\n");
		GC0308_SetBrightness(BRIGHT_1);
		break;
		case 2:
		printf("BRIGHT_2\r\n");
		GC0308_SetBrightness(BRIGHT_2);
		break;
		case 3:
		printf("BRIGHT_3\r\n");
		GC0308_SetBrightness(BRIGHT_3);
		break;
		case 4:
		printf("BRIGHT_4\r\n");
		GC0308_SetBrightness(BRIGHT_4);
		break;
		default:
		printf("BRIGHT_2\r\n");
		GC0308_SetBrightness(BRIGHT_2);
		break;
	}
}

//设置对比度		0(-2)	1(-1)	2(0)	3(1)	4(2)
void cameraSetContrast(unsigned char contrast)
{
	switch(contrast){
		case 0:
		printf("CONTARST_0\r\n");
		GC0308_SetContrast(CONTARST_0);
		break;
		case 1:
		printf("CONTARST_1\r\n");
		GC0308_SetContrast(CONTARST_1);
		break;
		case 2:
		printf("CONTARST_2\r\n");
		GC0308_SetContrast(CONTARST_2);
		break;
		case 3:
		printf("CONTARST_3\r\n");
		GC0308_SetContrast(CONTARST_3);
		break;
		case 4:
		printf("CONTARST_4\r\n");
		GC0308_SetContrast(CONTARST_4);
		break;
		default:
		printf("CONTARST_2\r\n");
		GC0308_SetContrast(CONTARST_2);
		break;
	}
}

//设置特殊效果	0正常模式		1负片模式		2黑白模式		3纯红		4纯绿		5纯蓝		6旧片模式
void cameraSpecialEffects(unsigned char se){
	switch(se){
		case 0:
		printf("IMAGE_NOMAL\r\n");
		GC0308_SetSpecialEffects(IMAGE_NOMAL);
		break;
		case 1:
		printf("IMAGE_NEGATIVE\r\n");
		GC0308_SetSpecialEffects(IMAGE_NEGATIVE);
		break;
		case 2:
		printf("IMAGE_BLACK_WHITE\r\n");
		GC0308_SetSpecialEffects(IMAGE_BLACK_WHITE);
		break;
		case 3:
		printf("IMAGE_SLANT_RED\r\n");
		GC0308_SetSpecialEffects(IMAGE_SLANT_RED);
		break;
		case 4:
		printf("IMAGE_SLANT_GREEN\r\n");
		GC0308_SetSpecialEffects(IMAGE_SLANT_GREEN);
		break;
		case 5:
		printf("IMAGE_SLANT_BLUE\r\n");
		GC0308_SetSpecialEffects(IMAGE_SLANT_BLUE);
		break;
		case 6:
		printf("IMAGE_VINTAGE\r\n");
		GC0308_SetSpecialEffects(IMAGE_VINTAGE);
		break;
		default:
		printf("IMAGE_NOMAL\r\n");
		GC0308_SetSpecialEffects(IMAGE_NOMAL);
		break;
	}
}




