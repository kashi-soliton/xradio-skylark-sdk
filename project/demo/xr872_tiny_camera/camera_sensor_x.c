#include "camera_sensor_x.h"


#include <stdbool.h>

#include <stdio.h>
#include <string.h>
#include "stdlib.h"

#include "kernel/os/os.h"

//#include "common/apps/recorder_app.h"
#include "kernel/os/os_time.h"
#include "audio/pcm/audio_pcm.h"
#include "audio/manager/audio_manager.h"

#include "lwip/sockets.h"
#include "lwip/netif.h"
#include "lwip/sockets.h"

#include "driver/component/csi_camera/camera.h"
#include "driver/component/csi_camera/camera_sensor.h"
#include "driver/component/csi_camera/gc0308/drv_gc0308.h"
#include "driver/component/csi_camera/gc0328c/drv_gc0328c.h"
#include "driver/chip/hal_i2c.h"
#include "driver/chip/psram/psram.h"


//#include "tft_lcd.h"


/* 摄像头测试 */
#define IMAGE_SENSOR_I2CID 		I2C0_ID
#define SENSOR_RESET_PIN        GPIO_PIN_15
#define SENSOR_RESET_PORT       GPIO_PORT_A
#define SENSOR_POWERDOWN_PIN    GPIO_PIN_14
#define SENSOR_POWERDOWN_PORT   GPIO_PORT_A

//不需要分模块

#define JPEG_PSRAM_EN			(1)
#define JPEG_PSRAM_SIZE			(700*1024)

#define IMAGE_WIDTH				(640)
#define IMAGE_HEIGHT			(480)

//#define IMAGE_WIDTH			(320)
//#define IMAGE_HEIGHT			(240)


#define SENSOR_FUNC_INIT	HAL_GC0308_Init
#define SENSOR_FUNC_DEINIT	HAL_GC0308_DeInit
#define SENSOR_FUNC_IOCTL	HAL_GC0308_IoCtl


static CAMERA_Cfg camera_cfg = {
	.jpeg_cfg.jpeg_en = 1,
	.jpeg_cfg.quality = 80,
	.jpeg_cfg.jpeg_clk  = 0, //no use

	.jpeg_cfg.memPartEn = 0,
	.jpeg_cfg.memPartNum = 0,

	.jpeg_cfg.jpeg_mode = JPEG_MOD_OFFLINE,

	.csi_cfg.csi_clk = 24000000, // no use

	/* sensor config */
	.sensor_cfg.i2c_id = IMAGE_SENSOR_I2CID,
	.sensor_cfg.pwcfg.Pwdn_Port = SENSOR_POWERDOWN_PORT,
	.sensor_cfg.pwcfg.Reset_Port = SENSOR_RESET_PORT,
	.sensor_cfg.pwcfg.Pwdn_Pin = SENSOR_POWERDOWN_PIN,
	.sensor_cfg.pwcfg.Reset_Pin = SENSOR_RESET_PIN,
	.sensor_cfg.pixel_size.width = IMAGE_WIDTH,//640,
	.sensor_cfg.pixel_size.height = IMAGE_HEIGHT,//480,
	.sensor_cfg.pixel_outfmt = YUV422_YUYV,
	//.sensor_cfg.pixel_outfmt = RGB565,
	
	.sensor_func.init = SENSOR_FUNC_INIT,
	.sensor_func.deinit = SENSOR_FUNC_DEINIT,
	.sensor_func.ioctl = SENSOR_FUNC_IOCTL,
};


static CAMERA_Mgmt mem_mgmt;

//创建信号发送buf
QueueHandle_t cameraJpegSendQueue;
#define CameraQueueLength		1
#define CameraQueueItemSize		(64*1024)

//图片进行信号发送的时候，需要在前面加上长度，因此需要一个单独的Buffer	jpeg图像的Buffer
unsigned char *cameraBuf;		





volatile unsigned char camera_restart_flag = 0;

static int camera_mem_create(CAMERA_JpegCfg *jpeg_cfg, CAMERA_Mgmt *mgmt)
{

	uint8_t* addr;
	
	addr = (uint8_t*)psram_malloc(JPEG_PSRAM_SIZE + 2048);//imgbuf;
	if (addr == NULL) {
		printf("malloc fail\n");
		return -1;
	}
	memset(addr, 0 , JPEG_PSRAM_SIZE + 2048);
	printf("malloc addr: %p -> %p\n", addr, addr + JPEG_PSRAM_SIZE + 2048);
	mgmt->yuv_buf = (uint8_t *)ALIGN_16B((uint32_t)addr);
	
	//mgmt->jpeg_buf = (uint8_t *)ALIGN_1K((uint32_t)mgmt->yuv_buf + CAMERA_JPEG_HEADER_LEN + camera_cfg.sensor_cfg.pixel_size.width * camera_cfg.sensor_cfg.pixel_size.height * 3/2);
	
	mgmt->jpeg_buf = (uint8_t *)ALIGN_1K((uint32_t)mgmt->yuv_buf + CAMERA_JPEG_HEADER_LEN + camera_cfg.sensor_cfg.pixel_size.width * camera_cfg.sensor_cfg.pixel_size.height * 2);
	
	
	mgmt->org_addr = addr;

	return 0;	

}

static void camera_mem_destroy()
{
	if (mem_mgmt.org_addr) {
		psram_free(mem_mgmt.org_addr);
		mem_mgmt.org_addr = NULL;
	}

	if (mem_mgmt.online_jpeg_mempart_last_buf) {
		free(mem_mgmt.online_jpeg_mempart_last_buf);
		mem_mgmt.online_jpeg_mempart_last_buf = NULL;
	}
}

