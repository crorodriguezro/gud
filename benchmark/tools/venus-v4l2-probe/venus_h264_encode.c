#include <errno.h>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#define NBUF 4
#define NPLANE VIDEO_MAX_PLANES

struct mapped_buffer {
	void *ptr[NPLANE];
	size_t len[NPLANE];
	int fd[NPLANE];
	unsigned int planes;
};

typedef int ion_user_handle_t;
struct ion_allocation_data {
	size_t len, align;
	unsigned int heap_id_mask, flags;
	ion_user_handle_t handle;
};
struct ion_fd_data { ion_user_handle_t handle; int fd; };
#define ION_IOC_MAGIC 'I'
#define ION_IOC_ALLOC _IOWR(ION_IOC_MAGIC, 0, struct ion_allocation_data)
#define ION_IOC_SHARE _IOWR(ION_IOC_MAGIC, 4, struct ion_fd_data)
#define ION_SYSTEM_HEAP_MASK (1U << 25)

static int xioctl(int fd, unsigned long request, void *arg)
{
	int ret;
	do ret = ioctl(fd, request, arg); while (ret < 0 && errno == EINTR);
	return ret;
}

static int set_ctrl(int fd, uint32_t id, int value)
{
	struct v4l2_control c = {.id = id, .value = value};
	if (xioctl(fd, VIDIOC_S_CTRL, &c) == 0) return 0;
	fprintf(stderr, "control 0x%x=%d: %s (continuing)\n", id, value,
		strerror(errno));
	return -1;
}

static int alloc_ion(int ionfd, size_t len, struct mapped_buffer *buffer,
		     unsigned int plane)
{
	struct ion_allocation_data alloc = {.len = (len + 4095) & ~4095UL,
		.align = 4096, .heap_id_mask = ION_SYSTEM_HEAP_MASK};
	struct ion_fd_data share;
	if (xioctl(ionfd, ION_IOC_ALLOC, &alloc) < 0) return -1;
	memset(&share, 0, sizeof(share)); share.handle = alloc.handle;
	if (xioctl(ionfd, ION_IOC_SHARE, &share) < 0) return -1;
	buffer->fd[plane] = share.fd; buffer->len[plane] = alloc.len;
	buffer->ptr[plane] = mmap(NULL, alloc.len, PROT_READ | PROT_WRITE,
		MAP_SHARED, share.fd, 0);
	return buffer->ptr[plane] == MAP_FAILED ? -1 : 0;
}

static int map_queue(int fd, int ionfd, enum v4l2_buf_type type,
		     unsigned int num_planes, const size_t sizes[NPLANE],
		     struct mapped_buffer buffers[NBUF])
{
	struct v4l2_requestbuffers req = {.count = NBUF, .type = type,
		.memory = V4L2_MEMORY_USERPTR};
	unsigned int i, p;
	if (xioctl(fd, VIDIOC_REQBUFS, &req) < 0 || req.count < 2) return -1;
	for (i = 0; i < req.count; i++) {
		buffers[i].planes = num_planes;
		for (p = 0; p < num_planes; p++)
			if (alloc_ion(ionfd, sizes[p], &buffers[i], p) < 0) return -1;
	}
	return req.count;
}

static void fill_nv12(struct mapped_buffer *b, unsigned int width,
		      unsigned int height, unsigned int frame)
{
	unsigned int x, y;
	uint8_t *luma = b->ptr[0];
	uint8_t *chroma = b->planes > 1 ? b->ptr[1] : luma + width * height;
	for (y = 0; y < height; y++)
		for (x = 0; x < width; x++)
			luma[y * width + x] = 16 + ((x + frame * 16) * 219 / width);
	for (y = 0; y < height / 2; y++)
		for (x = 0; x < width; x += 2) {
			chroma[y * width + x] = 64 + ((frame * 3 + y) & 127);
			chroma[y * width + x + 1] = 192 - ((frame * 5 + x) & 127);
		}
}

static int read_full(int fd, void *data, size_t length)
{
	uint8_t *p = data;
	while (length) {
		ssize_t n = read(fd, p, length);
		if (n == 0) return 0;
		if (n < 0) { if (errno == EINTR) continue; return -1; }
		p += n; length -= n;
	}
	return 1;
}

static uint8_t clamp8(int value)
{
	return value < 0 ? 0 : value > 255 ? 255 : value;
}

static void rgba_to_nv12(struct mapped_buffer *b, const uint8_t *rgba,
			 unsigned int width, unsigned int height)
{
	uint8_t *yplane = b->ptr[0];
	uint8_t *uv = b->planes > 1 ? b->ptr[1] : yplane + width * height;
	unsigned int x, y;
	for (y = 0; y < height; y++) for (x = 0; x < width; x++) {
		const uint8_t *p = rgba + (y * width + x) * 4;
		yplane[y * width + x] = clamp8(((66*p[0] + 129*p[1] + 25*p[2] + 128) >> 8) + 16);
	}
	for (y = 0; y < height; y += 2) for (x = 0; x < width; x += 2) {
		const uint8_t *p = rgba + (y * width + x) * 4;
		uv[(y/2) * width + x] = clamp8(((-38*p[0] - 74*p[1] + 112*p[2] + 128) >> 8) + 128);
		uv[(y/2) * width + x + 1] = clamp8(((112*p[0] - 94*p[1] - 18*p[2] + 128) >> 8) + 128);
	}
}

