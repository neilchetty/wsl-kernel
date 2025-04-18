/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright(c) 2022 Intel Corporation. */

#ifndef _IFS_H_
#define _IFS_H_

/**
 * DOC: In-Field Scan
 *
 * =============
 * In-Field Scan
 * =============
 *
 * Introduction
 * ------------
 *
 * In Field Scan (IFS) is a hardware feature to run circuit level tests on
 * a CPU core to detect problems that are not caught by parity or ECC checks.
 * Future CPUs will support more than one type of test which will show up
 * with a new platform-device instance-id.
 *
 *
 * IFS Image
 * ---------
 *
 * Intel provides firmware files containing the scan tests via the webpage [#f1]_.
 * Look under "In-Field Scan Test Images Download" section towards the
 * end of the page. Similar to microcode, there are separate files for each
 * family-model-stepping. IFS Images are not applicable for some test types.
 * Wherever applicable the sysfs directory would provide a "current_batch" file
 * (see below) for loading the image.
 *
 * .. [#f1] https://intel.com/InFieldScan
 *
 * IFS Image Loading
 * -----------------
 *
 * The driver loads the tests into memory reserved BIOS local to each CPU
 * socket in a two step process using writes to MSRs to first load the
 * SHA hashes for the test. Then the tests themselves. Status MSRs provide
 * feedback on the success/failure of these steps.
 *
 * The test files are kept in a fixed location: /lib/firmware/intel/ifs_<n>/
 * For e.g if there are 3 test files, they would be named in the following
 * fashion:
 * ff-mm-ss-01.scan
 * ff-mm-ss-02.scan
 * ff-mm-ss-03.scan
 * (where ff refers to family, mm indicates model and ss indicates stepping)
 *
 * A different test file can be loaded by writing the numerical portion
 * (e.g 1, 2 or 3 in the above scenario) into the curent_batch file.
 * To load ff-mm-ss-02.scan, the following command can be used::
 *
 *   # echo 2 > /sys/devices/virtual/misc/intel_ifs_<n>/current_batch
 *
 * The above file can also be read to know the currently loaded image.
 *
 * Running tests
 * -------------
 *
 * Tests are run by the driver synchronizing execution of all threads on a
 * core and then writing to the ACTIVATE_SCAN MSR on all threads. Instruction
 * execution continues when:
 *
 * 1) All tests have completed.
 * 2) Execution was interrupted.
 * 3) A test detected a problem.
 *
 * Note that ALL THREADS ON THE CORE ARE EFFECTIVELY OFFLINE FOR THE
 * DURATION OF THE TEST. This can be up to 200 milliseconds. If the system
 * is running latency sensitive applications that cannot tolerate an
 * interruption of this magnitude, the system administrator must arrange
 * to migrate those applications to other cores before running a core test.
 * It may also be necessary to redirect interrupts to other CPUs.
 *
 * In all cases reading the corresponding test's STATUS MSR provides details on what
 * happened. The driver makes the value of this MSR visible to applications
 * via the "details" file (see below). Interrupted tests may be restarted.
 *
 * The IFS driver provides sysfs interfaces via /sys/devices/virtual/misc/intel_ifs_<n>/
 * to control execution:
 *
 * Test a specific core::
 *
 *   # echo <cpu#> > /sys/devices/virtual/misc/intel_ifs_<n>/run_test
 *
 * when HT is enabled any of the sibling cpu# can be specified to test
 * its corresponding physical core. Since the tests are per physical core,
 * the result of testing any thread is same. All siblings must be online
 * to run a core test. It is only necessary to test one thread.
 *
 * For e.g. to test core corresponding to cpu5
 *
 *   # echo 5 > /sys/devices/virtual/misc/intel_ifs_<n>/run_test
 *
 * Results of the last test is provided in /sys::
 *
 *   $ cat /sys/devices/virtual/misc/intel_ifs_<n>/status
 *   pass
 *
 * Status can be one of pass, fail, untested
 *
 * Additional details of the last test is provided by the details file::
 *
 *   $ cat /sys/devices/virtual/misc/intel_ifs_<n>/details
 *   0x8081
 *
 * The details file reports the hex value of the test specific status MSR.
 * Hardware defined error codes are documented in volume 4 of the Intel
 * Software Developer's Manual but the error_code field may contain one of
 * the following driver defined software codes:
 *
 * +------+--------------------+
 * | 0xFD | Software timeout   |
 * +------+--------------------+
 * | 0xFE | Partial completion |
 * +------+--------------------+
 *
 * Driver design choices
 * ---------------------
 *
 * 1) The ACTIVATE_SCAN MSR allows for running any consecutive subrange of
 * available tests. But the driver always tries to run all tests and only
 * uses the subrange feature to restart an interrupted test.
 *
 * 2) Hardware allows for some number of cores to be tested in parallel.
 * The driver does not make use of this, it only tests one core at a time.
 *
 * Structural Based Functional Test at Field (SBAF):
 * -------------------------------------------------
 *
 * SBAF is a new type of testing that provides comprehensive core test
 * coverage complementing Scan at Field (SAF) testing. SBAF mimics the
 * manufacturing screening environment and leverages the same test suite.
 * It makes use of Design For Test (DFT) observation sites and features
 * to maximize coverage in minimum time.
 *
 * Similar to the SAF test, SBAF isolates the core under test from the
 * rest of the system during execution. Upon completion, the core
 * seamlessly resets to its pre-test state and resumes normal operation.
 * Any machine checks or hangs encountered during the test are confined to
 * the isolated core, preventing disruption to the overall system.
 *
 * Like the SAF test, the SBAF test is also divided into multiple batches,
 * and each batch test can take hundreds of milliseconds (100-200 ms) to
 * complete. If such a lengthy interruption is undesirable, it is
 * recommended to relocate the time-sensitive applications to other cores.
 */
