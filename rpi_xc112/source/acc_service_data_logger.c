// Copyright (c) Acconeer AB, 2018-2019
// All rights reserved

#include <complex.h>
#include <getopt.h>
#include <signal.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "acc_definitions.h"
#include "acc_device_os.h"
#include "acc_driver_hal.h"
#include "acc_log.h"
#include "acc_rss.h"
#include "acc_service.h"
#include "acc_service_envelope.h"
#include "acc_service_iq.h"
#include "acc_service_power_bins.h"

#include "acc_version.h"

#define DEFAULT_UPDATE_COUNT       0
#define DEFAULT_WAIT_FOR_INTERRUPT true
#define DEFAULT_RANGE_START_M      0.07f
#define DEFAULT_RANGE_END_M        0.5f
#define DEFAULT_N_BINS             10
#define DEFAULT_SERVICE_PROFILE    0         // Use service default profile
#define DEFAULT_GAIN               -1.0f     //-1.0 will trigger that the stack default will be used
#define DEFAULT_FREQUENCY          10.0f
#define DEFAULT_RUNNING_AVG        -1.0f     //-1.0 will trigger that the stack default will be used
#define DEFAULT_SENSOR             1
#define DEFAULT_LOG_LEVEL          ACC_LOG_LEVEL_ERROR

volatile sig_atomic_t interrupted = 0;


typedef enum
{
	INVALID_SERVICE = 0,
	POWER_BIN,
	ENVELOPE,
	IQ
} service_type_t;

typedef struct
{
	service_type_t                 service_type;
	uint16_t                       update_count;
	bool                           wait_for_interrupt;
	float                          start_m;
	float                          end_m;
	float                          frequency;
	int                            n_bins;
	float                          gain;
	uint32_t                       service_profile;
	float                          running_avg;
	int                            sensor;
	acc_log_level_t                log_level;
	char                           *file_path;
} input_t;


static void initialize_input(input_t *input)
{
	input->service_type       = INVALID_SERVICE;
	input->update_count       = DEFAULT_UPDATE_COUNT;
	input->wait_for_interrupt = DEFAULT_WAIT_FOR_INTERRUPT;
	input->start_m            = DEFAULT_RANGE_START_M;
	input->end_m              = DEFAULT_RANGE_END_M;
	input->frequency          = DEFAULT_FREQUENCY;
	input->n_bins             = DEFAULT_N_BINS;
	input->gain               = DEFAULT_GAIN;
	input->service_profile    = DEFAULT_SERVICE_PROFILE;
	input->running_avg        = DEFAULT_RUNNING_AVG;
	input->sensor             = DEFAULT_SENSOR;
	input->log_level          = DEFAULT_LOG_LEVEL;
	input->file_path          = NULL;
}


static bool parse_options(int argc, char *argv[], input_t *input);


static acc_service_configuration_t set_up_power_bin(input_t *input);


static bool execute_power_bin(acc_service_configuration_t power_bin_configuration, char *file_path, bool wait_for_interrupt,
                              uint16_t update_count);


static acc_service_configuration_t set_up_envelope(input_t *input);


static bool execute_envelope(acc_service_configuration_t envelope_configuration, char *file_path, bool wait_for_interrupt,
                             uint16_t update_count);


static acc_service_configuration_t set_up_iq(input_t *input);


static bool execute_iq(acc_service_configuration_t iq_configuration, char *file_path, bool wait_for_interrupt, uint16_t update_count);


static void interrupt_handler(int signum)
{
	if (signum == SIGINT)
	{
		interrupted = 1;
	}
}


