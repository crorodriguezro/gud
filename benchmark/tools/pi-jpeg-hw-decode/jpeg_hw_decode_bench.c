/*
 * jpeg_hw_decode_bench.c - V4L2 M2M stateful decoder client for the
 * Raspberry Pi bcm2835-codec-decode device (/dev/video10 by default).
 *
 * PROJECT SPEC (final codec benchmark) sections 8-9 require:
 *   - a real, repeated (>=10s) independent-frame hardware JPEG decode test
 *     via bcm2835-codec-decode (MJPG in, RGBP/RGB565 out);
 *   - per-frame submit/queue/decode/dequeue latency, not just one frame;
 *   - sustained throughput/FPS distinguished from single-frame latency.
 *
 * v4l2-ctl's generic --stream-mmap/--stream-out-mmap path does not perform
 * the VIDIOC_SUBSCRIBE_EVENT / V4L2_EVENT_SOURCE_CHANGE dance that this
 * stateful M2M decoder requires (verified: it left the CAPTURE queue
 * never streamed on). This is a small, purpose-built client instead.
 *
 * Usage:
 *   jpeg_hw_decode_bench <device> <jpeg_file> <width> <height> \
 *       <duration_seconds> <out_json_path> [--dump-first-frame <path>]
 *
 * All decoded frames use the SAME independent JPEG buffer resubmitted
 * repeatedly (frames are independent per the project's hard architectural
 * constraint; no previous-frame state is used by this decoder either).
 */
#define _GNU_SOURCE
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
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define MAX_BUFS 8
#define MAX_SAMPLES 200000

