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

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "kernel/os/os.h"
#include "driver/chip/hal_snd_card.h"
#include "audio/pcm/audio_pcm.h"
#include "audio/manager/audio_manager.h"
#include "common/framework/platform_init.h"
#include "mm_i2s.h"



//I2S mimamori config
#define I2S_MM_SND_CARD		SND_CARD_3
#define I2S_MM_LOOP_BACK_EN	1

#define I2S_MM_SAMPLE_RATE	8000	//[8000,16000,32000, 12000,24000,48000, 11025,22050,44100]
#define I2S_MM_CHANNEL_NUMS	1		//[1,2]
#define I2S_MM_RESOLUTION		16		//[8,12,16,20,24,28,32]


uint16_t tx_data_16bit[64] = { // 1kHz sine
	0x0000, 0x0b50, 0x7fff, 0x0b50, 0x0000, 0xf4b0, 0xf001, 0xf4b0,
	0x0000, 0x0b50, 0x7fff, 0x0b50, 0x0000, 0xf4b0, 0xf001, 0xf4b0,
	0x0000, 0x0b50, 0x7fff, 0x0b50, 0x0000, 0xf4b0, 0xf001, 0xf4b0,
	0x0000, 0x0b50, 0x7fff, 0x0b50, 0x0000, 0xf4b0, 0xf001, 0xf4b0,
	0x0000, 0x0b50, 0x7fff, 0x0b50, 0x0000, 0xf4b0, 0xf001, 0xf4b0,
	0x0000, 0x0b50, 0x7fff, 0x0b50, 0x0000, 0xf4b0, 0xf001, 0xf4b0,
	0x0000, 0x0b50, 0x7fff, 0x0b50, 0x0000, 0xf4b0, 0xf001, 0xf4b0,
	0x0000, 0x0b50, 0x7fff, 0x0b50, 0x0000, 0xf4b0, 0xf001, 0xf4b0,
};

#define THREAD_I2S_STACK_SIZE	(1024 * 1)

OS_TimerCallback_t mm_i2s_callback(void *arg)
{
	i2s_data_t* p = (i2s_data_t*) arg;
	OS_SemaphoreRelease(&p->rd_sem);
	return NULL;
}

static i2s_data_t i2s_data;

static int mm_i2s_open(uint8_t direction, uint32_t samplerate, uint8_t channels, uint8_t resolution);
static int mm_i2s_close(uint8_t direction);
static int mm_i2s_write(uint8_t *buffer, uint32_t size);
static void thread_i2s(void *arg);

int i2s_start(void)
{
	i2s_data.i2s_read = 1;
	return 0;
}

OS_Semaphore_t *i2s_get_sem(void)
{
	return &i2s_data.done_sem;
}

int i2s_stop(void)
{
	i2s_data.i2s_read = 0;
	return 0;
}

i2s_data_t *get_i2s_data()
{
	return &i2s_data;
}

int i2s_get_data(uint8_t **buf, uint32_t *size)
{
	uint32_t i;
	uint32_t flags, busy_page, page_num;
	OS_Status ret;
	ret = OS_MutexLock(&i2s_data.mu, 500);
	if (ret != OS_OK) {
		printf("%s: Failed to OS_MutexLocl\n", __func__);
		return 1;
	}
	flags = i2s_data.page_flags;
	busy_page = i2s_data.busy_page;
	page_num = i2s_data.page_num;
	*size = i2s_data.size;
	OS_MutexUnlock(&i2s_data.mu);
	if (flags == 0) return 1;
	for (i = 0; i < page_num; i++) {
		if ((1 << ((busy_page + i) % page_num)) & flags) {
			(*buf) = (i2s_data.buf + (*size) * ((busy_page + i) % page_num));
			ret = OS_MutexLock(&i2s_data.mu, 500);
			if (ret != OS_OK) {
				printf("%s: Failed to OS_MutexLocl\n", __func__);
				return 1;
			}
			i2s_data.page_flags &= ~(1 << ((busy_page + i) % page_num));
			flags = i2s_data.page_flags;
			OS_MutexUnlock(&i2s_data.mu);
			if (flags) printf("%s: Too many data: 0x%08x\n", __func__, flags);
			break;
		}
	}
	return 0;
}

int i2s_clear_all_flags()
{
	OS_Status ret;
	ret = OS_MutexLock(&i2s_data.mu, 500);
	if (ret != OS_OK) {
		printf("%s: Failed to OS_MutexLocl\n", __func__);
		return 1;
	}
	i2s_data.page_flags = 0;
	OS_MutexUnlock(&i2s_data.mu);

	return 0;
}

