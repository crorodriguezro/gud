#include <errno.h>
#include <stdbool.h>
#include <stdio.h>

#include "../gud_transfer_retry.h"

static int failures;

static void expect(const char *name, bool actual, bool expected)
{
	if (actual == expected)
		return;

	fprintf(stderr, "FAIL %s: actual=%d expected=%d\n",
		name, actual, expected);
	failures++;
}

int main(void)
{
	expect("first explicit busy", gud_set_buffer_should_retry(-EBUSY, 0), true);
	expect("last bounded retry",
	       gud_set_buffer_should_retry(
		       -EBUSY, GUD_SET_BUFFER_BUSY_RETRIES - 1),
	       true);
	expect("retry ceiling",
	       gud_set_buffer_should_retry(-EBUSY,
					   GUD_SET_BUFFER_BUSY_RETRIES),
	       false);
	expect("success", gud_set_buffer_should_retry(0, 0), false);
	expect("ambiguous transport error",
	       gud_set_buffer_should_retry(-ETIMEDOUT, 0), false);
	expect("other protocol error",
	       gud_set_buffer_should_retry(-EINVAL, 0), false);
	expect("retry delay minimum is positive",
	       GUD_SET_BUFFER_BUSY_RETRY_DELAY_MIN_US > 0, true);
	expect("retry delay range is ordered",
	       GUD_SET_BUFFER_BUSY_RETRY_DELAY_MAX_US >=
		       GUD_SET_BUFFER_BUSY_RETRY_DELAY_MIN_US,
	       true);

	if (failures)
		return 1;

	puts("gud transfer retry tests: PASS");
	return 0;
}