#include <linux/device.h>
#include <linux/miscdevice.h>

#define MSR_ARRAY_BIST				0x00000105

#define MSR_COPY_SBAF_HASHES			0x000002b8
#define MSR_SBAF_HASHES_STATUS			0x000002b9
#define MSR_AUTHENTICATE_AND_COPY_SBAF_CHUNK	0x000002ba
#define MSR_SBAF_CHUNKS_AUTHENTICATION_STATUS	0x000002bb
#define MSR_ACTIVATE_SBAF			0x000002bc
#define MSR_SBAF_STATUS				0x000002bd

#define MSR_COPY_SCAN_HASHES			0x000002c2
#define MSR_SCAN_HASHES_STATUS			0x000002c3
#define MSR_AUTHENTICATE_AND_COPY_CHUNK		0x000002c4
#define MSR_CHUNKS_AUTHENTICATION_STATUS	0x000002c5
#define MSR_ACTIVATE_SCAN			0x000002c6
#define MSR_SCAN_STATUS				0x000002c7
#define MSR_ARRAY_TRIGGER			0x000002d6
#define MSR_ARRAY_STATUS			0x000002d7
#define MSR_SAF_CTRL				0x000004f0
#define MSR_SBAF_CTRL				0x000004f8

#define SCAN_NOT_TESTED				0
#define SCAN_TEST_PASS				1
#define SCAN_TEST_FAIL				2

#define IFS_TYPE_SAF			0
#define IFS_TYPE_ARRAY_BIST		1
#define IFS_TYPE_SBAF			2

#define ARRAY_GEN0			0
#define ARRAY_GEN1			1

/* MSR_SCAN_HASHES_STATUS bit fields */
union ifs_scan_hashes_status {
	u64	data;
	struct {
		u32	chunk_size	:16;
		u32	num_chunks	:8;
		u32	rsvd1		:8;
		u32	error_code	:8;
		u32	rsvd2		:11;
		u32	max_core_limit	:12;
		u32	valid		:1;
	};
};

union ifs_scan_hashes_status_gen2 {
	u64	data;
	struct {
		u16	chunk_size;
		u16	num_chunks;
		u32	error_code		:8;
		u32	chunks_in_stride	:9;
		u32	rsvd			:2;
		u32	max_core_limit		:12;
		u32	valid			:1;
	};
};

/* MSR_CHUNKS_AUTH_STATUS bit fields */
union ifs_chunks_auth_status {
	u64	data;
	struct {
		u32	valid_chunks	:8;
		u32	total_chunks	:8;
		u32	rsvd1		:16;
		u32	error_code	:8;
		u32	rsvd2		:24;
	};
};

union ifs_chunks_auth_status_gen2 {
	u64	data;
	struct {
		u16	valid_chunks;
		u16	total_chunks;
		u32	error_code	:8;
		u32	rsvd2		:8;
		u32	max_bundle	:16;
	};
};

/* MSR_ACTIVATE_SCAN bit fields */
union ifs_scan {
	u64	data;
	struct {
		union {
			struct {
				u8	start;
				u8	stop;
				u16	rsvd;
			} gen0;
			struct {
				u16	start;
				u16	stop;
			} gen2;
		};
		u32	delay	:31;
		u32	sigmce	:1;
	};
};