int main(int argc, char *argv[])
{
	input_t input;

	initialize_input(&input);

	signal(SIGINT, interrupt_handler);

	if (!acc_driver_hal_init())
	{
		return EXIT_FAILURE;
	}

	if (!parse_options(argc, argv, &input))
	{
		if (input.file_path != NULL)
		{
			acc_os_mem_free(input.file_path);
		}

		return EXIT_FAILURE;
	}

	acc_hal_t hal = acc_driver_hal_get_implementation();

	hal.log.log_level = input.log_level;

	if (!acc_rss_activate(&hal))
	{
		return EXIT_FAILURE;
	}

	bool service_status;

	switch (input.service_type)
	{
		case POWER_BIN:
		{
			acc_service_configuration_t power_bin_configuration = set_up_power_bin(&input);

			if (power_bin_configuration == NULL)
			{
				if (input.file_path != NULL)
				{
					acc_os_mem_free(input.file_path);
				}

				return EXIT_FAILURE;
			}

			service_status = execute_power_bin(power_bin_configuration, input.file_path, input.wait_for_interrupt, input.update_count);

			if (input.file_path != NULL)
			{
				acc_os_mem_free(input.file_path);
			}

			if (!service_status)
			{
				printf("execute_power_bin() failed\n");
				return EXIT_FAILURE;
			}

			acc_service_power_bins_configuration_destroy(&power_bin_configuration);
			break;
		}

		case ENVELOPE:
		{
			acc_service_configuration_t envelope_configuration = set_up_envelope(&input);

			if (envelope_configuration == NULL)
			{
				if (input.file_path != NULL)
				{
					acc_os_mem_free(input.file_path);
				}

				return EXIT_FAILURE;
			}

			service_status = execute_envelope(envelope_configuration, input.file_path, input.wait_for_interrupt, input.update_count);

			if (input.file_path != NULL)
			{
				acc_os_mem_free(input.file_path);
			}

			if (!service_status)
			{
				printf("execute_envelope() failed\n");
				return EXIT_FAILURE;
			}

			acc_service_envelope_configuration_destroy(&envelope_configuration);
			break;
		}

		case IQ:
		{
			acc_service_configuration_t iq_configuration = set_up_iq(&input);

			if (iq_configuration == NULL)
			{
				if (input.file_path != NULL)
				{
					acc_os_mem_free(input.file_path);
				}
			}

			service_status = execute_iq(iq_configuration, input.file_path, input.wait_for_interrupt, input.update_count);

			if (input.file_path != NULL)
			{
				acc_os_mem_free(input.file_path);
			}

			if (!service_status)
			{
				printf("execute_iq() failed\n");
				return EXIT_FAILURE;
			}

			acc_service_iq_configuration_destroy(&iq_configuration);
			break;
		}
		default:
		{
			printf("Invalid service_type %d\n", input.service_type);
			return EXIT_FAILURE;
		}
	}

	acc_rss_deactivate();

	return EXIT_SUCCESS;
}


static void print_usage(void)
{
	printf("Usage: data_logger [OPTION]...\n\n");
	printf("-h, --help                this help\n");
	printf("-t, --service-type        service type to be run\n");
	printf("                            0. Power bin\n");
	printf("                            1. Envelope\n");
	printf("                            2. IQ\n");
	printf("-c, --sweep-count         number of updates, default application continues until interrupt\n");
	printf("-b, --range-start         start measurements at this distance [m], default %" PRIfloat "\n",
	       ACC_LOG_FLOAT_TO_INTEGER(DEFAULT_RANGE_START_M));
	printf("-e, --range-end           end measurements at this distance [m], default %" PRIfloat "\n",
	       ACC_LOG_FLOAT_TO_INTEGER(DEFAULT_RANGE_END_M));
	printf("-f, --frequency           update rate, default %" PRIfloat "\n",
	       ACC_LOG_FLOAT_TO_INTEGER(DEFAULT_FREQUENCY));
	printf("-g, --gain                gain (default service dependent)\n");
	printf("-n, --number-of-bins      number of bins (powerbins only), default %d.\n", DEFAULT_N_BINS);
	printf("-o, --out                 path to out file, default stdout\n");
	printf("-y, --service-profile     service profile to use (starting at index 1), default %u\n",
	       DEFAULT_SERVICE_PROFILE);
	printf("                            means no profile is set explicitly\n");
	printf("                            but default profile for the service is used\n");
	printf("-r, --running-avg-factor  strength of time domain filering\n");
	printf("                          (envelope and iq only, default service dependent)\n");
	printf("-s, --sensor              select sendor id, , default %d\n", DEFAULT_SENSOR);
	printf("-v, --verbose             set debug level to verbose\n");
}


