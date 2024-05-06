#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/wait.h>

int main(int argc, char* argv[])
{
	if (argc != 2) {
		fprintf(stderr, "Usage: %s <directory>\n", argv[0]);
		return 1;
	}

	char* filename = argv[1];
 
    struct stat st;
    if (stat(filename, &st) == -1) {
        fprintf(stderr, "Unable to execute\n");
        return 1;
    }

	if (!S_ISDIR(st.st_mode)){
		printf("%ld\n", st.st_size);
		return 0;
	}

    off_t total_size = st.st_size;
	struct dirent * entry;
 	DIR *directory;
	directory = opendir(argv[1]);
	if (directory == NULL) {
        fprintf(stderr, "Unable to execute\n");
        return 1;
    }

	while ((entry = readdir(directory)) != NULL) {
        char full_name[1024];
        snprintf(full_name, sizeof(full_name), "%s/%s", argv[1], entry->d_name);

        if (entry->d_type == DT_DIR) {
            if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {     //skipping these directories
                int pipefd[2];
				if (pipe(pipefd) == -1) {
					fprintf(stderr, "Unable to execute\n");
					continue;
				}

				pid_t child_pid = fork();

				if (child_pid == -1) {
					fprintf(stderr, "Unable to execute\n");
					continue;
				}

				//redirecting output of child process to parent process through pipe
				if (child_pid == 0) {
					close(pipefd[0]);
					dup2(pipefd[1], STDOUT_FILENO);
					close(pipefd[1]);

					char* newargv[] = {"./myDU", full_name, (char *)NULL};
					execvp("./myDU", newargv);  //calling exec myDU again for subdirectories
					fprintf(stderr, "Unable to execute\n");
					exit(1);
				} else {
					close(pipefd[1]);
					char buffer[32];
					ssize_t bytesRead;
					off_t subdirectory_size = 0;
					while ((bytesRead = read(pipefd[0], buffer, sizeof(buffer))) > 0) {   //reading the value given by the child process
						buffer[bytesRead] = '\0';
						subdirectory_size += atoll(buffer);
					}

					if (bytesRead == -1) {
						fprintf(stderr, "Unable to execute\n");
						// Handle read error here if needed
					} else {
						total_size += subdirectory_size;
					}
					close(pipefd[0]);
				}
            }
        } 
		else {
			struct stat st_temp;
			if (stat(full_name, &st_temp) == -1) {
				fprintf(stderr, "Unable to execute\n");
				// Handle stat error here if needed
			} else {
				off_t size_temp = st_temp.st_size;
				total_size += size_temp;     //adding to total_size if it is a file
			}
        }
    }
    printf("%ld\n", total_size);
	closedir(directory);
	return 0;
}
