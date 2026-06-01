/**
 * @file test_hardware_camera.c
 * @brief hardware_camera backend tests with injectable spawn hook.
 */

#include "test_runner.h"
#include "hardware/hardware_camera.h"
#include <stdio.h>
#include <string.h>

static char s_spawn_out_path[256];
static int s_spawn_fail;
static int s_spawn_called;

static int argv_has_shell_metachar(const char *s)
{
	const char *bad = ";|&$`<>\"'\n\r%";
	const char *p;
	if (!s)
		return 0;
	/* GStreamer pipeline token; passed as its own argv element, not via a shell. */
	if (strcmp(s, "!") == 0)
		return 0;
	for (p = s; *p; p++) {
		if (strchr(bad, *p) != NULL)
			return 1;
	}
	return 0;
}

static int argv_all_safe(const char *const *argv)
{
	int i;
	if (!argv)
		return 1;
	for (i = 0; argv[i] != NULL; i++) {
		if (argv_has_shell_metachar(argv[i]))
			return 0;
	}
	return 1;
}

static int mock_spawn(char *const argv[], char *errbuf, size_t errbufsz)
{
	const char *const *last;
	int i;
	(void)errbuf;
	(void)errbufsz;
	s_spawn_called = 1;
	last = hardware_camera_last_argv_for_test();
	ASSERT(last != NULL);
	ASSERT(argv_all_safe(last));
	if (s_spawn_fail) {
		if (errbuf && errbufsz > 0)
			snprintf(errbuf, errbufsz, "%s", HARDWARE_CAMERA_ERR_UNAVAILABLE);
		return -1;
	}
	for (i = 0; argv[i] != NULL; i++) {
		const char *loc = strstr(argv[i], "location=");
		if (loc) {
			snprintf(s_spawn_out_path, sizeof(s_spawn_out_path), "%s",
				 loc + strlen("location="));
			break;
		}
		if (strcmp(argv[i], "--output") == 0 && argv[i + 1]) {
			snprintf(s_spawn_out_path, sizeof(s_spawn_out_path), "%s",
				 argv[i + 1]);
			break;
		}
		if (strcmp(argv[i], "--stream-to") == 0 && argv[i + 1]) {
			snprintf(s_spawn_out_path, sizeof(s_spawn_out_path), "%s",
				 argv[i + 1]);
			break;
		}
	}
	if (s_spawn_out_path[0] != '\0') {
		FILE *f = fopen(s_spawn_out_path, "wb");
		ASSERT(f != NULL);
		ASSERT(fputc(0xff, f) != EOF);
		ASSERT(fputc(0xd8, f) != EOF);
		ASSERT(fputc(0xff, f) != EOF);
		ASSERT(fputc(0xd9, f) != EOF);
		fclose(f);
	}
	return 0;
}

static int setup_mock(void)
{
	s_spawn_fail = 0;
	s_spawn_called = 0;
	s_spawn_out_path[0] = '\0';
	hardware_camera_set_spawn_for_test(mock_spawn);
	ASSERT(hardware_camera_init() == 0);
	return 0;
}

static void teardown(void)
{
	hardware_camera_shutdown();
	hardware_camera_set_spawn_for_test(NULL);
}

static int test_stub_board_unavailable(void)
{
	char result[256];
	char err[128];
	RUN(setup_mock());
	ASSERT(hardware_camera_capture(BOARD_STUB, "auto", "640x480", 75, 0, 0, NULL,
				       result, sizeof(result), err, sizeof(err)) == -1);
	ASSERT(strstr(err, HARDWARE_CAMERA_ERR_UNAVAILABLE) != NULL);
	ASSERT(s_spawn_called == 0);
	teardown();
	return 0;
}

static int test_capture_requires_init(void)
{
	char result[256];
	char err[128];
	hardware_camera_shutdown();
	ASSERT(hardware_camera_capture(BOARD_JETSON_ORIN_NANO, "csi", "640x480", 75, 0,
				       0, NULL, result, sizeof(result), err,
				       sizeof(err)) == -1);
	ASSERT(strstr(err, "not initialized") != NULL);
	return 0;
}