int mm_init_i2s(void)
{
	OS_Status ret;
	i2s_data_t *p = &i2s_data;
	memset(p, 0, sizeof(i2s_data_t));
	p->period = 8;
	p->size = I2S_MM_CHANNEL_NUMS * (I2S_MM_RESOLUTION / 8) *
	       (I2S_MM_SAMPLE_RATE / 1000 * p->period);
	p->page_num = 4;
	p->buf = malloc(p->size * p->page_num);
	if (p->buf == NULL) {
		printf("error malloc i2s_data->buf\n");
		return 1;
	}
	memset(p->buf, 0, p->size * p->period);

	ret = OS_MutexCreate(&p->mu);
        if (ret != OS_OK) {
                printf("Failed: OS_MutexCreate mu\n");
                return 1;
        }
	ret = OS_SemaphoreCreateBinary(&p->rd_sem);
	if (ret != OS_OK) {
		printf("Failed: OS_SemaphoreCreateBinary\n");
                return 1;
	}
	ret = OS_SemaphoreCreateBinary(&p->done_sem);
	if (ret != OS_OK) {
		printf("Failed: OS_SemaphoreCreateBinary\n");
                return 1;
	}
	OS_TimerSetInvalid(&p->timer_id);
	ret = OS_TimerCreate(&p->timer_id, OS_TIMER_PERIODIC,
			(OS_TimerCallback_t) mm_i2s_callback, p, 16);
	if (ret != OS_OK) {
		printf("Failed: OS_TimerCreate\n");
                return 1;
	}
	ret = OS_ThreadCreate(&p->thread_i2s_handle, "i2s read",
			thread_i2s, p,
			OS_PRIORITY_NORMAL,THREAD_I2S_STACK_SIZE);
	if (ret != OS_OK) {
		printf("thread camera create error\n");
                return 1;
	}

	mm_i2s_open(0, I2S_MM_SAMPLE_RATE, I2S_MM_CHANNEL_NUMS, I2S_MM_RESOLUTION);
	mm_i2s_open(1, I2S_MM_SAMPLE_RATE, I2S_MM_CHANNEL_NUMS, I2S_MM_RESOLUTION);
#ifdef MM_I2S_TEST_DATA
	mm_i2s_write((uint8_t *)&tx_data_16bit, sizeof(tx_data_16bit));
#endif

	return 0;
}

void mm_uninit_i2s(void)
{
	OS_Status ret;
	i2s_data_t *p = &i2s_data;
	ret = OS_TimerStop(&p->timer_id);
	if (ret) {
		printf("Failed: OS_TimerStop\n");
	}
	ret = OS_TimerDelete(&p->timer_id);
	if (ret != OS_OK) {
		printf("Failed: OS_TimerDelete\n");
	}
	ret = OS_SemaphoreDelete(&p->done_sem);
	if (ret != OS_OK) {
		printf("Failed: OS_SemaphoreDelete\n");
	}
	ret = OS_SemaphoreDelete(&p->rd_sem);
	if (ret != OS_OK) {
		printf("Failed: OS_SemaphoreDelete\n");
	}
	ret = OS_ThreadDelete(&p->thread_i2s_handle);
	if (ret != OS_OK) {
		printf("Failed: OS_ThreadDelete\n");
	}
	ret = OS_MutexDelete(&p->mu);
        if (ret != OS_OK) {
                printf("Failed: OS_MutexDelete mu\n");
        }
	free(p->buf);
	p->buf = NULL;

	mm_i2s_close(0);
	mm_i2s_close(1);
}