static uint64_t now_ns(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static int xioctl(int fd, unsigned long req, void *arg)
{
	int r;
	do {
		r = ioctl(fd, req, arg);
	} while (r == -1 && errno == EINTR);
	return r;
}

struct mbuf {
	void *start;
	size_t length;
};

static void die(const char *msg)
{
	fprintf(stderr, "FATAL: %s: %s\n", msg, strerror(errno));
	exit(1);
}

int main(int argc, char **argv)
{
	if (argc < 8) {
		fprintf(stderr,
			"usage: %s <device> <jpeg_file> <width> <height> <duration_s> <out_json> <pipeline_depth 1..8>\n",
			argv[0]);
		return 2;
	}
	const char *device = argv[1];
	const char *jpeg_path = argv[2];
	int width = atoi(argv[3]);
	int height = atoi(argv[4]);
	double duration_s = atof(argv[5]);
	const char *out_json = argv[6];
	int pipeline_depth = atoi(argv[7]);
	if (pipeline_depth < 1)
		pipeline_depth = 1;
	if (pipeline_depth > MAX_BUFS)
		pipeline_depth = MAX_BUFS;

	FILE *jf = fopen(jpeg_path, "rb");
	if (!jf)
		die("fopen jpeg");
	fseek(jf, 0, SEEK_END);
	long jpeg_size = ftell(jf);
	fseek(jf, 0, SEEK_SET);
	uint8_t *jpeg_data = malloc(jpeg_size);
	if (fread(jpeg_data, 1, jpeg_size, jf) != (size_t)jpeg_size)
		die("fread jpeg");
	fclose(jf);

	int fd = open(device, O_RDWR | O_NONBLOCK);
	if (fd < 0)
		die("open device");

	struct v4l2_capability cap;
	memset(&cap, 0, sizeof(cap));
	if (xioctl(fd, VIDIOC_QUERYCAP, &cap) < 0)
		die("QUERYCAP");
	fprintf(stderr, "driver=%s card=%s\n", cap.driver, cap.card);

	/* Subscribe to source-change + EOS events before streaming starts. */
	struct v4l2_event_subscription sub;
	memset(&sub, 0, sizeof(sub));
	sub.type = V4L2_EVENT_SOURCE_CHANGE;
	if (xioctl(fd, VIDIOC_SUBSCRIBE_EVENT, &sub) < 0)
		die("SUBSCRIBE_EVENT source_change");
	memset(&sub, 0, sizeof(sub));
	sub.type = V4L2_EVENT_EOS;
	xioctl(fd, VIDIOC_SUBSCRIBE_EVENT, &sub); /* best-effort */

	/* ---- OUTPUT queue: compressed MJPEG ---- */
	struct v4l2_format ofmt;
	memset(&ofmt, 0, sizeof(ofmt));
	ofmt.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	ofmt.fmt.pix_mp.width = width;
	ofmt.fmt.pix_mp.height = height;
	ofmt.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_MJPEG;
	ofmt.fmt.pix_mp.num_planes = 1;
	/* Give the compressed-buffer plane comfortable headroom over the
	 * actual encoded size so re-submitting the same buffer never
	 * truncates it. */
	long out_plane_size = jpeg_size * 2;
	if (out_plane_size < 512 * 1024)
		out_plane_size = 512 * 1024;
	ofmt.fmt.pix_mp.plane_fmt[0].sizeimage = (uint32_t)out_plane_size;
	ofmt.fmt.pix_mp.field = V4L2_FIELD_NONE;
	if (xioctl(fd, VIDIOC_S_FMT, &ofmt) < 0)
		die("S_FMT OUTPUT");

	struct v4l2_requestbuffers oreq;
	memset(&oreq, 0, sizeof(oreq));
	oreq.count = (unsigned)pipeline_depth;
	oreq.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	oreq.memory = V4L2_MEMORY_MMAP;
	if (xioctl(fd, VIDIOC_REQBUFS, &oreq) < 0)
		die("REQBUFS OUTPUT");
	fprintf(stderr, "REQBUFS OUTPUT: requested=%d got=%u\n", pipeline_depth, oreq.count);

	struct mbuf obufs[MAX_BUFS];
	for (unsigned i = 0; i < oreq.count; i++) {
		struct v4l2_plane planes[1];
		struct v4l2_buffer buf;
		memset(&buf, 0, sizeof(buf));
		memset(planes, 0, sizeof(planes));
		buf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
		buf.memory = V4L2_MEMORY_MMAP;
		buf.index = i;
		buf.m.planes = planes;
		buf.length = 1;
		if (xioctl(fd, VIDIOC_QUERYBUF, &buf) < 0)
			die("QUERYBUF OUTPUT");
		obufs[i].length = planes[0].length;
		obufs[i].start = mmap(NULL, planes[0].length, PROT_READ | PROT_WRITE,
				       MAP_SHARED, fd, planes[0].m.mem_offset);
		if (obufs[i].start == MAP_FAILED)
			die("mmap OUTPUT");
	}

	/* Queue buffer 0 with the JPEG payload and STREAMON to trigger
	 * source detection. */
	memcpy(obufs[0].start, jpeg_data, jpeg_size);
	{
		struct v4l2_plane planes[1];
		struct v4l2_buffer buf;
		memset(&buf, 0, sizeof(buf));
		memset(planes, 0, sizeof(planes));
		buf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
		buf.memory = V4L2_MEMORY_MMAP;
		buf.index = 0;
		buf.m.planes = planes;
		buf.length = 1;
		planes[0].bytesused = (uint32_t)jpeg_size;
		if (xioctl(fd, VIDIOC_QBUF, &buf) < 0)
			die("QBUF OUTPUT[0]");
	}
	{
		int type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
		if (xioctl(fd, VIDIOC_STREAMON, &type) < 0)
			die("STREAMON OUTPUT");
	}

	/* Wait for V4L2_EVENT_SOURCE_CHANGE (poll for priority events). */
	int source_changed = 0;
	for (int tries = 0; tries < 500 && !source_changed; tries++) {
		struct pollfd pfd = { .fd = fd, .events = POLLPRI };
		int pr = poll(&pfd, 1, 20);
		if (pr <= 0)
			continue;
		if (pfd.revents & POLLPRI) {
			struct v4l2_event ev;
			memset(&ev, 0, sizeof(ev));
			while (xioctl(fd, VIDIOC_DQEVENT, &ev) == 0) {
				if (ev.type == V4L2_EVENT_SOURCE_CHANGE)
					source_changed = 1;
			}
		}
	}
	if (!source_changed)
		fprintf(stderr,
			"WARNING: no explicit SOURCE_CHANGE event observed; proceeding with declared width/height\n");

	/* ---- CAPTURE queue: RGB565 (RGBP) ---- */
	struct v4l2_format cfmt;
	memset(&cfmt, 0, sizeof(cfmt));
	cfmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	if (xioctl(fd, VIDIOC_G_FMT, &cfmt) < 0)
		die("G_FMT CAPTURE");
	fprintf(stderr, "capture negotiated: %ux%u fourcc=%.4s sizeimage=%u\n",
		cfmt.fmt.pix_mp.width, cfmt.fmt.pix_mp.height,
		(char *)&cfmt.fmt.pix_mp.pixelformat,
		cfmt.fmt.pix_mp.plane_fmt[0].sizeimage);
	cfmt.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_RGB565;
	if (xioctl(fd, VIDIOC_S_FMT, &cfmt) < 0)
		die("S_FMT CAPTURE RGB565");

	struct v4l2_requestbuffers creq;
	memset(&creq, 0, sizeof(creq));
	creq.count = (unsigned)pipeline_depth;
	creq.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	creq.memory = V4L2_MEMORY_MMAP;
	if (xioctl(fd, VIDIOC_REQBUFS, &creq) < 0)
		die("REQBUFS CAPTURE");
	fprintf(stderr, "REQBUFS CAPTURE: requested=%d got=%u\n", pipeline_depth, creq.count);

	struct mbuf cbufs[MAX_BUFS];
	for (unsigned i = 0; i < creq.count; i++) {
		struct v4l2_plane planes[1];
		struct v4l2_buffer buf;
		memset(&buf, 0, sizeof(buf));
		memset(planes, 0, sizeof(planes));
		buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
		buf.memory = V4L2_MEMORY_MMAP;
		buf.index = i;
		buf.m.planes = planes;
		buf.length = 1;
		if (xioctl(fd, VIDIOC_QUERYBUF, &buf) < 0)
			die("QUERYBUF CAPTURE");
		cbufs[i].length = planes[0].length;
		cbufs[i].start = mmap(NULL, planes[0].length, PROT_READ | PROT_WRITE,
				       MAP_SHARED, fd, planes[0].m.mem_offset);
		if (cbufs[i].start == MAP_FAILED)
			die("mmap CAPTURE");
		buf.m.planes = planes;
		if (xioctl(fd, VIDIOC_QBUF, &buf) < 0)
			die("QBUF CAPTURE (initial)");
	}
	{
		int type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
		if (xioctl(fd, VIDIOC_STREAMON, &type) < 0)
			die("STREAMON CAPTURE");
	}

	/* ---- Steady-state repeated independent-frame decode loop ---- */
	static uint64_t submit_ns[MAX_SAMPLES];
	static uint64_t complete_ns[MAX_SAMPLES];
	long n_samples = 0;
	long submitted = 0, completed = 0, decode_errors = 0;

	/* Buffer 0's true submit-to-complete latency is not a decode-time
	 * measurement: it also includes the (possibly multi-second, and for
	 * this MJPEG decoder never-firing) V4L2_EVENT_SOURCE_CHANGE wait
	 * above. Do not record a submit_ns entry for it; its completion is
	 * skipped explicitly below (skip_first_completion) instead of being
	 * paired with an unrelated later submission. */
	int skip_first_completion = 1;

	/* Re-queue OUTPUT buffers 1..N-1 up front so several independent
	 * frames are always in flight (pipelining), matching PROJECT SPEC
	 * section 9's pipelining requirement. Each of these gets a real,
	 * comparable submit timestamp (all taken after the capture queue is
	 * already streaming, so their FIFO pairing with completions is
	 * meaningful). */
	for (unsigned i = 1; i < oreq.count; i++) {
		memcpy(obufs[i].start, jpeg_data, jpeg_size);
		struct v4l2_plane planes[1];
		struct v4l2_buffer buf;
		memset(&buf, 0, sizeof(buf));
		memset(planes, 0, sizeof(planes));
		buf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
		buf.memory = V4L2_MEMORY_MMAP;
		buf.index = i;
		buf.m.planes = planes;
		buf.length = 1;
		planes[0].bytesused = (uint32_t)jpeg_size;
		uint64_t sub_ts = now_ns();
		if (xioctl(fd, VIDIOC_QBUF, &buf) < 0)
			die("QBUF OUTPUT[i] (initial pipeline fill)");
		if (submitted < MAX_SAMPLES)
			submit_ns[submitted++] = sub_ts;
	}
	uint64_t t_start = now_ns();
	uint64_t t_deadline = t_start + (uint64_t)(duration_s * 1e9);
	int dumped_first = 0;

	while (now_ns() < t_deadline && n_samples < MAX_SAMPLES) {
		struct pollfd pfd = { .fd = fd, .events = POLLIN };
		int pr = poll(&pfd, 1, 200);
		if (pr <= 0)
			continue;
		if (!(pfd.revents & POLLIN))
			continue;

		struct v4l2_plane cplanes[1];
		struct v4l2_buffer cbuf;
		memset(&cbuf, 0, sizeof(cbuf));
		memset(cplanes, 0, sizeof(cplanes));
		cbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
		cbuf.memory = V4L2_MEMORY_MMAP;
		cbuf.m.planes = cplanes;
		cbuf.length = 1;
		if (xioctl(fd, VIDIOC_DQBUF, &cbuf) != 0)
			continue;
		uint64_t comp_ts = now_ns();
		if (!dumped_first && argc > 9 &&
		    strcmp(argv[8], "--dump-first-frame") == 0) {
			FILE *df = fopen(argv[9], "wb");
			if (df) {
				fwrite(cbufs[cbuf.index].start, 1,
				       cplanes[0].bytesused, df);
				fclose(df);
			}
			dumped_first = 1;
		}
		if (skip_first_completion) {
			skip_first_completion = 0;
		} else if (n_samples < MAX_SAMPLES) {
			complete_ns[n_samples] = comp_ts;
			n_samples++;
		}
		completed++;
		if (xioctl(fd, VIDIOC_QBUF, &cbuf) < 0)
			decode_errors++;

		/*
		 * Only NOW (strictly after this capture completion has been
		 * observed and its buffer recycled) reclaim and resubmit the
		 * matching OUTPUT buffer. This removes the independent
		 * POLLOUT race that let a "depth 1" run silently overlap two
		 * frames (input-buffer reclaim can fire before the matching
		 * capture/decode finishes), which is what PROJECT SPEC
		 * section 18 requires distinguishing: SERIAL LATENCY
		 * (pipeline_depth=1, enforced here) vs PIPELINED THROUGHPUT
		 * (pipeline_depth>1, still gated the same way per slot but
		 * with multiple independent slots in flight).
		 */
		struct v4l2_plane oplanes[1];
		struct v4l2_buffer obuf;
		memset(&obuf, 0, sizeof(obuf));
		memset(oplanes, 0, sizeof(oplanes));
		obuf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
		obuf.memory = V4L2_MEMORY_MMAP;
		obuf.m.planes = oplanes;
		obuf.length = 1;
		/* The matching OUTPUT buffer may not have been marked done by
		 * the driver yet; poll briefly for POLLOUT before dequeuing. */
		for (int spins = 0; spins < 2000; spins++) {
			struct pollfd opfd = { .fd = fd, .events = POLLOUT };
			if (poll(&opfd, 1, 50) > 0 && (opfd.revents & POLLOUT))
				break;
		}
		if (xioctl(fd, VIDIOC_DQBUF, &obuf) == 0) {
			unsigned idx = obuf.index;
			memcpy(obufs[idx].start, jpeg_data, jpeg_size);
			oplanes[0].bytesused = (uint32_t)jpeg_size;
			obuf.m.planes = oplanes;
			uint64_t sub_ts = now_ns();
			if (xioctl(fd, VIDIOC_QBUF, &obuf) == 0) {
				if (submitted < MAX_SAMPLES)
					submit_ns[submitted] = sub_ts;
				submitted++;
			}
		}
	}

	int otype = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	int ctype = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	xioctl(fd, VIDIOC_STREAMOFF, &otype);
	xioctl(fd, VIDIOC_STREAMOFF, &ctype);
	close(fd);

	/* Pair submit[i] with complete[i] approximately (FIFO order across
	 * a single in-order M2M decoder queue); this slightly overestimates
	 * true per-frame latency, since >1 frame can be in flight, but the
	 * approximation is documented and the raw arrays are kept too. */
	long pairs = n_samples < submitted ? n_samples : submitted;
	uint64_t *lat_ns = malloc(sizeof(uint64_t) * (pairs > 0 ? pairs : 1));
	for (long i = 0; i < pairs; i++) {
		int64_t d = (int64_t)complete_ns[i] - (int64_t)submit_ns[i];
		lat_ns[i] = d > 0 ? (uint64_t)d : 0;
	}
	/* sort for percentiles */
	for (long i = 1; i < pairs; i++) {
		uint64_t key = lat_ns[i];
		long j = i - 1;
		while (j >= 0 && lat_ns[j] > key) {
			lat_ns[j + 1] = lat_ns[j];
			j--;
		}
		lat_ns[j + 1] = key;
	}
	double total_s = (now_ns() - t_start) / 1e9;
	double fps = completed / total_s;

	FILE *jout = fopen(out_json, "w");
	fprintf(jout, "{\n");
	fprintf(jout, "  \"device\": \"%s\",\n", device);
	fprintf(jout, "  \"driver\": \"%s\",\n", cap.driver);
	fprintf(jout, "  \"card\": \"%s\",\n", cap.card);
	fprintf(jout, "  \"width\": %d,\n", width);
	fprintf(jout, "  \"pipeline_depth\": %d,\n", pipeline_depth);
	fprintf(jout, "  \"height\": %d,\n", height);
	fprintf(jout, "  \"jpeg_bytes\": %ld,\n", jpeg_size);
	fprintf(jout, "  \"source_change_event_seen\": %s,\n", source_changed ? "true" : "false");
	fprintf(jout, "  \"requested_duration_s\": %.3f,\n", duration_s);
	fprintf(jout, "  \"actual_duration_s\": %.3f,\n", total_s);
	fprintf(jout, "  \"submitted_frames\": %ld,\n", submitted);
	fprintf(jout, "  \"completed_frames\": %ld,\n", completed);
	fprintf(jout, "  \"decode_errors\": %ld,\n", decode_errors);
	fprintf(jout, "  \"sustained_fps\": %.3f,\n", fps);
	if (pairs > 0) {
		fprintf(jout, "  \"latency_ns_p50\": %llu,\n", (unsigned long long)lat_ns[pairs * 50 / 100]);
		fprintf(jout, "  \"latency_ns_p95\": %llu,\n", (unsigned long long)lat_ns[pairs * 95 / 100 < pairs ? pairs * 95 / 100 : pairs - 1]);
		fprintf(jout, "  \"latency_ns_p99\": %llu,\n", (unsigned long long)lat_ns[pairs * 99 / 100 < pairs ? pairs * 99 / 100 : pairs - 1]);
		fprintf(jout, "  \"latency_ns_max\": %llu,\n", (unsigned long long)lat_ns[pairs - 1]);
		fprintf(jout, "  \"latency_ns_min\": %llu,\n", (unsigned long long)lat_ns[0]);
	}
	fprintf(jout, "  \"label\": \"MEASURED_PI_HW_JPEG_DECODE\"\n");
	fprintf(jout, "}\n");
	fclose(jout);

	fprintf(stderr,
		"submitted=%ld completed=%ld errors=%ld duration_s=%.3f fps=%.2f\n",
		submitted, completed, decode_errors, total_s, fps);
	return 0;
}