static int test_capture_argument_validation(void)
{
	char result[256];
	char err[128];
	RUN(setup_mock());
	ASSERT(hardware_camera_capture(BOARD_JETSON_ORIN_NANO, "csi", "bad", 75, 0, 0,
				       NULL, result, sizeof(result), err,
				       sizeof(err)) == -1);
	ASSERT(strstr(err, "resolution") != NULL);
	ASSERT(hardware_camera_capture(BOARD_JETSON_ORIN_NANO, "csi", "640x480", 0, 0,
				       0, NULL, result, sizeof(result), err,
				       sizeof(err)) == -1);
	ASSERT(strstr(err, "quality") != NULL);
	ASSERT(hardware_camera_capture(BOARD_JETSON_ORIN_NANO, "csi", "640x480", 75, 9,
				       0, NULL, result, sizeof(result), err,
				       sizeof(err)) == -1);
	ASSERT(strstr(err, "sensor_id") != NULL);
	ASSERT(s_spawn_called == 0);
	teardown();
	return 0;
}

static int test_unsafe_output_path_rejected(void)
{
	char result[256];
	char err[128];
	RUN(setup_mock());
	ASSERT(hardware_camera_capture(BOARD_JETSON_ORIN_NANO, "csi", "640x480", 75, 0,
				       0, "/tmp/evil;rm -rf /", result, sizeof(result),
				       err, sizeof(err)) == -1);
	ASSERT(s_spawn_called == 0);
	teardown();
	return 0;
}

static int test_output_path_traversal_rejected(void)
{
	char result[256];
	char err[128];
	RUN(setup_mock());
	ASSERT(hardware_camera_capture(BOARD_JETSON_ORIN_NANO, "csi", "640x480", 75, 0,
				       0, "/tmp/../etc/passwd.jpg", result,
				       sizeof(result), err, sizeof(err)) == -1);
	ASSERT(strstr(err, "unsafe output path") != NULL);
	ASSERT(s_spawn_called == 0);
	teardown();
	return 0;
}

static int test_resolution_injection_rejected(void)
{
	char result[256];
	char err[128];
	RUN(setup_mock());
	ASSERT(hardware_camera_capture(BOARD_JETSON_ORIN_NANO, "csi", "640x480;rm -rf /",
				       75, 0, 0, NULL, result, sizeof(result), err,
				       sizeof(err)) == -1);
	ASSERT(strstr(err, "resolution") != NULL);
	ASSERT(s_spawn_called == 0);
	teardown();
	return 0;
}

static int test_camera_type_injection_rejected(void)
{
	char result[256];
	char err[128];
	RUN(setup_mock());
	ASSERT(hardware_camera_capture(BOARD_JETSON_ORIN_NANO, "csi|sh", "640x480", 75, 0,
				       0, NULL, result, sizeof(result), err,
				       sizeof(err)) == -1);
	ASSERT(strstr(err, "camera_type") != NULL);
	ASSERT(s_spawn_called == 0);
	teardown();
	return 0;
}

/** sensor_id is int 0-3; argv must use numeric sensor-id= only (no shell tokens). */
static int test_sensor_id_injection_rejected(void)
{
	char result[256];
	char err[128];
	const char *const *argv;
	int i;
	int found_sensor = 0;
	RUN(setup_mock());
	ASSERT(hardware_camera_capture(BOARD_JETSON_ORIN_NANO, "csi", "640x480", 75, 2,
				       0, NULL, result, sizeof(result), err,
				       sizeof(err)) == 0);
	argv = hardware_camera_last_argv_for_test();
	ASSERT(argv != NULL);
	for (i = 0; argv[i] != NULL; i++) {
		if (strncmp(argv[i], "sensor-id=", 10) == 0) {
			found_sensor = 1;
			ASSERT(strcmp(argv[i], "sensor-id=2") == 0);
			ASSERT(argv_has_shell_metachar(argv[i]) == 0);
		}
	}
	ASSERT(found_sensor == 1);
	ASSERT(hardware_camera_capture(BOARD_JETSON_ORIN_NANO, "csi", "640x480", 75, 4,
				       0, NULL, result, sizeof(result), err,
				       sizeof(err)) == -1);
	ASSERT(strstr(err, "sensor_id") != NULL);
	teardown();
	return 0;
}

static int test_no_shell_invocation(void)
{
	const char *const *argv;
	int i;
	char result[256];
	char err[128];
	RUN(setup_mock());
	ASSERT(hardware_camera_capture(BOARD_JETSON_ORIN_NANO, "csi", "640x480", 75, 0,
				       0, NULL, result, sizeof(result), err,
				       sizeof(err)) == 0);
	argv = hardware_camera_last_argv_for_test();
	ASSERT(argv != NULL);
	for (i = 0; argv[i] != NULL; i++) {
		ASSERT(strcmp(argv[i], "sh") != 0);
		ASSERT(strcmp(argv[i], "/bin/sh") != 0);
		ASSERT(strstr(argv[i], " -c ") == NULL);
	}
	teardown();
	return 0;
}

