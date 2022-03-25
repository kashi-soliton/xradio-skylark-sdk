#ifndef _camera_sensor_x_
#define _camera_sensor_x_


//初始化摄像头
void initCameraSensor(void);

//
unsigned char getCameraFrameCount(void);

//获取一张jpeg图像
int getCameraSensorImg(unsigned char *buf,unsigned int maxLen);
//退出摄像头初始化
void deinitCameraSensor(void);

//更新当前摄像头参数，尺寸和图像质量等参数
void restartCameraByParam(unsigned int width,unsigned int height,unsigned int quality);



//设置灯光模式	0自动	1日照模式	2阴天模式	3办公室模式		4居家模式
void cameraSetLightMode(unsigned char mode);

//设置色彩饱和度		0(-2)	1(-1)	2(0)	3(1)	4(2)
void cameraSetColorSaturation(unsigned char cs);

//设置亮度		0(-2)	1(-1)	2(0)	3(1)	4(2)
void cameraSetBrightness(unsigned char brightness);

//设置对比度		0(-2)	1(-1)	2(0)	3(1)	4(2)
void cameraSetContrast(unsigned char contrast);

//设置特殊效果	0正常模式		1负片模式		2黑白模式		3纯红		4纯绿		5纯蓝		6旧片模式
void cameraSpecialEffects(unsigned char se);

#endif


