/*
 * The ADC library for the specific card
 *
 * Copyright (C) 2013 CERN (www.cern.ch)
 * Author: Federico Vaga <federico.vaga@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * version 2 as published by the Free Software Foundation or, at your
 * option, any later version.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>

#include <linux/zio-user.h>

#include "fmcadc-lib.h"
#include "fmcadc-lib-int.h"

#define ZIO_SYS_PATH "/sys/bus/zio/devices"

#define FMCADC_NCHAN 4

struct fmcadc_dev *fmcadc_zio_open(const struct fmcadc_board_type *b,
				   unsigned int dev_id,
				   unsigned long totalsamples,
				   unsigned int nbuffer,
				   unsigned long flags)
{
	struct __fmcadc_dev_zio *fa;
	struct stat st;
	char *syspath, *devpath, fname[128];
	int udev_zio_dir = 1;

	/* Check if device exists by looking in sysfs */
	asprintf(&syspath, "%s/%s-%04x", ZIO_SYS_PATH, b->devname, dev_id);
	if (stat(syspath, &st))
		goto out_fa_stat; /* ENOENT or equivalent */

	/* ZIO char devices are in /dev/zio or just /dev (older udev) */
	if (stat("/dev/zio", &st) < 0)
		udev_zio_dir = 0;
	asprintf(&devpath, "%s/%s-%04x", (udev_zio_dir ? "/dev/zio" : "/dev"),
		 b->devname, dev_id);

	/* Sysfs path exists, so device is there, hopefully */
	fa = calloc(1, sizeof(*fa));
	if (!fa)
		goto out_fa_alloc;
	fa->sysbase = syspath;
	fa->devbase = devpath;
	fa->cset = 0;

	/* Open char devices */
	sprintf(fname, "%s-0-i-ctrl", fa->devbase);
	fa->fdc = open(fname, O_RDONLY);
	sprintf(fname, "%s-0-i-data", fa->devbase);
	fa->fdd = open(fname, O_RDONLY);
	if (fa->fdc < 0 || fa->fdd < 0)
		goto out_fa_open;

	fa->gid.board = b;

	/*
	 * We need to save the page size and samplesize.
	 * Samplesize includes the nchan in the count.
	 */
	fa->samplesize = 8; /* FIXME: should read sysfs instead -- where? */
	fa->pagesize = getpagesize();

	/* Finally, support verbose operation */
	if (getenv("LIB_FMCADC_VERBOSE"))
		fa->flags |= FMCADC_FLAG_VERBOSE;

	return (void *) &fa->gid;

out_fa_open:
	if (fa->fdc >= 0)
		close(fa->fdc);
	if (fa->fdd >= 0)
		close(fa->fdd);
	free(fa);
out_fa_alloc:
	free(devpath);
out_fa_stat:
	free(syspath);

	return NULL;
}

int fmcadc_zio_close(struct fmcadc_dev *dev)
{
	struct __fmcadc_dev_zio *fa = to_dev_zio(dev);

	close(fa->fdc);
	close(fa->fdd);
	free(fa->sysbase);
	free(fa->devbase);
	free(fa);
	return 0;
}

/* poll is used by start, so it's defined first */
int fmcadc_zio_acq_poll(struct fmcadc_dev *dev,
			unsigned int flags, struct timeval *timeout)
{
	struct __fmcadc_dev_zio *fa = to_dev_zio(dev);
	fd_set set;
	int err;

	FD_ZERO(&set);
	FD_SET(fa->fdc, &set);
	err = select(fa->fdc + 1, &set, NULL, NULL, timeout);
	switch (err) {
	case 0:
		errno = EAGAIN;
		return -1;
	case 1:
		return 0;
	default:
		return err;
	}
}

int fmcadc_zio_acq_start(struct fmcadc_dev *dev,
			 unsigned int flags, struct timeval *timeout)
{
	struct __fmcadc_dev_zio *fa = to_dev_zio(dev);
	uint32_t cmd = 1; /* hw command for "start" */
	int err;

	err = fa_zio_sysfs_set(fa, "cset0/fsm-command", &cmd);
	if (err)
		return err;

	if (timeout && timeout->tv_sec == 0 && timeout->tv_usec == 0)
		return 0;

	return fmcadc_zio_acq_poll(dev, flags, timeout);
}

int fmcadc_zio_acq_stop(struct fmcadc_dev *dev,	unsigned int flags)
{
	struct __fmcadc_dev_zio *fa = to_dev_zio(dev);
	uint32_t cmd = 2; /* hw command for "stop" */

	return fa_zio_sysfs_set(fa, "cset0/fsm-command", &cmd);
}



