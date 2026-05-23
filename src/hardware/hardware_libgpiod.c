/**
 * @file hardware_libgpiod.c
 * @brief libgpiod v2 GPIO backend with mutex-serialized pin access.
 */
#define _POSIX_C_SOURCE 200809L

#include "hardware/hardware_libgpiod.h"
#include <errno.h>
#include <gpiod.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>

#define GPIO_CONSUMER "shellclaw"
#define SFIO_ERR_FMT "pin %d is configured as SFIO (I2C/UART/SPI) in pinmux"

static const hardware_pin_table_t *s_pin_table;
static const hardware_pin_table_t *s_test_pin_table;
static int s_libgpiod_ready;
static pthread_mutex_t s_gpio_mutex = PTHREAD_MUTEX_INITIALIZER;

void hardware_libgpiod_set_pin_table_for_test(const hardware_pin_table_t *table)
{
	s_test_pin_table = table;
}

static const hardware_pin_table_t *active_pin_table(void)
{
	if (s_test_pin_table != NULL)
		return s_test_pin_table;
	return s_pin_table;
}

static const hardware_pin_entry_t *lookup_pin(int pin, char *errbuf, size_t errbufsz)
{
	const hardware_pin_table_t *table = active_pin_table();
	int i;
	if (!table || !table->entries || table->count <= 0) {
		if (errbuf && errbufsz > 0)
			snprintf(errbuf, errbufsz, "gpio: pin table not configured");
		return NULL;
	}
	if (pin < 1 || pin > HARDWARE_HEADER_PIN_COUNT) {
		if (errbuf && errbufsz > 0)
			snprintf(errbuf, errbufsz, "gpio: pin %d out of range (1-%d)",
				 pin, HARDWARE_HEADER_PIN_COUNT);
		return NULL;
	}
	for (i = 0; i < table->count; i++) {
		if (table->entries[i].physical_pin == pin)
			return &table->entries[i];
	}
	if (errbuf && errbufsz > 0)
		snprintf(errbuf, errbufsz, "gpio: pin %d not mapped on this board", pin);
	return NULL;
}

static int reject_sfio(const hardware_pin_entry_t *entry, int pin,
		       char *errbuf, size_t errbufsz)
{
	if (!entry->sfio_flag)
		return 0;
	if (errbuf && errbufsz > 0)
		snprintf(errbuf, errbufsz, SFIO_ERR_FMT, pin);
	return -1;
}

static int chip_path_for_num(unsigned int gpiochip_num, char *path, size_t pathsz)
{
	int n = snprintf(path, pathsz, "/dev/gpiochip%u", gpiochip_num);
	if (n < 0 || (size_t)n >= pathsz)
		return -1;
	return 0;
}

static struct gpiod_line_settings *make_line_settings(int as_output,
						     enum gpiod_line_value output_val)
{
	struct gpiod_line_settings *settings = gpiod_line_settings_new();

	if (!settings)
		return NULL;
	if (as_output) {
		gpiod_line_settings_set_direction(settings, GPIOD_LINE_DIRECTION_OUTPUT);
		gpiod_line_settings_set_output_value(settings, output_val);
	} else {
		gpiod_line_settings_set_direction(settings, GPIOD_LINE_DIRECTION_INPUT);
	}
	return settings;
}

static struct gpiod_line_request *request_from_chip(struct gpiod_chip *chip,
						    unsigned int offset,
						    struct gpiod_line_settings *settings,
						    char *errbuf, size_t errbufsz)
{
	struct gpiod_line_config *line_cfg = NULL;
	struct gpiod_request_config *req_cfg = NULL;
	struct gpiod_line_request *request = NULL;
	int ret;

	line_cfg = gpiod_line_config_new();
	if (!line_cfg)
		return NULL;
	ret = gpiod_line_config_add_line_settings(line_cfg, &offset, 1, settings);
	if (ret != 0) {
		if (errbuf && errbufsz > 0)
			snprintf(errbuf, errbufsz, "gpio: line config failed for offset %u",
				 offset);
		goto done;
	}
	req_cfg = gpiod_request_config_new();
	if (!req_cfg)
		goto done;
	gpiod_request_config_set_consumer(req_cfg, GPIO_CONSUMER);
	request = gpiod_chip_request_lines(chip, req_cfg, line_cfg);
done:
	gpiod_request_config_free(req_cfg);
	gpiod_line_config_free(line_cfg);
	return request;
}

static struct gpiod_line_request *request_line(const hardware_pin_entry_t *entry,
					       int as_output,
					       enum gpiod_line_value output_val,
					       char *errbuf, size_t errbufsz)
{
	char chip_path[32];
	struct gpiod_chip *chip = NULL;
	struct gpiod_line_settings *settings = NULL;
	struct gpiod_line_request *request = NULL;
	unsigned int offset = entry->line_num;

	if (chip_path_for_num(entry->gpiochip_num, chip_path, sizeof(chip_path)) != 0) {
		if (errbuf && errbufsz > 0)
			snprintf(errbuf, errbufsz, "gpio: chip path too long for gpiochip%u",
				 entry->gpiochip_num);
		return NULL;
	}
	chip = gpiod_chip_open(chip_path);
	if (!chip) {
		if (errbuf && errbufsz > 0)
			snprintf(errbuf, errbufsz, "gpio: open %s failed: %s",
				 chip_path, strerror(errno));
		return NULL;
	}
	settings = make_line_settings(as_output, output_val);
	if (!settings)
		goto fail;
	request = request_from_chip(chip, offset, settings, errbuf, errbufsz);
	if (!request && errbuf && errbufsz > 0)
		snprintf(errbuf, errbufsz, "gpio: request line %u on %s failed: %s",
			 offset, chip_path, strerror(errno));
fail:
	gpiod_line_settings_free(settings);
	gpiod_chip_close(chip);
	return request;
}

