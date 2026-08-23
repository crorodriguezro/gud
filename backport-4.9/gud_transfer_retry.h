#ifndef __GUD_TRANSFER_RETRY_H__
#define __GUD_TRANSFER_RETRY_H__

#define GUD_SET_BUFFER_BUSY_RETRIES 100U
#define GUD_SET_BUFFER_BUSY_RETRY_DELAY_MIN_US 500U
#define GUD_SET_BUFFER_BUSY_RETRY_DELAY_MAX_US 1000U

static inline bool gud_set_buffer_should_retry(int result,
					      unsigned int retries)
{
	return result == -EBUSY && retries < GUD_SET_BUFFER_BUSY_RETRIES;
}

#endif