bool parse_options(int argc, char *argv[], input_t *input)
{
	static struct option long_options[] =
	{
		{"service-type",       required_argument,  0, 't'},
		{"sweep-count",        required_argument,  0, 'c'},
		{"range-start",        required_argument,  0, 'b'},
		{"range-end",          required_argument,  0, 'e'},
		{"frequency",          required_argument,  0, 'f'},
		{"gain",               required_argument,  0, 'g'},
		{"number-of-bins",     required_argument,  0, 'n'},
		{"out",                required_argument,  0, 'o'},
		{"service-profile",    required_argument,  0, 'y'},
		{"running-avg-factor", required_argument,  0, 'r'},
		{"sensor",             required_argument,  0, 's'},
		{"verbose",            no_argument,        0, 'v'},
		{"help",               no_argument,        0, 'h'},
		{NULL,                 0,                  NULL, 0}
	};

	int16_t character_code;
	int32_t option_index = 0;

	while ((character_code = getopt_long(argc, argv, "t:c:b:e:f:g:n:o:r:s:vh?:y:", long_options, &option_index)) != -1)
	{
		switch (character_code)
		{
			case 't':
			{
				switch (atoi(optarg))
				{
					case 0:
					{
						input->service_type = POWER_BIN;
						break;
					}
					case 1:
					{
						input->service_type = ENVELOPE;
						break;
					}
					case 2:
					{
						input->service_type = IQ;
						break;
					}
					default:
						printf("Invalid service type.\n");
						print_usage();
						return false;
				}

				break;
			}
			case 'c':
			{
				input->update_count       = atoi(optarg);
				input->wait_for_interrupt = false;
				break;
			}
			case 'b':
			{
				input->start_m = strtof(optarg, NULL);
				break;
			}
			case 'e':
			{
				input->end_m = strtof(optarg, NULL);
				break;
			}
			case 'f':
			{
				float f = strtof(optarg, NULL);
				if (f > 0 && f < 100000)
				{
					input->frequency = f;
				}
				else
				{
					printf("Frequency out of range.\n");
					print_usage();
					exit(EXIT_FAILURE);
				}

				break;
			}
			case 'g':
			{
				float g = strtof(optarg, NULL);
				if (g >= 0 && g <= 1)
				{
					input->gain = g;
				}
				else
				{
					printf("Gain out of range.\n");
					print_usage();
					exit(EXIT_FAILURE);
				}

				break;
			}
			case 'n':
			{
				int n = atoi(optarg);
				if (n > 0 && n <= 32)
				{
					input->n_bins = n;
				}
				else
				{
					printf("Number of bins out of range.\n");
					print_usage();
					exit(EXIT_FAILURE);
				}

				break;
			}
			case 'y':
			{
				input->service_profile = atoi(optarg);
				break;
			}
			case 'o':
			{
				input->file_path = acc_os_mem_alloc(sizeof(char) * (strlen(optarg) + 1));
				if (input->file_path == NULL)
				{
					printf("Failed allocating memory\n");
					return false;
				}

				snprintf(input->file_path, strlen(optarg) + 1, "%s", optarg);
				break;
			}
			case 'r':
			{
				float r = strtof(optarg, NULL);
				if (r >= 0 && r <= 1)
				{
					input->running_avg = r;
				}
				else
				{
					printf("Running average factor out of range.\n");
					print_usage();
					exit(EXIT_FAILURE);
				}

				break;
			}
			case 's':
			{
				int s = atoi(optarg);
				if (s > 0 && s <= 4)
				{
					input->sensor = s;
				}
				else
				{
					printf("Sensor id out of range.\n");
					print_usage();
					exit(EXIT_FAILURE);
				}

				break;
			}
			case 'v':
			{
				input->log_level = ACC_LOG_LEVEL_VERBOSE;
				break;
			}
			case 'h':
			case '?':
			{
				print_usage();
				return false;
			}
		}
	}

	if (input->service_type == INVALID_SERVICE)
	{
		printf("Missing option service type.\n");
		print_usage();
		return false;
	}

	return true;
}


acc_service_configuration_t set_up_power_bin(input_t *input)
{
	acc_service_configuration_t power_bin_configuration = acc_service_power_bins_configuration_create();

	if (power_bin_configuration == NULL)
	{
		printf("acc_service_power_bin_configuration_create() failed\n");
		return NULL;
	}

	/*
	 * Numbering of service profiles starts at 1. Setting 0 means don't set profile explicitly
	 * but instead use the default for the service
	 */
	if (input->service_profile > 0)
	{
		acc_service_profile_set(power_bin_configuration, input->service_profile);
	}

	acc_service_power_bins_requested_bin_count_set(power_bin_configuration, input->n_bins);

	float length_m = input->end_m - input->start_m;

	acc_service_requested_start_set(power_bin_configuration, input->start_m);
	acc_service_requested_length_set(power_bin_configuration, length_m);
	acc_service_repetition_mode_streaming_set(power_bin_configuration, input->frequency);

	acc_service_sensor_set(power_bin_configuration, input->sensor);

	if (input->gain >= 0)
	{
		acc_service_receiver_gain_set(power_bin_configuration, input->gain);
	}

	return power_bin_configuration;
}


