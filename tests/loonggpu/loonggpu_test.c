/*
 * Copyright (C) 2020 Loongson Technology Co., Ltd.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <stdarg.h>
#include <stdint.h>

#include "drm.h"
#include "xf86drmMode.h"
#include "xf86drm.h"

#include "CUnit/Basic.h"

#include "loonggpu_test.h"
#include "loonggpu_internal.h"

/* Test suite names */
#define BASIC_TESTS_STR "Basic Tests"
#define BO_TESTS_STR "BO Tests"
#define DMA_TESTS_STR "DMA Tests"
#define CS_TESTS_STR "CS Tests"
#define DEADLOCK_TESTS_STR "Deadlock Tests"
#define VM_TESTS_STR "VM Tests"

/**
 *  Open handles for loonggpu devices
 *
 */
int drm_loonggpu[MAX_CARDS_SUPPORTED];

/** Open render node to test */
int open_render_node = 0;	/* By default run most tests on primary node */

/** The table of all known test suites to run */
static CU_SuiteInfo suites[] = {
	{
		.pName = BASIC_TESTS_STR,
		.pInitFunc = suite_basic_tests_init,
		.pCleanupFunc = suite_basic_tests_clean,
		.pTests = basic_tests,
	},
	{
		.pName = BO_TESTS_STR,
		.pInitFunc = suite_bo_tests_init,
		.pCleanupFunc = suite_bo_tests_clean,
		.pTests = bo_tests,
	},
	{
		.pName = DMA_TESTS_STR,
		.pInitFunc = suite_dma_tests_init,
		.pCleanupFunc = suite_dma_tests_clean,
		.pTests = dma_tests,
	},
	{
		.pName = DEADLOCK_TESTS_STR,
		.pInitFunc = suite_deadlock_tests_init,
		.pCleanupFunc = suite_deadlock_tests_clean,
		.pTests = deadlock_tests,
	},
	{
		.pName = VM_TESTS_STR,
		.pInitFunc = suite_vm_tests_init,
		.pCleanupFunc = suite_vm_tests_clean,
		.pTests = vm_tests,
	},

	CU_SUITE_INFO_NULL,
};

typedef CU_BOOL (*active__stat_func)(void);

typedef struct Suites_Active_Status {
	char*             pName;
	active__stat_func pActive;
}Suites_Active_Status;

static CU_BOOL always_active()
{
	return CU_TRUE;
}

static Suites_Active_Status suites_active_stat[] = {
		{
			.pName = BASIC_TESTS_STR,
			.pActive = always_active,
		},
		{
			.pName = BO_TESTS_STR,
			.pActive = always_active,
		},
		{
			.pName = DMA_TESTS_STR,
			.pActive = always_active,
		},
		{
			.pName = DEADLOCK_TESTS_STR,
			.pActive = suite_deadlock_tests_enable,
		},
		{
			.pName = VM_TESTS_STR,
			.pActive = suite_vm_tests_enable,
		},
};


/*
 * Display information about all  suites and their tests
 *
 * NOTE: Must be run after registry is initialized and suites registered.
 */
static void display_test_suites(void)
{
	int iSuite;
	int iTest;
	CU_pSuite pSuite = NULL;
	CU_pTest  pTest  = NULL;

	printf("Suites\n");

	for (iSuite = 0; suites[iSuite].pName != NULL; iSuite++) {

		pSuite = CU_get_suite_by_index((unsigned int) iSuite + 1,
						      CU_get_registry());

		if (!pSuite) {
			fprintf(stderr, "Invalid suite id : %d\n", iSuite + 1);
			continue;
		}

		printf("Suite id = %d: Name '%s status: %s'\n",
				iSuite + 1, suites[iSuite].pName,
				pSuite->fActive ? "ENABLED" : "DISABLED");



		for (iTest = 0; suites[iSuite].pTests[iTest].pName != NULL;
			iTest++) {

			pTest = CU_get_test_by_index((unsigned int) iTest + 1,
									pSuite);

			if (!pTest) {
				fprintf(stderr, "Invalid test id : %d\n", iTest + 1);
				continue;
			}

			printf("Test id %d: Name: '%s status: %s'\n", iTest + 1,
					suites[iSuite].pTests[iTest].pName,
					pSuite->fActive && pTest->fActive ?
						     "ENABLED" : "DISABLED");
		}
	}
}


