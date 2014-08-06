#include <sys/time.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <string.h>
#include <assert.h>


#include "litmus.h"
#include "common.h"

static void usage(char *error) {
	fprintf(stderr, "Error: %s\n", error);
	fprintf(stderr,
		"Usage of rtspin:\n"
		"	rt_spin [COMMON-OPTS] WCET PERIOD DURATION\n"
		"	rt_spin [COMMON-OPTS] -f FILE [-o COLUMN] WCET PERIOD\n"
		"	rt_spin -l\n"
		"\n"
		"COMMON-OPTS = [-w] [-s SCALE]\n"
		"              [-p PARTITION/CLUSTER [-z CLUSTER SIZE]] [-c CLASS]\n"
		"              [-X LOCKING-PROTOCOL] [-L CRITICAL SECTION LENGTH] [-Q RESOURCE-ID]"
		"\n"
		"Usage for Mixed-criticality systems:\n"
			"rt_spin -m NO_OF_LEVELS -n CRITICALITY_LEVEL_OF_TASK WCET1 WCET2 .. PERIOD DURATION\n"
		"NOTE:\n"
		"WCET values to be in increasing order."
		"\n"
		"WCET and PERIOD are milliseconds, DURATION is seconds.\n"
		"CRITICAL SECTION LENGTH is in milliseconds.\n");
	exit(EXIT_FAILURE);
}


#define NUMS 4096
static int num[NUMS];
static char* progname;

static int loop_once(void)
{
	int i, j = 0;
	for (i = 0; i < NUMS; i++)
		j += num[i]++;
	return j;
}

static int loop_for(double exec_time, double emergency_exit)
{
	double last_loop = 0, loop_start;
	int tmp = 0;

	double start = cputime();
	double now = cputime();

	while (now + last_loop < start + exec_time) {
		loop_start = now;
		tmp += loop_once();
		now = cputime();
		last_loop = now - loop_start;
		if (emergency_exit && wctime() > emergency_exit) {
			/* Oops --- this should only be possible if the execution time tracking
			 * is broken in the LITMUS^RT kernel. */
			fprintf(stderr, "!!! rtspin/%d emergency exit!\n", getpid());
			fprintf(stderr, "Something is seriously wrong! Do not ignore this.\n");
			break;
		}
	}

	return tmp;
}

void func(int num_values, double* wcet_ptr, double program_end)
{
		int temp = 0;
		int flag=0;
		int loop_index; 
		srand(getpid());
		front:	while(wctime()<program_end)
			{
				for(loop_index=0;loop_index<num_values;loop_index++)
				{
					if(loop_index==temp)
					{
						loop_for(*(wcet_ptr+loop_index),program_end+1);
						sleep_next_period();
						if(wctime() > (program_end/2) && flag==0)
						{
						redo_rand: 	temp = rand() % num_values;
								if(temp==0)
									goto redo_rand;
								flag=1;
								goto front;
						}
					}	
				}
			}
}	