bool execute_power_bin(acc_service_configuration_t power_bin_configuration, char *file_path, bool wait_for_interrupt,
                       uint16_t update_count)
{
	acc_service_handle_t handle = acc_service_create(power_bin_configuration);

	if (handle == NULL)
	{
		printf("acc_service_create failed\n");
		return false;
	}

	acc_service_power_bins_metadata_t power_bins_metadata;
	acc_service_power_bins_get_metadata(handle, &power_bins_metadata);

	uint16_t power_bins_data[power_bins_metadata.bin_count];

	acc_service_power_bins_result_info_t result_info;
	bool                                 service_status = acc_service_activate(handle);

	if (service_status)
	{
		FILE *file = stdout;

		if (file_path != NULL)
		{
			file = fopen(file_path, "w");

			if (file == NULL)
			{
				printf("opening file failed\n");
				return false;
			}
		}

		uint16_t updates = 0;

		while ((wait_for_interrupt && interrupted == 0) || updates < update_count)
		{
			service_status = acc_service_power_bins_get_next(handle, power_bins_data, power_bins_metadata.bin_count, &result_info);

			if (service_status)
			{
				for (uint_fast16_t index = 0; index < power_bins_metadata.bin_count; index++)
				{
					fprintf(file, "%u\t", (unsigned int)power_bins_data[index]);
				}

				fprintf(file, "\n");

				if (file_path == NULL)
				{
					fflush(stdout);
				}
			}
			else
			{
				printf("Power bin data not properly retrieved\n");
				fflush(stdout);
				return false;
			}

			if (!wait_for_interrupt)
			{
				updates++;
			}
		}

		if (file_path != NULL)
		{
			fclose(file);
		}

		service_status = acc_service_deactivate(handle);
	}
	else
	{
		printf("acc_service_activate() failed\n");
	}

	acc_service_destroy(&handle);

	return service_status;
}


acc_service_configuration_t set_up_envelope(input_t *input)
{
	acc_service_configuration_t envelope_configuration = acc_service_envelope_configuration_create();

	if (envelope_configuration == NULL)
	{
		printf("acc_service_envelope_configuration_create() failed\n");
		return NULL;
	}

	/*
	 * Numbering of service profiles starts at 1. Setting 0 means don't set profile explicitly
	 * but instead use the default for the service
	 */
	if (input->service_profile > 0)
	{
		acc_service_profile_set(envelope_configuration, input->service_profile);
	}

	if (input->running_avg >= 0)
	{
		printf("using running avg: %" PRIfloat "\n", ACC_LOG_FLOAT_TO_INTEGER(input->running_avg));
		acc_service_envelope_running_average_factor_set(envelope_configuration, input->running_avg);
	}

	float length_m = input->end_m - input->start_m;

	acc_service_requested_start_set(envelope_configuration, input->start_m);
	acc_service_requested_length_set(envelope_configuration, length_m);
	acc_service_repetition_mode_streaming_set(envelope_configuration, input->frequency);

	if (input->gain >= 0)
	{
		acc_service_receiver_gain_set(envelope_configuration, input->gain);
	}

	return envelope_configuration;
}