static int mm_i2s_open(uint8_t direction, uint32_t samplerate, uint8_t channels, uint8_t resolution)
{
	uint32_t cmd_param[3];
	cmd_param[0] = (direction? I2S_MM_LOOP_BACK_EN: 0)<<24 | 0x0<<16 | 0x20<<8 | 0x2;
	cmd_param[1] = (channels+1)/2*32;
	cmd_param[2] = samplerate%1000 ? 22579200 : 24576000;

	if (direction != 0 && direction != 1) {
		printf("Invalid direction %d\n", direction);
		return HAL_INVALID;
	}

	if (channels < 1 || channels > 2) {
		printf("Invalid channels %d\n", channels);
		return HAL_INVALID;
	}

	switch(samplerate){
		case 8000:
		case 16000:
		case 32000:

		case 12000:
		case 24000:
		case 48000:

		case 11025:
		case 22050:
		case 44100:
			break;

		default:
			printf("Invalid sample rate %u\n",samplerate);
			return HAL_INVALID;
	}

	switch(resolution){
		case 8:
			resolution = PCM_FORMAT_S8;
			break;
		case 12:
			resolution = PCM_FORMAT_S12_LE;
			break;
		case 16:
			resolution = PCM_FORMAT_S16_LE;
			break;
		case 20:
			resolution = PCM_FORMAT_S20_LE;
			break;
		case 24:
			resolution = PCM_FORMAT_S24_LE;
			break;
		case 28:
			resolution = PCM_FORMAT_S28_LE;
			break;
		case 32:
			resolution = PCM_FORMAT_S32_LE;
			break;

		default:
			printf("Invalid resolution %d\n",resolution);
			return HAL_INVALID;
	}

	struct pcm_config pcm_cfg;
	pcm_cfg.rate = samplerate;
	pcm_cfg.channels = channels;
	pcm_cfg.format = resolution;
	pcm_cfg.period_count = 1;
	pcm_cfg.period_size = sizeof(tx_data_16bit)/(pcm_format_to_bits(pcm_cfg.format)/8*channels)/pcm_cfg.period_count;

	//cmd_param[0] = I2S_MM_LOOP_BACK_EN<<24 | 0x0<<16 | 0x20<<8 | 0x2;
	//cmd_param[1] = (channels+1)/2*32;
	//cmd_param[2] = samplerate%1000 ? 22579200 : 24576000;
	audio_maneger_ioctl(I2S_MM_SND_CARD, PLATFORM_IOCTL_HW_CONFIG, cmd_param, 3);

	cmd_param[0] = 256<<16 | 256;
	audio_maneger_ioctl(I2S_MM_SND_CARD, PLATFORM_IOCTL_SW_CONFIG, cmd_param, 1);

	if (snd_pcm_open(I2S_MM_SND_CARD, (Audio_Stream_Dir)direction, &pcm_cfg)) {
		printf("snd pcm open Fail..\n");
		return HAL_ERROR;
	}

	return HAL_OK;
}

static int mm_i2s_close(uint8_t direction)
{
	if (direction != 0 && direction != 1) {
		printf("Invalid direction %d\n", direction);
		return HAL_INVALID;
	}

	if (snd_pcm_close(I2S_MM_SND_CARD, (Audio_Stream_Dir)direction)) {
		printf("Snd pcm close Fail..\n");
		return HAL_ERROR;
	}

	return HAL_OK;
}

static int mm_i2s_write(uint8_t *buffer, uint32_t size)
{
	uint8_t *buf;
	uint16_t *pos;
	uint32_t i;

	if(!buffer || !size){
		printf("Invalid write buf|size params error!\n");
		return HAL_INVALID;
	}

	buf = (uint8_t *)malloc(size);
	if (buf == NULL) {
		printf("Malloc I2S write buffer Fail\n");
		return HAL_ERROR;
	}

	memcpy(buf, buffer, size);
	if(snd_pcm_write(I2S_MM_SND_CARD, buf, size) != size){
		free(buf);
		printf("I2S write error!\n");
		return HAL_ERROR;
	}

	printf("\nwrite buf:\n");
	pos = (uint16_t*) buf;
	for(i=0; i<size / 2; i++) {
		printf("0x%04x ", *(pos+i));
		if ((i % 8) == 7) printf("\n");
	}
	printf("\n\n");

	free(buf);
	return HAL_OK;
}

static void i2s_test_example(void *arg)
{
	i2s_data_t *p = (i2s_data_t*) arg;

	printf("\n\n%s: start\n", __func__);

	p->i2s_read = 1;
	OS_Sleep(1);
	p->i2s_read = 0;

	printf("\n\ndone\n");

}

static void thread_i2s(void *arg)
{
	i2s_data_t *p = (i2s_data_t*) arg;
	OS_Status ret;
	uint16_t *pos;

	ret = OS_TimerChangePeriod(&p->timer_id, p->period - p->period / 2);
	if (ret != OS_OK) {
		printf("Failed: OS_TimerChangePeriod\n");
	}
	while( 1 ) {
		ret = OS_SemaphoreWait(&p->rd_sem, 500);
		if (ret == OS_OK) {
			ret = OS_TimerChangePeriod(&p->timer_id, p->period - p->period / 2);
			if (ret != OS_OK) {
				printf("Failed: OS_TimerChangePeriod\n");
			}
			if (p->i2s_read) {
				//uint32_t time, cost;
				//time = OS_TicksToMSecs(OS_GetTicks());
				pos = (uint16_t*) (p->buf + p->size * p->busy_page);
				if(snd_pcm_read(I2S_MM_SND_CARD, pos, p->size) != p->size){
					printf("I2S read error!\n");
					return;
				}
				//cost = OS_TicksToMSecs(OS_GetTicks()) - time;
				//printf("i:%u t:%06d c: %03d\n", p->busy_page, time, cost);
				OS_MutexLock(&i2s_data.mu, 500);
				p->page_flags |= 1 << p->busy_page;
				p->busy_page++;
				p->busy_page %= p->page_num;
				OS_MutexUnlock(&i2s_data.mu);
				OS_SemaphoreRelease(&p->done_sem);
			}
		} else {
			OS_MSleep(100);
		}
	}
	printf("%s: done\n", __func__);
}

