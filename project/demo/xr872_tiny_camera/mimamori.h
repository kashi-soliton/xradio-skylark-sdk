#ifndef _MIMAMORI_H_
#define _MIMAMORI_H_

#ifdef __cplusplus
extern "C" {
#endif

typedef struct private_s {
        OS_Mutex_t mu;
	//OS_Semaphore_t sem;
	int32_t do_capture;
	uint8_t *jpeg_buf;
	uint32_t jpeg_size;
	uint32_t new_jpeg;
	uint32_t dms[3];
	uint32_t dms_num;
} private_t;

#ifdef __cplusplus
}
#endif

#endif /* _COMMAND_H_ */