bool execute_envelope(acc_service_configuration_t envelope_configuration, char *file_path, bool wait_for_interrupt,
                      uint16_t update_count)
{
	acc_service_handle_t handle = acc_service_create(envelope_configuration);

	if (handle == NULL)
	{
		printf("acc_Service_create failed\n");
		return false;
	}

	acc_service_envelope_metadata_t envelope_metadata;
	acc_service_envelope_get_metadata(handle, &envelope_metadata);

	uint16_t envelope_data[envelope_metadata.data_length];

	acc_service_envelope_result_info_t result_info;
	bool                               service_status = acc_service_activate(handle);

	if (service_status)
	{
		FILE *file = stdout;

		if (file_path != NULL)
		{
			file = fopen(file_path, "w");

			if (file == NULL)
			{
				printf("opening file failed\n");
				return false;
			}
		}

		uint16_t updates = 0;

		while ((wait_for_interrupt && interrupted == 0) || updates < update_count)
		{
			service_status = acc_service_envelope_get_next(handle, envelope_data, envelope_metadata.data_length, &result_info);

			if (service_status)
			{
				for (uint_fast16_t index = 0; index < envelope_metadata.data_length; index++)
				{
					fprintf(file, "%u\t", (unsigned int)(envelope_data[index] + 0.5f));
				}

				fprintf(file, "\n");

				if (file_path == NULL)
				{
					fflush(stdout);
				}
			}
			else
			{
				printf("Envelope data not properly retrieved\n");
				fflush(stdout);
				return false;
			}

			if (!wait_for_interrupt)
			{
				updates++;
			}
		}

		if (file_path != NULL)
		{
			fclose(file);
		}

		service_status = acc_service_deactivate(handle);
	}
	else
	{
		printf("acc_service_activate() failed\n");
	}

	acc_service_destroy(&handle);

	return service_status;
}


acc_service_configuration_t set_up_iq(input_t *input)
{
	acc_service_configuration_t iq_configuration = acc_service_iq_configuration_create();

	if (iq_configuration == NULL)
	{
		printf("acc_service_iq_configuration_create() failed\n");
		return NULL;
	}

	/*
	 * Numbering of service profiles starts at 1. Setting 0 means don't set profile explicitly
	 * but instead use the default for the service
	 */
	if (input->service_profile > 0)
	{
		acc_service_profile_set(iq_configuration, input->service_profile);
	}

	acc_service_iq_output_format_set(iq_configuration, ACC_SERVICE_IQ_OUTPUT_FORMAT_FLOAT_COMPLEX);

	float length_m = input->end_m - input->start_m;

	acc_service_requested_start_set(iq_configuration, input->start_m);
	acc_service_requested_length_set(iq_configuration, length_m);
	acc_service_repetition_mode_streaming_set(iq_configuration, input->frequency);

	if (input->gain >= 0)
	{
		acc_service_receiver_gain_set(iq_configuration, input->gain);
	}

	return iq_configuration;
}


bool execute_iq(acc_service_configuration_t iq_configuration, char *file_path, bool wait_for_interrupt, uint16_t update_count)
{
	acc_service_handle_t handle = acc_service_create(iq_configuration);

	if (handle == NULL)
	{
		printf("acc_service_create failed\n");
		return false;
	}

	acc_service_iq_metadata_t iq_metadata;
	acc_service_iq_get_metadata(handle, &iq_metadata);

	float complex                iq_data[iq_metadata.data_length];
	acc_service_iq_result_info_t result_info;

	bool service_status = acc_service_activate(handle);

	if (service_status)
	{
		FILE *file = stdout;

		if (file_path != NULL)
		{
			file = fopen(file_path, "w");

			if (file == NULL)
			{
				printf("opening file failed\n");
				return false;
			}
		}

		uint16_t updates = 0;

		while ((wait_for_interrupt && interrupted == 0) || updates < update_count)
		{
			service_status = acc_service_iq_get_next(handle, iq_data, iq_metadata.data_length, &result_info);

			if (service_status)
			{
				for (uint_fast16_t index = 0; index < iq_metadata.data_length; index++)
				{
					fprintf(file, "%" PRIfloat "\t%" PRIfloat "\t", ACC_LOG_FLOAT_TO_INTEGER(crealf(
															 iq_data[index])),
					        ACC_LOG_FLOAT_TO_INTEGER(cimagf(iq_data[index])));
				}

				fprintf(file, "\n");

				if (file_path == NULL)
				{
					fflush(stdout);
				}
			}
			else
			{
				printf("IQ data not properly retrieved\n");
				fflush(stdout);
				return false;
			}

			if (!wait_for_interrupt)
			{
				updates++;
			}
		}

		if (file_path != NULL)
		{
			fclose(file);
		}

		service_status = acc_service_deactivate(handle);
	}
	else
	{
		printf("acc_service_activate() failed\n");
	}

	acc_service_destroy(&handle);

	return service_status;
}