/** Help string for command line parameters */
static const char usage[] =
	"Usage: %s [-hlpr] [<-s <suite id>> [-t <test id>] [-f]] "
	"[-b <pci_bus_id> [-d <pci_device_id>]]\n"
	"where:\n"
	"       l - Display all suites and their tests\n"
	"       r - Run the tests on render node\n"
	"       b - Specify device's PCI bus id to run tests\n"
	"       d - Specify device's PCI device id to run tests (optional)\n"
	"       p - Display information of LOONGGPU devices in system\n"
	"       f - Force executing inactive suite or test\n"
	"       h - Display this help\n";
/** Specified options strings for getopt */
static const char options[]   = "hlrps:t:b:d:f";

/* Open GPU devices.
 * Return the number of GPU device openned.
 */
static int loonggpu_open_devices(int open_render_node)
{
	drmDevicePtr devices[MAX_CARDS_SUPPORTED];
	int i;
	int drm_node;
	int loonggpu_index = 0;
	int drm_count;
	int fd;
	drmVersionPtr version;

	drm_count = drmGetDevices2(0, devices, MAX_CARDS_SUPPORTED);

	if (drm_count < 0) {
		fprintf(stderr,
			"drmGetDevices2() returned an error %d\n",
			drm_count);
		return 0;
	}

	for (i = 0; i < drm_count; i++) {
		/* If this is not PCI device, skip*/
		if (devices[i]->bustype != DRM_BUS_PCI)
			continue;

		/* If this is not Loongson GPU vender ID, skip*/
		if (devices[i]->deviceinfo.pci->vendor_id != 0x0014)
			continue;

		if (open_render_node)
			drm_node = DRM_NODE_RENDER;
		else
			drm_node = DRM_NODE_PRIMARY;

		fd = -1;
		if (devices[i]->available_nodes & 1 << drm_node)
			fd = open(
				devices[i]->nodes[drm_node],
				O_RDWR | O_CLOEXEC);

		/* This node is not available. */
		if (fd < 0) continue;

		version = drmGetVersion(fd);
		if (!version) {
			fprintf(stderr,
				"Warning: Cannot get version for %s."
				"Error is %s\n",
				devices[i]->nodes[drm_node],
				strerror(errno));
			close(fd);
			continue;
		}

		if (strcmp(version->name, "loonggpu")) {
			/* This is not LOONGGPU driver, skip.*/
			drmFreeVersion(version);
			close(fd);
			continue;
		}

		drmFreeVersion(version);

		drm_loonggpu[loonggpu_index] = fd;
		loonggpu_index++;
	}

	drmFreeDevices(devices, drm_count);
	return loonggpu_index;
}

/* Close GPU devices.
 */
static void loonggpu_close_devices()
{
	int i;
	for (i = 0; i < MAX_CARDS_SUPPORTED; i++)
		if (drm_loonggpu[i] >=0)
			close(drm_loonggpu[i]);
}

