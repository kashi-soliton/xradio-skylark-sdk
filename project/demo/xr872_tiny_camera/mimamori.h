#ifndef _MIMAMORI_H_
#define _MIMAMORI_H_

#ifdef __cplusplus
extern "C" {
#endif

typedef struct private_s {
        OS_Mutex_t mu;
	int32_t do_capture;
        uint32_t count;
        uint32_t max_size;
	uint32_t max_cost;
	uint32_t quality;
} private_t;

#ifdef __cplusplus
}
#endif

#endif /* _COMMAND_H_ */
