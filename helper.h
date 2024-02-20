#ifndef _HELPER_H_
#define _HELPER_H_


typedef struct job {
    pid_t pid;
    char input_str[1024];
    struct job *next;
} job_t;

void free_memory(char **input_arr, int input_arr_len);

int get_arr_len(char **input_arr);

char** process_input_command(int *input_arr_len, char **input_str);

void process_command_path(char **input_arr, char **input_command_path);

void signal_handler(int sig __attribute__((unused)));

job_t* remove_suspended_job(job_t *sus_jobs_list, int index, pid_t *resume_pid, char **input_str);

void print_suspended_jobs(job_t *sus_jobs_list);

int get_sus_jobs_count(job_t *sus_jobs_list);

int check_and_execute_builtin_command(char **input_arr, int input_arr_len, job_t *sus_jobs_list);

int validate_pipes(char **input_arr);

int validate_and_process_output_redirects(char **input_arr);

int validate_and_process_input_redirects(char **input_arr);

job_t* add_suspended_job(char* input_str, pid_t pid, job_t *sus_jobs_list);

void sigstop_handler(int signum __attribute__((unused)));

int find_pipes(char **input_arr);

int get_pipe_index_and_commands(char **input_arr);

char** get_pipe_command_1(char **input_arr, int pipe_index);

char** get_pipe_command_2(char **input_arr, int pipe_index);

#endif