/* Add options for MC systems */
#define OPTSTR "p:m:n:c:wlveo:f:v:s:q:X:L:Q:"
int main(int argc, char** argv)
{
	int ret;
	lt_t wcet=0;
	lt_t period=0;
	double wcet_ms=0, period_ms=0;
	unsigned int priority = LITMUS_LOWEST_PRIORITY;
	int migrate = 0;
	int cluster = 0;
	int opt;
	int wait = 0;
	
	
	const char *file = NULL;
	//No more needed
	//int want_enforcement = 0;
	double duration = 0, start = 0;
	
	
	task_class_t class = RT_CLASS_HARD;
	
	struct rt_task param;
	/* MC variable. Default is 0. Has to be set by user by using -m option */
	int mc_task = 0;
	/*Total number of levels in the task set*/
	int num_of_levels = 0; 
	int criticality_level = 0;
	/*Number of WCET values*/
	int num_values = 0;
	
	/*Pointer to store the integer values */	
	unsigned long long *ptr=NULL;
	int loop_index=0;
	int temp_index=0;	
	//Read lambda values for virtual deadlines from a file
	float* lambda_ptr=NULL;	
	unsigned long long *vd_ptr=NULL;
	//const char* file_name=NULL;	
	
	//To store the WCET in seconds
	double* wcet_ptr=NULL;

	

	progname = argv[0];

	while ((opt = getopt(argc, argv, OPTSTR)) != -1) {
		switch (opt) {
		case 'w':
			wait = 1;
			break;
		case 'p':
			cluster = atoi(optarg);
			migrate = 1;
			break;
		case 'q':
			priority = atoi(optarg);

			if (!litmus_is_valid_fixed_prio(priority))
				usage("Invalid priority.");
			break;
		/* MC task system */
		case 'm':
			num_of_levels = atoi(optarg);
			mc_task = 1;
			break;
		case 'n':
			criticality_level = atoi(optarg);
			break;
		//Introduced this to get lambda values.. Not using now.
		//case 'v':
		//	file_name = optarg;
		//	break;
		case 'c':
			class = str2class(optarg);
			if (class == -1)
				usage("Unknown task class.");
			break;
		//Always enabled for MC tasks
		//case 'e':
			//want_enforcement = 1;
			//break;
		
		case ':':
			usage("Argument missing.");
			break;
		case '?':
		default:
			usage("Bad argument.");
			break;
		}
	}
	 
	if ((argc - optind < 3) && (!mc_task))
		usage("Arguments missing.");
	
	
	/* This is only for non MC systems*/
	if (!mc_task)
	{
		wcet_ms   = atof(argv[optind + 0]);
		period_ms = atof(argv[optind + 1]);

		wcet   = ms2ns(wcet_ms);
		period = ms2ns(period_ms);
		if (wcet <= 0)
			usage("The worst-case execution time must be a "
					"positive number.");
		if (period <= 0)
			usage("The period must be a positive number.");
		if (!file && wcet > period) {
			usage("The worst-case execution time must not "
					"exceed the period.");
		}
	}

	if (!file && !mc_task )
		duration  = atof(argv[optind + 2]);

	if (migrate) {
		ret = be_migrate_to_domain(cluster);
		if (ret < 0)
			bail_out("could not migrate to target partition or cluster.");
	}

	/* Parameters parsing for MC task systems is done here
	* No of WCET values = num_of_levels - criticality_level + 1
	* The number of arguments should be (No of WCET values + 1 + 1) for period and duration 
	* 
	*/
	if(mc_task)
	{
				
		num_values = num_of_levels - criticality_level +1;
		if (argc - optind < (num_values + 2))
			usage("Arguments missing.");
		//Allocate memory for ptr containing WCET values		
		ptr = (unsigned long long*) malloc(sizeof(unsigned long long)*num_values);
		//Get them		
		for(loop_index=0;loop_index<num_values;loop_index++)
		{
			*(ptr+loop_index)= atoi(argv[optind+loop_index]);
		}
	
		
		//File input for lambda is not working right now. Hard code the 
		//values for time being
		lambda_ptr = (float*) malloc(sizeof(float)*(num_of_levels));
		*(lambda_ptr+0)= 0.000000; //always 0	
		*(lambda_ptr+1)= 0.220000;
		*(lambda_ptr+2)= 0.940000;	
		//*(lambda_ptr+3)= 0.500000;
		
		/*Do 1-lambda*/
		*(lambda_ptr+0) = 1 - *(lambda_ptr+0); 			
		*(lambda_ptr+1) = 1 - *(lambda_ptr+1); 		
		*(lambda_ptr+2) = 1 - *(lambda_ptr+2); 			
		//*(lambda_ptr+3) = 1 - *(lambda_ptr+3); 		
		
		wcet_ms   = *(ptr+0); //Should be set to the first node in the linked list.
		period_ms = atof(argv[optind + num_values]);;
		duration  = atof(argv[optind + num_values + 1]);
		
		wcet   = ms2ns(wcet_ms);
		period = ms2ns(period_ms);
		
		//Now convert all WCET values to ns
		for(loop_index=0;loop_index<num_values;loop_index++)
		{
			*(ptr+loop_index)= (*(ptr+loop_index))*1000000LL;
		}
		

		//wcet_ptr holds the list of wcet values in seconds 
		wcet_ptr = (double*) malloc(sizeof(double)*num_values);
		//convert the wcet values to seconds
		for(loop_index=0;loop_index<num_values;loop_index++)
			*(wcet_ptr+loop_index)=(*(ptr+loop_index))*0.000000001;





		//Compute virtual deadlines
		vd_ptr = (unsigned long long*) malloc (sizeof(unsigned long long)*(num_values));
		
		*(vd_ptr+0) = *(lambda_ptr+0)*period;
		//Dont hardcode this.. 	
		for(loop_index=1;loop_index<(num_values);loop_index++)
		{
			//Period is in ns and period_ms is in ms..
			//Now vd is in ns..
			temp_index = loop_index-1;
			*(vd_ptr+loop_index) = *(lambda_ptr+loop_index)*(*(vd_ptr+temp_index));
		}
		
		/*
		//For debugguing purposes. Remove this later
		for(loop_index=0;loop_index<num_values;loop_index++)
		{	
			*(vd_ptr+loop_index)= 0;
		}
		*/

		//Sanity checks
		if (wcet <= 0)
			usage("The worst-case execution time must be a "
					"positive number.");
		if (period <= 0)
			usage("The period must be a positive number.");
		if (wcet > period) {
			usage("The worst-case execution time must not "
					"exceed the period.");
		}
	
	}
	

	init_rt_task_param(&param);
	param.exec_cost = wcet;
	param.period = period;
	param.priority = priority;
	param.cls = class;
	param.budget_policy = PRECISE_ENFORCEMENT;
	
	//Default policy is sporadic.. Change to periodic..
	param.release_policy = TASK_PERIODIC;

	if (migrate)
		param.cpu = domain_to_first_cpu(cluster);
	
	ret = set_rt_task_param(gettid(), &param);
	if (ret < 0)
		bail_out("could not setup rt task params");
	
	/*Passing the system criticality indicator, task criticality, multiple wcet values and virtual 		 * deadlines 
	 */
	if(mc_task)
	{
		ret = set_sys_cl(gettid(), &num_of_levels, &criticality_level);
		if (ret != 0)
			bail_out("could not set system criticality indicator");
	
		ret = set_wcet_val(gettid(), ptr, vd_ptr, &num_values);
		if (ret != 0)
			bail_out("could not set wcet values");
	}
	
	init_litmus();

	ret = task_mode(LITMUS_RT_TASK);
	if (ret != 0)
		bail_out("could not become RT task");

	if (wait) {
		ret = wait_for_ts_release();
		if (ret != 0)
			bail_out("wait_for_ts_release()");
	}

	start = wctime();
	//Need to include MC logic..
	func(num_values, wcet_ptr, start+duration);


	ret = task_mode(BACKGROUND_TASK);
	if (ret != 0)
		bail_out("could not become regular task (huh?)");

	//Free the memory
	if(mc_task)
	{
		free(ptr);
		free(lambda_ptr);
		free(vd_ptr);
		free(wcet_ptr);
	}

	return 0;
}