/* MSR_SCAN_STATUS bit fields */
union ifs_status {
	u64	data;
	struct {
		union {
			struct {
				u8	chunk_num;
				u8	chunk_stop_index;
				u16	rsvd1;
			} gen0;
			struct {
				u16	chunk_num;
				u16	chunk_stop_index;
			} gen2;
		};
		u32	error_code		:8;
		u32	rsvd2			:22;
		u32	control_error		:1;
		u32	signature_error		:1;
	};
};

/* MSR_ARRAY_BIST bit fields */
union ifs_array {
	u64	data;
	struct {
		u32	array_bitmask;
		u16	array_bank;
		u16	rsvd			:15;
		u16	ctrl_result		:1;
	};
};

/* MSR_ACTIVATE_SBAF bit fields */
union ifs_sbaf {
	u64	data;
	struct {
		u32	bundle_idx	:9;
		u32	rsvd1		:5;
		u32	pgm_idx		:2;
		u32	rsvd2		:16;
		u32	delay		:31;
		u32	sigmce		:1;
	};
};

/* MSR_SBAF_STATUS bit fields */
union ifs_sbaf_status {
	u64	data;
	struct {
		u32	bundle_idx	:9;
		u32	rsvd1		:5;
		u32	pgm_idx		:2;
		u32	rsvd2		:16;
		u32	error_code	:8;
		u32	rsvd3		:21;
		u32	test_fail	:1;
		u32	sbaf_status	:2;
	};
};

/*
 * Driver populated error-codes
 * 0xFD: Test timed out before completing all the chunks.
 * 0xFE: not all scan chunks were executed. Maximum forward progress retries exceeded.
 */
#define IFS_SW_TIMEOUT				0xFD
#define IFS_SW_PARTIAL_COMPLETION		0xFE

#define IFS_SUFFIX_SZ		5

struct ifs_test_caps {
	int	integrity_cap_bit;
	int	test_num;
	char	image_suffix[IFS_SUFFIX_SZ];
};

/**
 * struct ifs_test_msrs - MSRs used in IFS tests
 * @copy_hashes: Copy test hash data
 * @copy_hashes_status: Status of copied test hash data
 * @copy_chunks: Copy chunks of the test data
 * @copy_chunks_status: Status of the copied test data chunks
 * @test_ctrl: Control the test attributes
 */
struct ifs_test_msrs {
	u32	copy_hashes;
	u32	copy_hashes_status;
	u32	copy_chunks;
	u32	copy_chunks_status;
	u32	test_ctrl;
};

/**
 * struct ifs_data - attributes related to intel IFS driver
 * @loaded_version: stores the currently loaded ifs image version.
 * @loaded: If a valid test binary has been loaded into the memory
 * @loading_error: Error occurred on another CPU while loading image
 * @valid_chunks: number of chunks which could be validated.
 * @status: it holds simple status pass/fail/untested
 * @scan_details: opaque scan status code from h/w
 * @cur_batch: number indicating the currently loaded test file
 * @generation: IFS test generation enumerated by hardware
 * @chunk_size: size of a test chunk
 * @array_gen: test generation of array test
 * @max_bundle: maximum bundle index
 */
struct ifs_data {
	int	loaded_version;
	bool	loaded;
	bool	loading_error;
	int	valid_chunks;
	int	status;
	u64	scan_details;
	u32	cur_batch;
	u32	generation;
	u32	chunk_size;
	u32	array_gen;
	u32	max_bundle;
};

struct ifs_work {
	struct work_struct w;
	struct device *dev;
};

struct ifs_device {
	const struct ifs_test_caps *test_caps;
	const struct ifs_test_msrs *test_msrs;
	struct ifs_data rw_data;
	struct miscdevice misc;
};

static inline struct ifs_data *ifs_get_data(struct device *dev)
{
	struct miscdevice *m = dev_get_drvdata(dev);
	struct ifs_device *d = container_of(m, struct ifs_device, misc);

	return &d->rw_data;
}

static inline const struct ifs_test_caps *ifs_get_test_caps(struct device *dev)
{
	struct miscdevice *m = dev_get_drvdata(dev);
	struct ifs_device *d = container_of(m, struct ifs_device, misc);

	return d->test_caps;
}

static inline const struct ifs_test_msrs *ifs_get_test_msrs(struct device *dev)
{
	struct miscdevice *m = dev_get_drvdata(dev);
	struct ifs_device *d = container_of(m, struct ifs_device, misc);

	return d->test_msrs;
}

extern bool *ifs_pkg_auth;
int ifs_load_firmware(struct device *dev);
int do_core_test(int cpu, struct device *dev);
extern struct attribute *plat_ifs_attrs[];
extern struct attribute *plat_ifs_array_attrs[];

#endif
