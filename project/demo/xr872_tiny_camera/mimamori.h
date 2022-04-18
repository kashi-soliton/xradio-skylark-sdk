#ifndef _MIMAMORI_H_
#define _MIMAMORI_H_

#ifdef __cplusplus
extern "C" {
#endif

typedef struct private_s {
        OS_Mutex_t mu;
	int32_t do_capture;
        uint32_t count;
} private_t;

extern uint8_t *jpeg_buf;

#ifdef __cplusplus
}
#endif

#endif /* _COMMAND_H_ */
