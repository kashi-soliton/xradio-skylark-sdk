/*
 * Copyright (C) 2017 XRADIO TECHNOLOGY CO., LTD. All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *    1. Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *    2. Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the
 *       distribution.
 *    3. Neither the name of XRADIO TECHNOLOGY CO., LTD. nor the names of
 *       its contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *  A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *  OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *  SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *  LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *  DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *  THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef __GC0308_CFG_H__
#define __GC0308_CFG_H__

//#define WIN_MODE_QVGA
//#define SUBSAMPLE_1_2

//��ʼ���Ĵ������м����Ӧ��ֵ
const uint8_t gc0308_init_reg_tbl[][2]=
{
	{0xfe,0x00},
	//MCLK=24MHz 10fps
	{0x0f, 0x00},
	{0x01, 0x6a},
	{0x02, 0x70},
	//{0x0f, 0x60},
	//{0x01, 0x6a},
	//{0x02, 0x3b},

	/* 消除flicker问题 */
	//exp step
	{0xe2, 0x00},
	{0xe3, 0x96},
	//exp level
	{0xe4, 0x02},
	{0xe5, 0x58},
	{0xe6, 0x02},
	{0xe7, 0x58},
	{0xe8, 0x02},
	{0xe9, 0x58},
	{0xea, 0x02},
	{0xeb, 0x58},

//	{0xfe,0x00},
	{0xec,0x20},

#ifdef WIN_MODE_QVGA
	//QVGA
	{0x05,0x00},
	{0x06,0x78},
	{0x07,0x00},
	{0x08,0xa0},
	{0x09,0x00}, //240
	{0x0a,0xf8},
	{0x0b,0x01}, //320
	{0x0c,0x48},
#else
	//VGA
	{0x05,0x00},
	{0x06,0x00},
	{0x07,0x00},
	{0x08,0x00},
	{0x09,0x01},//reg09,reg0a决定win_height的大小，win_height=0x1e8=488(实际�?88-8=480)
	{0x0a,0xe8},
	{0x0b,0x02},//reg0b,reg0c决定win_width的大小，win_width=0x288=648(实际�?48-8=640)
	{0x0c,0x88},
#endif
	//Crop window mode for QVGA
	//{0x46,0x80},
	//{0x47,0x78},
	//{0x48,0xa0},
	//{0x49,0x00},
	//{0x4a,0xf0},
	//{0x4b,0x01},
	//{0x4c,0x40},

	{0x0d,0x02},
	{0x0e,0x02},
	{0x10,0x26},
	{0x11,0x0d},
	{0x12,0x2a},
	{0x13,0x00},
	{0x14,0x11},
	{0x15,0x0a},
	{0x16,0x05},
	{0x17,0x01},
	{0x18,0x44},
	{0x19,0x44},
	{0x1a,0x2a},
	{0x1b,0x00},
	{0x1c,0x49},
	{0x1d,0x9a},
	{0x1e,0x61},
	{0x1f, 0x17},//0x16
	{0x20,0x7f},
	{0x21,0xfa},
	{0x22,0x57},
	{0x24,0xa2},	//YCbYCr
	//{0x25,0x0f},
	{0x26,0x03},	//pclk=1; hsync=1, vsync=1
	{0x28,0x00},	//MCLK分频
	{0x2d,0x0a},
	{0x2f,0x01},
	{0x30,0xf7},
	{0x31,0x50},
	{0x32,0x00},
	{0x33,0x28},
	{0x34,0x2a},
	{0x35,0x28},
	{0x39,0x04},
	{0x3a,0x20},
	{0x3b,0x20},
	{0x3c,0x00},
	{0x3d,0x00},
	{0x3e,0x00},
	{0x3f,0x00},
	{0x50,0x14}, // 0x14
	{0x52,0x41},
	{0x53,0x80},
	{0x54,0x80},
	{0x55,0x80},
	{0x56,0x80},
	{0x8b,0x20},
	{0x8c,0x20},
	{0x8d,0x20},
	{0x8e,0x14},
	{0x8f,0x10},
	{0x90,0x14},
	{0x91,0x3c},
	{0x92,0x50},
	//{0x8b,0x10},
	//{0x8c,0x10},
	//{0x8d,0x10},
	//{0x8e,0x10},
	//{0x8f,0x10},
	//{0x90,0x10},
	//{0x91,0x3c},
	//{0x92,0x50},
	{0x5d,0x12},
	{0x5e,0x1a},
	{0x5f,0x24},
	{0x60,0x07},
	{0x61,0x15},
	{0x62,0x08}, // 0x08
	{0x64,0x03},  // 0x03
	{0x66,0xe8},
	{0x67,0x86},
	{0x68,0x82},
	{0x69,0x18},
	{0x6a,0x0f},
	{0x6b,0x00},
	{0x6c,0x5f},
	{0x6d,0x8f},
	{0x6e,0x55},
	{0x6f,0x38},
	{0x70,0x15},
	{0x71,0x33},
	{0x72,0xdc},
	{0x73,0x00},
	{0x74,0x02},
	{0x75,0x3f},
	{0x76,0x02},
	{0x77,0x38}, // 0x47
	{0x78,0x88},
	{0x79,0x81},
	{0x7a,0x81},
	{0x7b,0x22},
	{0x7c,0xff},
	{0x93,0x48},  //color matrix default
	{0x94,0x02},
	{0x95,0x07},
	{0x96,0xe0},
	{0x97,0x40},
	{0x98,0xf0},
	{0xb1,0x40},
	{0xb2,0x40},
	{0xb3,0x40}, //0x40
	{0xb6,0xe0},
	{0xbd,0x38},
	{0xbe,0x36},
	{0xd0,0xCB},
	{0xd1,0x10},
	{0xd2,0x90},
	{0xd3,0x48},
	{0xd5,0xF2},
	{0xd6,0x16},
	{0xdb,0x92},
	{0xdc,0xA5},
	{0xdf,0x23},
	{0xd9,0x00},
	{0xda,0x00},
	{0xe0,0x09},
	{0xed,0x04},
	{0xee,0xa0},
	{0xef,0x40},
	{0x80,0x03},

	{0x9F,0x10},
	{0xA0,0x20},
	{0xA1,0x38},
	{0xA2,0x4e},
	{0xA3,0x63},
	{0xA4,0x76},
	{0xA5,0x87},
	{0xA6,0xa2},
	{0xA7,0xb8},
	{0xA8,0xca},
	{0xA9,0xd8},
	{0xAA,0xe3},
	{0xAB,0xeb},
	{0xAC,0xf0},
	{0xAD,0xF8},
	{0xAE,0xFd},
	{0xAF,0xFF},

	{0xc0,0x00},
	{0xc1,0x10},
	{0xc2,0x1c},
	{0xc3,0x30},
	{0xc4,0x43},
	{0xc5,0x54},
	{0xc6,0x65},
	{0xc7,0x75},
	{0xc8,0x93},
	{0xc9,0xB0},
	{0xca,0xCB},
	{0xcb,0xE6},
	{0xcc,0xFF},
	{0xf0,0x02},
	{0xf1,0x01},
	{0xf2,0x02},
	{0xf3,0x30},

	//Measure window for VGA