static void release_request(struct gpiod_line_request *request)
{
	if (request)
		gpiod_line_request_release(request);
}

static int line_value_to_int(enum gpiod_line_value val)
{
	return val == GPIOD_LINE_VALUE_ACTIVE ? 1 : 0;
}

static enum gpiod_line_value int_to_line_value(int value)
{
	return value ? GPIOD_LINE_VALUE_ACTIVE : GPIOD_LINE_VALUE_INACTIVE;
}

int hardware_libgpiod_init(const hardware_pin_table_t *table)
{
	if (!table || !table->entries || table->count <= 0)
		return -1;
	s_pin_table = table;
	s_libgpiod_ready = 1;
	return 0;
}

void hardware_libgpiod_shutdown(void)
{
	s_pin_table = NULL;
	s_test_pin_table = NULL;
	s_libgpiod_ready = 0;
}

int hardware_libgpiod_is_available(void)
{
	return s_libgpiod_ready ? 1 : 0;
}

int hardware_gpio_read(int pin, int *value_out, char *errbuf, size_t errbufsz)
{
	const hardware_pin_entry_t *entry;
	struct gpiod_line_request *request = NULL;
	enum gpiod_line_value val;
	int ret = -1;
	if (!value_out) {
		if (errbuf && errbufsz > 0)
			snprintf(errbuf, errbufsz, "gpio: value_out is NULL");
		return -1;
	}
	if (!s_libgpiod_ready) {
		if (errbuf && errbufsz > 0)
			snprintf(errbuf, errbufsz, "gpio: libgpiod backend not initialized");
		return -1;
	}
	entry = lookup_pin(pin, errbuf, errbufsz);
	if (!entry)
		return -1;
	if (reject_sfio(entry, pin, errbuf, errbufsz) != 0)
		return -1;
	pthread_mutex_lock(&s_gpio_mutex);
	request = request_line(entry, 0, GPIOD_LINE_VALUE_INACTIVE, errbuf, errbufsz);
	if (!request)
		goto done;
	val = gpiod_line_request_get_value(request, entry->line_num);
	if (val == GPIOD_LINE_VALUE_ERROR) {
		if (errbuf && errbufsz > 0)
			snprintf(errbuf, errbufsz, "gpio: read pin %d failed: %s",
				 pin, strerror(errno));
		goto done;
	}
	*value_out = line_value_to_int(val);
	ret = 0;
done:
	release_request(request);
	pthread_mutex_unlock(&s_gpio_mutex);
	return ret;
}

int hardware_gpio_write(int pin, int value, char *errbuf, size_t errbufsz)
{
	const hardware_pin_entry_t *entry;
	struct gpiod_line_request *request = NULL;
	enum gpiod_line_value out_val = int_to_line_value(value);
	int ret = -1;
	if (!s_libgpiod_ready) {
		if (errbuf && errbufsz > 0)
			snprintf(errbuf, errbufsz, "gpio: libgpiod backend not initialized");
		return -1;
	}
	entry = lookup_pin(pin, errbuf, errbufsz);
	if (!entry)
		return -1;
	if (reject_sfio(entry, pin, errbuf, errbufsz) != 0)
		return -1;
	pthread_mutex_lock(&s_gpio_mutex);
	request = request_line(entry, 1, out_val, errbuf, errbufsz);
	if (!request)
		goto done;
	if (gpiod_line_request_set_value(request, entry->line_num, out_val) != 0) {
		if (errbuf && errbufsz > 0)
			snprintf(errbuf, errbufsz, "gpio: write pin %d failed: %s",
				 pin, strerror(errno));
		goto done;
	}
	ret = 0;
done:
	release_request(request);
	pthread_mutex_unlock(&s_gpio_mutex);
	return ret;
}

int hardware_gpio_mode(int pin, const char *mode, char *errbuf, size_t errbufsz)
{
	const hardware_pin_entry_t *entry;
	struct gpiod_line_request *request = NULL;
	int as_output = 0;
	int ret = -1;
	if (!mode) {
		if (errbuf && errbufsz > 0)
			snprintf(errbuf, errbufsz, "gpio: mode is NULL");
		return -1;
	}
	if (strcmp(mode, "input") == 0)
		as_output = 0;
	else if (strcmp(mode, "output") == 0)
		as_output = 1;
	else {
		if (errbuf && errbufsz > 0)
			snprintf(errbuf, errbufsz, "gpio: invalid mode '%s' (expected input|output)",
				 mode);
		return -1;
	}
	if (!s_libgpiod_ready) {
		if (errbuf && errbufsz > 0)
			snprintf(errbuf, errbufsz, "gpio: libgpiod backend not initialized");
		return -1;
	}
	entry = lookup_pin(pin, errbuf, errbufsz);
	if (!entry)
		return -1;
	if (reject_sfio(entry, pin, errbuf, errbufsz) != 0)
		return -1;
	pthread_mutex_lock(&s_gpio_mutex);
	request = request_line(entry, as_output, GPIOD_LINE_VALUE_INACTIVE, errbuf, errbufsz);
	if (!request)
		goto done;
	ret = 0;
done:
	release_request(request);
	pthread_mutex_unlock(&s_gpio_mutex);
	return ret;
}
