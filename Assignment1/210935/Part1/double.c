#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <unistd.h>
int main(int argc,char *argv[])
{
	if(argc <2) {
		fprintf(stderr, "“Unable to execute” \n");
		return 1;
	}
	int num=atoi(argv[argc-1]);
	int result =2*num;

	if(argc==2){
		printf("%d\n",result);
		return 0;
	}
	else{
		char str[20];
		sprintf(str, "%d", result);	   //changing from numeric to string
		char** newargv=(char**)malloc(sizeof(char*)*argc);
		for(int i=0;i<argc-2;i++){
			newargv[i]=argv[i+1];
		}
		newargv[argc-2]=str;     //replacing the numeric argument to further the execution
		newargv[argc-1]=NULL;    //adding null to support the execution
		char* newname=(char*)malloc(sizeof(char)*8);
		newname[0]='.';    //concatenating "./" to the file name
		newname[1]='/';
		for(int i=0;i<6;i++){
			newname[i+2]=newargv[0][i];    //updating filename
		}
		newargv[0]=newname;
		if(execvp(newargv[0],newargv)<0){
			perror("Unable to execute\n");
		}
	}
	return 0;
}