int main(int argc, char **argv)
{
	const char *device = argc > 1 ? argv[1] : "/dev/video33";
	const char *output = argc > 2 ? argv[2] : "/tmp/venus-test.h264";
	int rgba_direct = argc > 3 && !strcmp(argv[3], "--rgba-direct");
	int rgba_stdin = rgba_direct || (argc > 3 && !strcmp(argv[3], "--rgba-stdin"));
	unsigned int width = 1920, height = 1080, frames = argc > 4 ? atoi(argv[4]) : 90, i;
	uint8_t *rgba = rgba_stdin && !rgba_direct ? malloc((size_t)width * height * 4) : NULL;
	struct mapped_buffer out[NBUF] = {0}, cap[NBUF] = {0};
	struct v4l2_format fmt = {0};
	struct v4l2_streamparm parm = {0};
	enum v4l2_buf_type out_type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	enum v4l2_buf_type cap_type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	int fd = open(device, O_RDWR | O_NONBLOCK), ionfd = open("/dev/ion", O_RDWR);
	int ofd, out_count, cap_count;
	unsigned int out_planes, cap_planes;
	size_t out_sizes[NPLANE] = {0}, cap_sizes[NPLANE] = {0};
	uint64_t bytes = 0;
	struct timespec start, end;
	if (fd < 0) { perror("open encoder"); return 1; }
	ofd = open(output, O_CREAT | O_TRUNC | O_WRONLY, 0644);
	if (ofd < 0) { perror("open output"); return 1; }

	fmt.type = cap_type;
	fmt.fmt.pix_mp.width = width; fmt.fmt.pix_mp.height = height;
	fmt.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_H264;
	fmt.fmt.pix_mp.field = V4L2_FIELD_NONE;
	fmt.fmt.pix_mp.num_planes = 1;
	fmt.fmt.pix_mp.plane_fmt[0].sizeimage = 2 * 1024 * 1024;
	if (xioctl(fd, VIDIOC_S_FMT, &fmt) < 0) { perror("S_FMT capture"); return 1; }
	cap_planes = fmt.fmt.pix_mp.num_planes;
	for (i = 0; i < cap_planes; i++) cap_sizes[i] = fmt.fmt.pix_mp.plane_fmt[i].sizeimage;
	printf("capture %ux%u planes=%u size=%u\n", fmt.fmt.pix_mp.width,
		fmt.fmt.pix_mp.height, cap_planes, fmt.fmt.pix_mp.plane_fmt[0].sizeimage);

	memset(&fmt, 0, sizeof(fmt)); fmt.type = out_type;
	fmt.fmt.pix_mp.width = width; fmt.fmt.pix_mp.height = height;
	fmt.fmt.pix_mp.pixelformat = rgba_direct ? V4L2_PIX_FMT_RGB32 : V4L2_PIX_FMT_NV12;
	fmt.fmt.pix_mp.field = V4L2_FIELD_NONE;
	if (xioctl(fd, VIDIOC_S_FMT, &fmt) < 0) { perror("S_FMT output"); return 1; }
	out_planes = fmt.fmt.pix_mp.num_planes;
	for (i = 0; i < out_planes; i++) out_sizes[i] = fmt.fmt.pix_mp.plane_fmt[i].sizeimage;
	printf("output %ux%u planes=%u sizes=%u,%u\n", fmt.fmt.pix_mp.width,
		fmt.fmt.pix_mp.height, out_planes,
		fmt.fmt.pix_mp.plane_fmt[0].sizeimage,
		fmt.fmt.pix_mp.plane_fmt[1].sizeimage);

	parm.type = out_type;
	parm.parm.output.timeperframe.numerator = 1;
	parm.parm.output.timeperframe.denominator = 30;
	if (xioctl(fd, VIDIOC_S_PARM, &parm) < 0) perror("S_PARM");
	set_ctrl(fd, V4L2_CID_MPEG_VIDEO_BITRATE, 15000000);
	set_ctrl(fd, V4L2_CID_MPEG_VIDEO_GOP_SIZE, 30);
	set_ctrl(fd, V4L2_CID_MPEG_VIDEO_B_FRAMES, 0);

	out_count = map_queue(fd, ionfd, out_type, out_planes, out_sizes, out);
	if (out_count < 0) perror("REQBUFS/map output");
	cap_count = map_queue(fd, ionfd, cap_type, cap_planes, cap_sizes, cap);
	if (cap_count < 0) perror("REQBUFS/map capture");
	if (out_count < 0 || cap_count < 0) return 1;
	for (i = 0; i < (unsigned int)cap_count; i++) {
		struct v4l2_plane p[NPLANE] = {{0}};
		struct v4l2_buffer b = {.type = cap_type, .memory = V4L2_MEMORY_USERPTR,
			.index = i, .length = cap[i].planes, .m.planes = p};
		for (unsigned int j = 0; j < cap[i].planes; j++) {
			p[j].m.userptr = (unsigned long)cap[i].ptr[j];
			p[j].reserved[0] = cap[i].fd[j]; p[j].length = cap[i].len[j];
		}
		if (xioctl(fd, VIDIOC_QBUF, &b) < 0) { perror("QBUF capture"); return 1; }
	}
	if (xioctl(fd, VIDIOC_STREAMON, &cap_type) < 0 ||
	    xioctl(fd, VIDIOC_STREAMON, &out_type) < 0) { perror("STREAMON"); return 1; }
	clock_gettime(CLOCK_MONOTONIC, &start);
	for (i = 0; i < frames; i++) {
		unsigned int index = i % out_count, pidx;
		struct v4l2_plane p[NPLANE] = {{0}};
		struct v4l2_buffer b = {.type = out_type, .memory = V4L2_MEMORY_USERPTR,
			.index = index, .length = out[index].planes, .m.planes = p};
		if (rgba_direct) {
			int rr = read_full(STDIN_FILENO, out[index].ptr[0], (size_t)width * height * 4);
			if (rr <= 0) { fprintf(stderr, "RGBA input ended at frame %u\n", i); break; }
		} else if (rgba_stdin) {
			int rr = read_full(STDIN_FILENO, rgba, (size_t)width * height * 4);
			if (rr <= 0) { fprintf(stderr, "RGBA input ended at frame %u\n", i); break; }
			rgba_to_nv12(&out[index], rgba, width, height);
		} else fill_nv12(&out[index], width, height, i);
		for (pidx = 0; pidx < out[index].planes; pidx++) {
			p[pidx].m.userptr = (unsigned long)out[index].ptr[pidx];
			p[pidx].reserved[0] = out[index].fd[pidx]; p[pidx].length = out[index].len[pidx];
			p[pidx].bytesused = rgba_direct ? width * height * 4 :
				(pidx == 0 ? width * height : width * height / 2);
		}
		if (out[index].planes == 1 && !rgba_direct) p[0].bytesused = width * height * 3 / 2;
		b.timestamp.tv_sec = i / 30; b.timestamp.tv_usec = (i % 30) * 33333;
		if (xioctl(fd, VIDIOC_QBUF, &b) < 0) { perror("QBUF output"); return 1; }
		for (;;) {
			struct pollfd pollfd = {.fd = fd, .events = POLLIN | POLLOUT};
			struct v4l2_plane cp[NPLANE] = {{0}};
			struct v4l2_buffer cb = {.type = cap_type, .memory = V4L2_MEMORY_USERPTR,
				.length = cap_planes, .m.planes = cp};
			if (poll(&pollfd, 1, 2000) <= 0) { perror("poll"); return 1; }
			if (xioctl(fd, VIDIOC_DQBUF, &cb) == 0) {
				if (cp[0].bytesused && write(ofd, cap[cb.index].ptr[0], cp[0].bytesused) != (ssize_t)cp[0].bytesused) { perror("write"); return 1; }
				bytes += cp[0].bytesused;
				for (pidx = 0; pidx < cap[cb.index].planes; pidx++) {
					cp[pidx].m.userptr = (unsigned long)cap[cb.index].ptr[pidx];
					cp[pidx].reserved[0] = cap[cb.index].fd[pidx];
					cp[pidx].length = cap[cb.index].len[pidx];
				}
				if (xioctl(fd, VIDIOC_QBUF, &cb) < 0) { perror("re-QBUF capture"); return 1; }
			}
			memset(p, 0, sizeof(p)); b.length = out[index].planes; b.m.planes = p;
			b.memory = V4L2_MEMORY_USERPTR;
			if (xioctl(fd, VIDIOC_DQBUF, &b) == 0) break;
			if (errno != EAGAIN) { perror("DQBUF output"); return 1; }
		}
	}
	clock_gettime(CLOCK_MONOTONIC, &end);
	printf("encoded_frames=%u bytes=%llu seconds=%.3f fps=%.2f mbps=%.2f source=%s\n",
		i, (unsigned long long)bytes,
		(end.tv_sec-start.tv_sec)+(end.tv_nsec-start.tv_nsec)/1e9,
		i/((end.tv_sec-start.tv_sec)+(end.tv_nsec-start.tv_nsec)/1e9),
		bytes*8.0/1e6/((end.tv_sec-start.tv_sec)+(end.tv_nsec-start.tv_nsec)/1e9),
		rgba_direct ? "Mir-RGBA-direct" : rgba_stdin ? "Mir-RGBA-to-NV12" : "synthetic-NV12");
	xioctl(fd, VIDIOC_STREAMOFF, &out_type);
	xioctl(fd, VIDIOC_STREAMOFF, &cap_type);
	close(ofd); close(ionfd); close(fd); free(rgba); return 0;
}