//	{0xf7,0x12},
//	{0xf8,0x0a},
//	{0xf9,0x9f},
//	{0xfa,0x78},
	{0xf7,0x04},
	{0xf8,0x02},
	{0xf9,0x98},
	{0xfa,0x78},

	//Measure window for QVGA
//	{0xf7,0x28},
//	{0xf8,0x1E},
//	{0xf9,0x50},
//	{0xfa,0x3C},

	{0xfe,0x01},
	{0x00,0xf5},
	{0x02,0x20},
	{0x04,0x10},
	{0x05,0x08},
	{0x06,0x20},
	{0x08,0x0a},
	{0x0a,0xa0},
	{0x0b,0x60},
	{0x0c,0x08},
	{0x0e,0x44},
	{0x0f,0x32},
	{0x10,0x41},
	{0x11,0x37},
	{0x12,0x22},
	{0x13,0x19},
	{0x14,0x44},
	{0x15,0x44},
	{0x16,0xc2},
	{0x17,0xA8},
	{0x18,0x18},
	{0x19,0x50},
	{0x1a,0xd8},
	{0x1b,0xf5},
	{0x70,0x40},
	{0x71,0x58},
	{0x72,0x30},
	{0x73,0x48},
	{0x74,0x20},
	{0x75,0x60},
	{0x77,0x20},
	{0x78,0x32},
	{0x30,0x03},
	{0x31,0x40},
	{0x32,0x10},
	{0x33,0xe0},
	{0x34,0xe0},
	{0x35,0x00},
	{0x36,0x80},
	{0x37,0x00},
	{0x38,0x04},
	{0x39,0x09},
	{0x3a,0x12},
	{0x3b,0x1C},
	{0x3c,0x28},
	{0x3d,0x31},
	{0x3e,0x44},
	{0x3f,0x57},
	{0x40,0x6C},
	{0x41,0x81},
	{0x42,0x94},
	{0x43,0xA7},
	{0x44,0xB8},
	{0x45,0xD6},
	{0x46,0xEE},
	{0x47,0x0d},
#ifdef SUBSAMPLE_1_2
	{0x53,0x82},
	{0x54,0x22},
	{0x55,0x03},
	{0x56,0x00},
	{0x57,0x00},
	{0x58,0x00},
	{0x59,0x00},
#endif
	{0x62,0xf7},
	{0x63,0x68},
	{0x64,0xd3},
	{0x65,0xd3},
	{0x66,0x60},
	{0xfe,0x00},

	{0x25,0x0f},

	//ri guang deng
	{0x22,0x55},
	{0x5a,0x40},
	{0x5b,0x42},
	{0x5c,0x50},

	//color effect none
	{0x23,0x00},
	{0x2d,0x0a},
	{0x20,0xff},
	{0xd2,0x90},
	{0x73,0x00},
	{0x77,0x54},
	{0xb3,0x40},
	{0xb4,0x80},
	{0xba,0x00},
	{0xbb,0x00},

};

#endif