static int test_jetson_csi_argv_no_shell_metacharacters(void)
{
	char result[256];
	char err[128];
	const char *const *argv;
	int i;
	int found_src = 0;
	int found_buffers = 0;
	RUN(setup_mock());
	ASSERT(hardware_camera_capture(BOARD_JETSON_ORIN_NANO, "csi", "640x480", 75, 1,
				       0, NULL, result, sizeof(result), err,
				       sizeof(err)) == 0);
	ASSERT(s_spawn_called == 1);
	argv = hardware_camera_last_argv_for_test();
	ASSERT(argv != NULL);
	ASSERT(argv_all_safe(argv));
	ASSERT(strcmp(argv[0], "gst-launch-1.0") == 0);
	for (i = 0; argv[i] != NULL; i++) {
		if (strcmp(argv[i], "nvarguscamerasrc") == 0)
			found_src = 1;
		if (strcmp(argv[i], "num-buffers=4") == 0)
			found_buffers = 1;
	}
	ASSERT(found_src == 1);
	ASSERT(found_buffers == 1);
	teardown();
	return 0;
}

static int test_spawn_failure_unavailable(void)
{
	char result[256];
	char err[128];
	RUN(setup_mock());
	s_spawn_fail = 1;
	ASSERT(hardware_camera_capture(BOARD_JETSON_ORIN_NANO, "csi", "640x480", 75, 0,
				       0, NULL, result, sizeof(result), err,
				       sizeof(err)) == -1);
	ASSERT(strstr(err, HARDWARE_CAMERA_ERR_UNAVAILABLE) != NULL);
	teardown();
	return 0;
}

static int test_usb_backend_argv(void)
{
	char result[256];
	char err[128];
	const char *const *argv;
	RUN(setup_mock());
	ASSERT(hardware_camera_capture(BOARD_JETSON_ORIN_NANO, "usb", "320x240", 75, 0,
				       2, NULL, result, sizeof(result), err,
				       sizeof(err)) == 0);
	argv = hardware_camera_last_argv_for_test();
	ASSERT(argv != NULL);
	ASSERT(strcmp(argv[0], "v4l2-ctl") == 0);
	ASSERT(strstr(argv[2], "/dev/video2") != NULL);
	teardown();
	return 0;
}

static int test_rpi_csi_argv(void)
{
	char result[256];
	char err[128];
	const char *const *argv;
	int i;
	int found_width = 0;
	int found_height = 0;
	RUN(setup_mock());
	ASSERT(hardware_camera_capture(BOARD_RPI_ZERO2W, "csi", "640x480", 75, 0, 0, NULL,
				       result, sizeof(result), err, sizeof(err)) == 0);
	ASSERT(s_spawn_called == 1);
	argv = hardware_camera_last_argv_for_test();
	ASSERT(argv != NULL);
	ASSERT(strcmp(argv[0], "libcamera-still") == 0);
	for (i = 0; argv[i] != NULL; i++) {
		if (strcmp(argv[i], "--width") == 0 && argv[i + 1] &&
		    strcmp(argv[i + 1], "640") == 0)
			found_width = 1;
		if (strcmp(argv[i], "--height") == 0 && argv[i + 1] &&
		    strcmp(argv[i + 1], "480") == 0)
			found_height = 1;
	}
	ASSERT(found_width == 1);
	ASSERT(found_height == 1);
	teardown();
	return 0;
}

int main(void)
{
	RUN(test_capture_requires_init());
	RUN(test_stub_board_unavailable());
	RUN(test_capture_argument_validation());
	RUN(test_unsafe_output_path_rejected());
	RUN(test_output_path_traversal_rejected());
	RUN(test_resolution_injection_rejected());
	RUN(test_camera_type_injection_rejected());
	RUN(test_sensor_id_injection_rejected());
	RUN(test_no_shell_invocation());
	RUN(test_jetson_csi_argv_no_shell_metacharacters());
	RUN(test_spawn_failure_unavailable());
	RUN(test_usb_backend_argv());
	RUN(test_rpi_csi_argv());
	printf("All hardware_camera tests passed.\n");
	return 0;
}