static int camera_init()
{
    /* malloc mem */
	memset(&mem_mgmt, 0, sizeof(CAMERA_Mgmt));
	if (camera_mem_create(&camera_cfg.jpeg_cfg, &mem_mgmt) != 0)
		return -1;

	/* camera init */
	camera_cfg.mgmt = &mem_mgmt;
	if (HAL_CAMERA_Init(&camera_cfg) != HAL_OK) {
		printf("%s fail, %d\n", __func__, __LINE__);
		return -1;
	}

	//上电就将模块设置为 居家模式，显示效果较好
	GC0308_SetLightMode(LIGHT_HOME);
	return 0;
}


int camera_get_image()
{
	uint8_t *addr = NULL;
	CAMERA_OutFmt fmt;
	fmt = CAMERA_OUT_YUV420;

	//这个是获取原始图片
	uint32_t size = HAL_CAMERA_CaptureImage(fmt, 1);
	if (size <= 0) {
		printf("capture image failed\r\n");
		return -1;
	}
	addr = mem_mgmt.yuv_buf;
	//printf("capture image: %p  size:%d bytes\r\n", addr,size);
	//updateCameraTFT(addr,size);
	
	if (camera_cfg.jpeg_cfg.jpeg_en) {
		//获取转码后的图片
		uint32_t encode_size = HAL_CAMERA_CaptureImage(CAMERA_OUT_JPEG, 0);

		/* jpeg data*/
		encode_size += CAMERA_JPEG_HEADER_LEN;
		addr = (mem_mgmt.jpeg_buf - CAMERA_JPEG_HEADER_LEN) ;

		if(encode_size<=CameraQueueItemSize-4){
			cameraBuf[0] = (encode_size>>24)&0xff;
			cameraBuf[1] = (encode_size>>16)&0xff;
			cameraBuf[2] = (encode_size>>8)&0xff;
			cameraBuf[3] = encode_size&0xff;

			memcpy(&cameraBuf[4],addr,encode_size);
			BaseType_t xStatus;
			//发送最多等待10ms
			xStatus = xQueueSend(cameraJpegSendQueue, cameraBuf, pdMS_TO_TICKS(10));
			
			if(xStatus !=pdPASS){
				//发送失败了
				//printf("audioNetSendQueue full\r\n");
			}
			return 1;
		}else{
			printf("Camera Queue Size small for img:%d : %d\r\n",encode_size,CameraQueueItemSize);
		}
		
	}

	return 0;
}

static void camera_deinit()
{
	HAL_CAMERA_DeInit();
	camera_mem_destroy();
}



unsigned char frameCount = 0;
//线程
static void thread_camera_Fun(){
	camera_init();
	while(1){
		if(camera_restart_flag!=0){
			printf("thread_camera_Fun restart camera now width:%d  height:%d quality:%d\r\n",camera_cfg.sensor_cfg.pixel_size.width, camera_cfg.sensor_cfg.pixel_size.height, camera_cfg.jpeg_cfg.quality );
			camera_deinit();
			OS_MSleep(100);
			camera_init();
			OS_MSleep(100);
			camera_restart_flag = 0;
		}
		camera_get_image();
		OS_MSleep(5);
		frameCount++;
	}
	
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

//初始化摄像头
void initCameraSensor(void)
{
	if(cameraJpegSendQueue==NULL){
		printf("Create cameraJpegSendQueue\r\n");
		cameraJpegSendQueue = xQueueCreate(CameraQueueLength, CameraQueueItemSize);
	}else{
		printf("cameraJpegSendQueue have been create\r\n");
	}
	
	if(cameraJpegSendQueue==NULL){
		printf("cameraJpegSendQueue malloc failed can't start Camera Thread");
		return ;
	}
	
	if(cameraBuf==NULL){
		printf("Create cameraBuf\r\n");
		cameraBuf = (uint8_t*)psram_malloc(CameraQueueItemSize);
	}else{
		printf("cameraBuf have been create\r\n");
	}
	
	if(cameraBuf==NULL){
		printf("cameraBuf malloc failed can't start Camera Thread");
		return ;
	}
	
	if (OS_ThreadCreate(&thread_camera,"thread_camera",thread_camera_Fun,NULL,OS_PRIORITY_NORMAL,THREAD_CAMERA_STACK_SIZE) != OS_OK) {			
		printf("thread camera create error\n");
	}
	
	printf("init camera thread ok\r\n");
}

//获取一张jpeg图像
int getCameraSensorImg(unsigned char *buf,unsigned int maxLen)
{
	unsigned int imgLen = 0;
	BaseType_t xStatus;
	xStatus = xQueueReceive(cameraJpegSendQueue, buf, pdMS_TO_TICKS(300));
	if(xStatus == pdPASS){
		imgLen = buf[0];	imgLen = imgLen<<8;
		imgLen |= buf[1];	imgLen = imgLen<<8;
		imgLen |= buf[2];	imgLen = imgLen<<8;
		imgLen |= buf[3];
		return imgLen;
	}
	printf("get img error,camera thread not run\r\n");
	return 0;
}
//退出摄像头初始化
void deinitCameraSensor(void)
{
	
}

void restartCameraByParam(unsigned int width,unsigned int height,unsigned int quality)
{
	camera_cfg.jpeg_cfg.quality = quality;
	camera_cfg.sensor_cfg.pixel_size.width = width;
	camera_cfg.sensor_cfg.pixel_size.height = height ;
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




