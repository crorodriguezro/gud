#include <errno.h>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

static const char *fourcc(__u32 value, char out[5])
{
	out[0] = value & 0xff;
	out[1] = (value >> 8) & 0xff;
	out[2] = (value >> 16) & 0xff;
	out[3] = (value >> 24) & 0xff;
	out[4] = '\0';
	return out;
}

static void enum_formats(int fd, enum v4l2_buf_type type, const char *name)
{
	struct v4l2_fmtdesc fmt;
	char code[5];

	memset(&fmt, 0, sizeof(fmt));
	fmt.type = type;
	printf("%s formats:\n", name);
	for (fmt.index = 0; ioctl(fd, VIDIOC_ENUM_FMT, &fmt) == 0; fmt.index++)
		printf("  %u %s %s flags=0x%x\n", fmt.index,
		       fourcc(fmt.pixelformat, code), fmt.description, fmt.flags);
	if (errno != EINVAL)
		fprintf(stderr, "VIDIOC_ENUM_FMT(%s): %s\n", name, strerror(errno));
}

static void enum_controls(int fd)
{
	struct v4l2_queryctrl ctrl;

	memset(&ctrl, 0, sizeof(ctrl));
	ctrl.id = V4L2_CTRL_FLAG_NEXT_CTRL;
	puts("controls:");
	while (ioctl(fd, VIDIOC_QUERYCTRL, &ctrl) == 0) {
		printf("  0x%08x %s min=%d max=%d step=%d default=%d flags=0x%x\n",
		       ctrl.id, ctrl.name, ctrl.minimum, ctrl.maximum, ctrl.step,
		       ctrl.default_value, ctrl.flags);
		ctrl.id |= V4L2_CTRL_FLAG_NEXT_CTRL;
	}
}

int main(int argc, char **argv)
{
	const char *path = argc > 1 ? argv[1] : "/dev/video33";
	struct v4l2_capability cap;
	int fd = open(path, O_RDWR | O_NONBLOCK);

	if (fd < 0) {
		fprintf(stderr, "open %s: %s\n", path, strerror(errno));
		return 1;
	}
	memset(&cap, 0, sizeof(cap));
	if (ioctl(fd, VIDIOC_QUERYCAP, &cap) < 0) {
		fprintf(stderr, "VIDIOC_QUERYCAP: %s\n", strerror(errno));
		close(fd);
		return 1;
	}
	printf("device=%s driver=%s card=%s bus=%s version=%u.%u.%u "
	       "caps=0x%08x device_caps=0x%08x\n", path, cap.driver, cap.card,
	       cap.bus_info, cap.version >> 16, (cap.version >> 8) & 0xff,
	       cap.version & 0xff, cap.capabilities, cap.device_caps);
	enum_formats(fd, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE, "OUTPUT_MPLANE");
	enum_formats(fd, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE, "CAPTURE_MPLANE");
	enum_controls(fd);
	close(fd);
	return 0;
}
