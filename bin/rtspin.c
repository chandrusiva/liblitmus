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

/*
 * returns the character that made processing stop, newline or EOF
 */
static int skip_to_next_line(FILE *fstream)
{
	int ch;
	for (ch = fgetc(fstream); ch != EOF && ch != '\n'; ch = fgetc(fstream));
	return ch;
}

static void skip_comments(FILE *fstream)
{
	int ch;
	for (ch = fgetc(fstream); ch == '#'; ch = fgetc(fstream))
		skip_to_next_line(fstream);
	ungetc(ch, fstream);
}

static void get_exec_times(const char *file, const int column,
			   int *num_jobs,    double **exec_times)
{
	FILE *fstream;
	int  cur_job, cur_col, ch;
	*num_jobs = 0;

	fstream = fopen(file, "r");
	if (!fstream)
		bail_out("could not open execution time file");

	/* figure out the number of jobs */
	do {
		skip_comments(fstream);
		ch = skip_to_next_line(fstream);
		if (ch != EOF)
			++(*num_jobs);
	} while (ch != EOF);

	if (-1 == fseek(fstream, 0L, SEEK_SET))
		bail_out("rewinding file failed");

	/* allocate space for exec times */
	*exec_times = calloc(*num_jobs, sizeof(*exec_times));
	if (!*exec_times)
		bail_out("couldn't allocate memory");

	for (cur_job = 0; cur_job < *num_jobs && !feof(fstream); ++cur_job) {

		skip_comments(fstream);

		for (cur_col = 1; cur_col < column; ++cur_col) {
			/* discard input until we get to the column we want */
			int unused __attribute__ ((unused)) = fscanf(fstream, "%*s,");
		}

		/* get the desired exec. time */
		if (1 != fscanf(fstream, "%lf", (*exec_times)+cur_job)) {
			fprintf(stderr, "invalid execution time near line %d\n",
					cur_job);
			exit(EXIT_FAILURE);
		}

		skip_to_next_line(fstream);
	}

	assert(cur_job == *num_jobs);
	fclose(fstream);
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


static void debug_delay_loop(void)
{
	double start, end, delay;

	while (1) {
		for (delay = 0.5; delay > 0.01; delay -= 0.01) {
			start = wctime();
			loop_for(delay, 0);
			end = wctime();
			printf("%6.4fs: looped for %10.8fs, delta=%11.8fs, error=%7.4f%%\n",
			       delay,
			       end - start,
			       end - start - delay,
			       100 * (end - start - delay) / delay);
		}
	}
}

static int job(double exec_time, double program_end, int lock_od, double cs_length)
{
	double chunk1, chunk2;

	if (wctime() > program_end)
		return 0;
	else {
		if (lock_od >= 0) {
			/* simulate critical section somewhere in the middle */
			chunk1 = drand48() * (exec_time - cs_length);
			chunk2 = exec_time - cs_length - chunk1;

			/* non-critical section */
			loop_for(chunk1, program_end + 1);

			/* critical section */
			litmus_lock(lock_od);
			loop_for(cs_length, program_end + 1);
			litmus_unlock(lock_od);

			/* non-critical section */
			loop_for(chunk2, program_end + 2);
		} else {
			loop_for(exec_time, program_end + 1);
		}
		sleep_next_period();
		return 1;
	}
}
/* Add options for MC systems */
#define OPTSTR "p:m:n:c:wlveo:f:s:q:X:L:Q:"
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
	int test_loop = 0;
	int column = 1;
	const char *file = NULL;
	int want_enforcement = 0;
	double duration = 0, start = 0;
	double *exec_times = NULL;
	double scale = 1.0;
	task_class_t class = RT_CLASS_HARD;
	int cur_job = 0, num_jobs = 0;
	struct rt_task param;
	/* MC variable. Default is 0. Has to be set by user by using -m option */
	int mc_task = 0;
	/*Total number of levels in the task set*/
	int num_of_levels = 0; 
	int criticality_level = 0;
	/*Number of WCET values*/
	int num_values = 0;
	
	/*For traversing the list */
	struct exec_times *temp;
	int loop_index=0;
	/*this is the list of all WCET values for a task */
	struct exec_times mylist; 
		
	/* locking */
	int lock_od = -1;
	int resource_id = 0;
	const char *lock_namespace = "./rtspin-locks";
	int protocol = -1;
	double cs_length = 1; /* millisecond */

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
		case 'c':
			class = str2class(optarg);
			if (class == -1)
				usage("Unknown task class.");
			break;
		case 'e':
			want_enforcement = 1;
			break;
		case 'l':
			test_loop = 1;
			break;
		case 'o':
			column = atoi(optarg);
			break;
		case 'f':
			file = optarg;
			break;
		case 's':
			scale = atof(optarg);
			break;
		case 'X':
			protocol = lock_protocol_for_name(optarg);
			if (protocol < 0)
				usage("Unknown locking protocol specified.");
			break;
		case 'L':
			cs_length = atof(optarg);
			if (cs_length <= 0)
				usage("Invalid critical section length.");
			break;
		case 'Q':
			resource_id = atoi(optarg);
			if (resource_id <= 0 && strcmp(optarg, "0"))
				usage("Invalid resource ID.");
			break;
		case ':':
			usage("Argument missing.");
			break;
		case '?':
		default:
			usage("Bad argument.");
			break;
		}
	}

	if (test_loop) {
		debug_delay_loop();
		return 0;
	}

	srand(getpid());

	if (file) {
		get_exec_times(file, column, &num_jobs, &exec_times);

		if (argc - optind < 2)
			usage("Arguments missing.");

		for (cur_job = 0; cur_job < num_jobs; ++cur_job) {
			/* convert the execution time to seconds */
			duration += exec_times[cur_job] * 0.001;
		}
	} else {
		/*
		 * if we're not reading from the CSV file, then we need
		 * three parameters
		 * And it should not be a MC task system
		 */
		 
		if ((argc - optind < 3) && (!mc_task))
			usage("Arguments missing.");
	}
	
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
	else if (file && !mc_task && num_jobs > 1)
		duration += period_ms * 0.001 * (num_jobs - 1);

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
		
		/* Linked list implementation*/
		/* Initialize the list head */
		
		INIT_LIST_HEAD(&mylist.list);
		
		
		for(loop_index=0;loop_index<num_values;loop_index++)
		{
			temp = (struct exec_times *)malloc(sizeof(struct exec_times));
			temp->wcet_val = atof(argv[optind + loop_index]);
			list_add_tail(&temp->list,&mylist.list);
		}
		
		
		wcet_ms   = atof(argv[optind + 0]); //Should be set to the first node in the linked list.
		period_ms = atof(argv[optind + num_values]);;
		duration  = atof(argv[optind + num_values + 1]);
		wcet   = ms2ns(wcet_ms);
		period = ms2ns(period_ms);
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
	param.budget_policy = (want_enforcement) ?
			PRECISE_ENFORCEMENT : NO_ENFORCEMENT;
	if (migrate)
		param.cpu = domain_to_first_cpu(cluster);
	ret = set_rt_task_param(gettid(), &param);
	if (ret < 0)
		bail_out("could not setup rt task params");

	init_litmus();

	ret = task_mode(LITMUS_RT_TASK);
	if (ret != 0)
		bail_out("could not become RT task");

	if (protocol >= 0) {
		/* open reference to semaphore */
		lock_od = litmus_open_lock(protocol, resource_id, lock_namespace, &cluster);
		if (lock_od < 0) {
			perror("litmus_open_lock");
			usage("Could not open lock.");
		}
	}

	if (wait) {
		ret = wait_for_ts_release();
		if (ret != 0)
			bail_out("wait_for_ts_release()");
	}

	start = wctime();

	if (file) {
		/* use times read from the CSV file */
		for (cur_job = 0; cur_job < num_jobs; ++cur_job) {
			/* convert job's length to seconds */
			job(exec_times[cur_job] * 0.001 * scale,
			    start + duration,
			    lock_od, cs_length * 0.001);
		}
	} else {
		/* convert to seconds and scale */
		while (job(wcet_ms * 0.001 * scale, start + duration,
			   lock_od, cs_length * 0.001));
	}

	ret = task_mode(BACKGROUND_TASK);
	if (ret != 0)
		bail_out("could not become regular task (huh?)");

	if (file)
		free(exec_times);

	return 0;
}