/* Print GPU devices information */
static void loonggpu_print_devices()
{
	int i;
	drmDevicePtr device;

	/* Open the first GPU devcie to print driver information. */
	if (drm_loonggpu[0] >=0) {
		/* Display GPU driver version information.*/
		drmVersionPtr retval = drmGetVersion(drm_loonggpu[0]);

		if (retval == NULL) {
			perror("Cannot get version for LOONGGPU device");
			return;
		}

		printf("Driver name: %s, Date: %s, Description: %s.\n",
			retval->name, retval->date, retval->desc);
		drmFreeVersion(retval);
	}

	/* Display information of GPU devices */
	printf("Devices:\n");
	for (i = 0; i < MAX_CARDS_SUPPORTED && drm_loonggpu[i] >=0; i++)
		if (drmGetDevice2(drm_loonggpu[i],
			DRM_DEVICE_GET_PCI_REVISION,
			&device) == 0) {
			if (device->bustype == DRM_BUS_PCI) {
				printf("PCI ");
				printf(" domain:%04x",
					device->businfo.pci->domain);
				printf(" bus:%02x",
					device->businfo.pci->bus);
				printf(" device:%02x",
					device->businfo.pci->dev);
				printf(" function:%01x",
					device->businfo.pci->func);
				printf(" vendor_id:%04x",
					device->deviceinfo.pci->vendor_id);
				printf(" device_id:%04x",
					device->deviceinfo.pci->device_id);
				printf(" subvendor_id:%04x",
					device->deviceinfo.pci->subvendor_id);
				printf(" subdevice_id:%04x",
					device->deviceinfo.pci->subdevice_id);
				printf(" revision_id:%02x",
					device->deviceinfo.pci->revision_id);
				printf("\n");
			}
			drmFreeDevice(&device);
		}
}

/* Find a match GPU device in PCI bus
 * Return the index of the device or -1 if not found
 */
static int loonggpu_find_device(uint8_t bus, uint16_t dev)
{
	int i;
	drmDevicePtr device;

	for (i = 0; i < MAX_CARDS_SUPPORTED && drm_loonggpu[i] >= 0; i++) {
		if (drmGetDevice2(drm_loonggpu[i],
			DRM_DEVICE_GET_PCI_REVISION,
			&device) == 0) {
			if (device->bustype == DRM_BUS_PCI)
				if ((bus == 0xFF || device->businfo.pci->bus == bus) &&
					device->deviceinfo.pci->device_id == dev) {
					drmFreeDevice(&device);
					return i;
				}

			drmFreeDevice(&device);
		}
	}

	return -1;
}

static void loonggpu_disable_suites()
{
	loonggpu_device_handle device_handle;
	uint32_t major_version, minor_version, family_id = LOONGGPU_FAMILY_UNKNOWN;
	int i;
	int size = sizeof(suites_active_stat) / sizeof(suites_active_stat[0]);

	if (loonggpu_device_initialize(drm_loonggpu[0], &major_version,
				   &minor_version, &device_handle))
		return;

	family_id = device_handle->info.family_id;

	if (loonggpu_device_deinitialize(device_handle))
		return;

	/* Set active status for suites based on their policies */
	for (i = 0; i < size; ++i)
		if (loonggpu_set_suite_active(suites_active_stat[i].pName,
				suites_active_stat[i].pActive()))
			fprintf(stderr, "suite deactivation failed - %s\n", CU_get_error_msg());

	/* Explicitly disable specific tests due to known bugs or preferences */
	/*
	* BUG: Compute ring stalls and never recovers when the address is
	* written after the command already submitted
	*/
	if (loonggpu_set_test_active(BASIC_TESTS_STR, "Sync dependency Test", CU_FALSE))
		fprintf(stderr, "test Sync dependency Test failed - %s\n", CU_get_error_msg());

	/* if (loonggpu_set_test_active(BO_TESTS_STR, "Metadata", CU_FALSE)) */
		/* fprintf(stderr, "test Metadata Test failed - %s\n", CU_get_error_msg()); */

}

/* The main() function for setting up and running the tests.
 * Returns a CUE_SUCCESS on successful running, another
 * CUnit error code on failure.
 */
