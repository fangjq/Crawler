#include <errno.h>
#include <linux/time.h>

int poll_internal(int fd, int wf, double timeout)
{
	if (timeout)
	{
		int test;
		test = select_fd(fd, timeout, wf);
		if (test == 0)
			errno = ETIMEDOUT;
		if (test <= 0)
			return 0;
	}
	return 1;
}

/* Returns 1 if FD is available, 0 for timeout and -1 for error. */

int select_fd(int fd, double maxtime, int wait_for)
{
	fd_set fdset;
	fd_set *rd = NULL, *wr = NULL;
	struct timeval tmout;
	int result;

	FD_ZERO(&fdset);
	FD_SET(fd, &fdset);
	
	if (wait_for & WAIT_FOR_READ)
		rd = &fdset;

	if (wait_for & WAIT_FOR_WRITE)
		wr = &fdset;

	tmout.tv_sec = (long) maxtime;
	tmout.tv_usec = 1000000 * (maxtime - (long) maxtime);
	
	do
	{
		result = select(fd + 1, rd, wr, NULL, &tmout);
	}
	while (result < 0 && errno == EINTR);

	return result;
}
	
