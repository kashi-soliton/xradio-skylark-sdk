/*
 */

#ifndef _MM_I2S_H_
#define _MM_I2S_H_

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
	OS_Semaphore_t rd_sem;
	OS_Semaphore_t done_sem;
	OS_Timer_t timer_id;
	OS_Mutex_t mu;
	OS_Thread_t thread_i2s_handle;
	int i2s_read;
	uint8_t *buf;
	uint32_t size;
	uint32_t page_num;
	uint32_t page_flags;
	uint32_t busy_page;
	uint32_t period;
} i2s_data_t;

int mm_init_i2s(void);
void mm_uninit_i2s(void);
i2s_data_t *get_i2s_data(void);
int i2s_start(void);
int i2s_stop(void);
OS_Semaphore_t *i2s_get_sem(void);
int i2s_get_data(uint8_t **buf, uint32_t *size);
int i2s_clear_all_flags();

#ifdef __cplusplus
}
#endif

#endif /* _MM_I2S_H_ */