int main(int argc, char **argv)
{
	int c;			/* Character received from getopt */
	int i = 0;
	int suite_id = -1;	/* By default run everything */
	int test_id  = -1;	/* By default run all tests in the suite */
	int pci_bus_id = -1;    /* By default PC bus ID is not specified */
	int pci_device_id = 0;  /* By default PC device ID is zero */
	int display_devices = 0;/* By default not to display devices' info */
	CU_pSuite pSuite = NULL;
	CU_pTest  pTest  = NULL;
	int test_device_index;
	int display_list = 0;
	int force_run = 0;

	for (i = 0; i < MAX_CARDS_SUPPORTED; i++)
		drm_loonggpu[i] = -1;


	/* Parse command line string */
	opterr = 0;		/* Do not print error messages from getopt */
	while ((c = getopt(argc, argv, options)) != -1) {
		switch (c) {
		case 'l':
			display_list = 1;
			break;
		case 's':
			suite_id = atoi(optarg);
			break;
		case 't':
			test_id = atoi(optarg);
			break;
		case 'b':
			pci_bus_id = atoi(optarg);
			break;
		case 'd':
			sscanf(optarg, "%x", &pci_device_id);
			break;
		case 'p':
			display_devices = 1;
			break;
		case 'r':
			open_render_node = 1;
			break;
		case 'f':
			force_run = 1;
			break;
		case '?':
		case 'h':
			fprintf(stderr, usage, argv[0]);
			exit(EXIT_SUCCESS);
		default:
			fprintf(stderr, usage, argv[0]);
			exit(EXIT_FAILURE);
		}
	}

	if (loonggpu_open_devices(open_render_node) <= 0) {
		perror("Cannot open LOONGGPU device");
		exit(EXIT_FAILURE);
	}

	if (drm_loonggpu[0] < 0) {
		perror("Cannot open LOONGGPU device");
		exit(EXIT_FAILURE);
	}

	if (display_devices) {
		loonggpu_print_devices();
		loonggpu_close_devices();
		exit(EXIT_SUCCESS);
	}

	if (pci_bus_id > 0 || pci_device_id) {
		/* A device was specified to run the test */
		test_device_index = loonggpu_find_device(pci_bus_id,
						       pci_device_id);

		if (test_device_index >= 0) {
			/* Most tests run on device of drm_loonggpu[0].
			 * Swap the chosen device to drm_loonggpu[0].
			 */
			i = drm_loonggpu[0];
			drm_loonggpu[0] = drm_loonggpu[test_device_index];
			drm_loonggpu[test_device_index] = i;
		} else {
			fprintf(stderr,
				"The specified GPU device does not exist.\n");
			exit(EXIT_FAILURE);
		}
	}

	/* Initialize test suites to run */

	/* initialize the CUnit test registry */
	if (CUE_SUCCESS != CU_initialize_registry()) {
		loonggpu_close_devices();
		return CU_get_error();
	}

	/* Register suites. */
	if (CU_register_suites(suites) != CUE_SUCCESS) {
		fprintf(stderr, "suite registration failed - %s\n",
				CU_get_error_msg());
		CU_cleanup_registry();
		loonggpu_close_devices();
		exit(EXIT_FAILURE);
	}

	/* Run tests using the CUnit Basic interface */
	CU_basic_set_mode(CU_BRM_VERBOSE);

	/* Disable suites and individual tests based on misc. conditions */
	loonggpu_disable_suites();

	if (display_list) {
		display_test_suites();
		goto end;
	}

	if (suite_id != -1) {	/* If user specify particular suite? */
		pSuite = CU_get_suite_by_index((unsigned int) suite_id,
						CU_get_registry());

		if (pSuite) {

			if (force_run)
				CU_set_suite_active(pSuite, CU_TRUE);

			if (test_id != -1) {   /* If user specify test id */
				pTest = CU_get_test_by_index(
						(unsigned int) test_id,
						pSuite);
				if (pTest) {
					if (force_run)
						CU_set_test_active(pTest, CU_TRUE);

					CU_basic_run_test(pSuite, pTest);
				}
				else {
					fprintf(stderr, "Invalid test id: %d\n",
								test_id);
					CU_cleanup_registry();
					loonggpu_close_devices();
					exit(EXIT_FAILURE);
				}
			} else
				CU_basic_run_suite(pSuite);
		} else {
			fprintf(stderr, "Invalid suite id : %d\n",
					suite_id);
			CU_cleanup_registry();
			loonggpu_close_devices();
			exit(EXIT_FAILURE);
		}
	} else
		CU_basic_run_tests();

end:
	CU_cleanup_registry();
	loonggpu_close_devices();
	return CU_get_error();
}
