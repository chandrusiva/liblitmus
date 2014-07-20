#include <stdio.h>
#include <stdlib.h>

static int skip_to_next_line(FILE *fstream)
{
	int ch;
	for (ch = fgetc(fstream); ch != EOF && ch != '\n'; ch = fgetc(fstream));
	return ch;
}
static int check_next(FILE *fstream)
{
	int ch;
	int retval=1;
	ch=fgetc(fstream);
	if(ch=='\n')
		retval=0;
	ungetc(ch,fstream);
	return retval;	
}
int main()
{
	//Declarations
	FILE *fp;
	//temporary variables
	int temp_fp,temp_first;
	int temp,temp1,level;
	int max_times=1;
	float common,sum;
	int retval;
	//task system variables
	int max_sys_cl;
	int num_tasks;
	//array containing the num of tasks in each criti level
	int *num_tasks_in_level;
	//for manipulating loops
	int loop_index,i,j,k;
	//2-D array containing WCET values and period of task at each level
	int** array;	
	//2-D array to store the utilaztion of tasks at each level	
	float** util;
	//Array containing the lambda values
	float* lambda;
	
	fp=fopen("input.txt","r");
	
	retval = fscanf(fp, "%d",&num_tasks);	
	if(!retval)
		fprintf(stderr, "Error in file read");

	printf("No of tasks = %d\n",num_tasks);
	skip_to_next_line(fp);
	
	retval = fscanf(fp, "%d",&max_sys_cl);	
	if(!retval)
		fprintf(stderr, "Error in file read");
	num_tasks_in_level = (int*) calloc(max_sys_cl,sizeof(int));
	printf("Max citicality level = %d\n",max_sys_cl);
	skip_to_next_line(fp);
	
	for(loop_index=0;loop_index<num_tasks;loop_index++)
	{
		retval = fscanf(fp, "%d",&temp_fp);	
		if(!retval)
			fprintf(stderr, "Error in file read");
		num_tasks_in_level[temp_fp-1]++;
	}	
	skip_to_next_line(fp);
	
	printf("\n");
	printf("**Printing num_tasks_in_level array**\n");
	
	//And find max in the array
	for(loop_index=0;loop_index<max_sys_cl;loop_index++)
	{
		if(num_tasks_in_level[loop_index]>max_times)
			max_times = num_tasks_in_level[loop_index];
		printf("%d\n",num_tasks_in_level[loop_index]);
	}	
	
	
	printf("\n");
	printf("Max of %d tasks in a level\n", max_times);

	array = calloc(max_sys_cl,sizeof(int *));
	for(loop_index = 0; loop_index < max_sys_cl; loop_index++)
		array[loop_index] = calloc((max_sys_cl+1)*(max_times),sizeof(int));
	
	i=0;
	j=0;
	for(loop_index=0;loop_index<max_sys_cl;loop_index++)
	{	
		
		retval = fscanf(fp, "%d",&temp_first);	
		if(!retval)
			fprintf(stderr, "Error in file read");
		temp_fp=temp_first;
		if(temp_first==0)
			goto out;	
		do{
			array[i][j]=temp_fp;
			j++;	
			retval = fscanf(fp, "%d",&temp_fp);	
			if(!retval)
				fprintf(stderr, "Error in file read");
			
		}while(check_next(fp));	
		
		//to assign the last element
		array[i][j]=temp_fp;
			
		out:
			i++;
			j=0;
			skip_to_next_line(fp);
	}
	
	fclose(fp);
	
	printf("\n");

	//print the array holding the wcet and period	
	printf("**Printing wcet and period array**\n");
	for (i = 0; i < max_sys_cl; i++) 
	{
        	for (j = 0; j < ((max_sys_cl+1)*max_times); j++) 
		{
            		printf("%d ", array[i][j]);
        	}
        	printf("\n");
    	}
	
	//Allocate memory for the utilization array	
	util = calloc(max_sys_cl, sizeof(float *));
	for(loop_index = 0; loop_index < max_sys_cl; loop_index++)
		util[loop_index] = calloc(max_sys_cl,sizeof(float));
	
	//print the 2-D array
	/*
	for (i = 0; i < max_sys_cl; i++) 
	{
        	for (j = 0; j < max_sys_cl; j++) 
		{
            		util[i][j]=0;
			printf("%f ", util[i][j]);
        	}
        	printf("\n");
    	}
	*/
	
	//Compute utilization values
	for(i=0;i<max_sys_cl;i++)
	{
		
		for(k=0;k<=i;k++)
		{	
			temp=0;
			printf("temp value = %d\n",temp);	
			while(temp!=num_tasks_in_level[i])
			{
				temp1=temp*(max_sys_cl+1)+k;
				util[i][k] = util[i][k] + (array[i][temp1] /
						((float)array[i][temp1+max_sys_cl-k]));
				temp++;
				//printf("temp value = %d\n",temp);	
				printf("temp1 value = %d\n",temp1);
				printf("i=%d\n",i);
				printf("k=%d\n",k);
				printf("WCET value = %d\n", array[i][temp1]);	
				printf("Period = %d\n", array[i][temp1+max_sys_cl-k]);
				printf("util = %f\n", util[i][k]);
			}
		}	
	}
	
	
	printf("\n");
	
	//print the array holding the utilization values
	printf("**Printing utlization array**\n");
	for (i = 0; i < max_sys_cl; i++) 
	{
        	for (j = 0; j < max_sys_cl; j++) 
		{
            		printf("%f ", util[i][j]);
        	}
        	printf("\n");
    	}
	
	
	//Allocate space for lambda array
	lambda = (float*) calloc(max_sys_cl,sizeof(float));

	/*	
	printf("**Allocating lambda array**\n");
	for(loop_index=0;loop_index< max_sys_cl;loop_index++)
		printf("%f\n", lambda[loop_index]);		
	*/

	for(level=2;level<= max_sys_cl;level++)
	{
		common=1;
		sum=0;	
		printf("Level = %d\n",level);
		for(loop_index=level-1;loop_index>=1;loop_index--)
			common = common * (1-lambda[loop_index-1]);
		printf("Common = %f\n",common);
		for(i=level;i<=max_sys_cl;i++)
			sum = sum + util[i-1][level-2];	
		printf("Sum = %f\n",sum);
		lambda[level-1] = sum / (common * (1-(util[level-2][level-2]/common)));
		printf("lambda value = %f\n",lambda[level-1]);
	}	
	
	printf("\n");
	
	//print the lambda array	
	printf("**Printing lambda array**\n");
	for(loop_index=0;loop_index< max_sys_cl;loop_index++)
		printf("%f\n", lambda[loop_index]);		
	
	//Save the lambda values in a file	
	fp=fopen("bin/lambda.txt","w+");	
	for(loop_index=0;loop_index< max_sys_cl;loop_index++)
		fprintf(fp,"%f ",lambda[loop_index]);
	fclose(fp);

	return 0;
}
