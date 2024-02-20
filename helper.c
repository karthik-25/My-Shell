#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <libgen.h>
#include <limits.h>
#include <fcntl.h>
#include <sys/wait.h>

#include "helper.h"


void free_memory(char **input_arr, int input_arr_len){
  for (int j = 0; j < input_arr_len; j++) {
    free(input_arr[j]);
  }
  free(input_arr);
}

int get_arr_len(char **input_arr){
  int i=0;
  while (input_arr[i] != NULL){
    i++;
  }
  return i;
}

char** process_input_command(int *input_arr_len, char **input_str){

  char *input = NULL;
  size_t input_size = 0;
  ssize_t char_read_count;
  char *token, *saveptr;

  char_read_count = getline(&input, &input_size, stdin);

  // Check if getline fails
  if (char_read_count == -1) {
      exit(0);
  }

  strcpy(*input_str, input);
  char *newline_ptr = strchr(*input_str, '\n');
  if (newline_ptr != NULL) {
    *newline_ptr = '\0';
  }

  // Allocate memory for input - will be stored in array
  char **input_arr = (char **)malloc(sizeof(char *) * (char_read_count + 1));

  // Tokenize input, store each token in array
  int i = 0;
  for (token = strtok_r(input, " \n", &saveptr); token != NULL; token = strtok_r(NULL, " \n", &saveptr)) {
      input_arr[i] = malloc((strlen(token) + 1) * sizeof(char));
      strcpy(input_arr[i], token);
      i++;
  }
  *input_arr_len = i;
  input_arr[i] = NULL;
  
  free(input);

  return input_arr;
}

void process_command_path(char **input_arr, char **input_command_path){

  char *input_command = input_arr[0];

  if (input_command[0] != '/' && input_command[0] != '.' && input_command[0] != '|'){
    
    char *new_input_command = NULL;

    if (strchr(input_command, '/') != NULL) {
      new_input_command = (char *)malloc(strlen(input_command)+3);
      strcpy(new_input_command, "./");
      strncat(new_input_command, input_command, strlen(input_command));
      new_input_command[strlen(new_input_command)] = '\0';
    }
    else {
      char *default_dir = "/usr/bin/";
      new_input_command = (char *)malloc(strlen(input_command)+strlen(default_dir)+1);
      strcpy(new_input_command, default_dir);
      strncat(new_input_command, input_command, strlen(input_command));
      new_input_command[strlen(new_input_command)] = '\0';
    }
    strcpy(*input_command_path, new_input_command);
    // input_arr[0] = realloc(input_arr[0], strlen(new_input_command)+1);
    // strcpy(input_arr[0], new_input_command);

    free(new_input_command);

  }
  else{
    strcpy(*input_command_path, input_arr[0]);
  }
}

void signal_handler(int sig __attribute__((unused))){}

job_t* remove_suspended_job(job_t *sus_jobs_list, int index, pid_t *resume_pid, char **input_str) {
    job_t* curr_job = sus_jobs_list;
    job_t* prev_job = NULL;
    int i = 1;
    while (curr_job != NULL) {
        if (i == index) {
          *resume_pid = curr_job->pid;
          strcpy(*input_str, curr_job->input_str);
          if (prev_job == NULL) {
            if (curr_job->next == NULL){
              sus_jobs_list = NULL;
              break;
            }
            else {
              sus_jobs_list = curr_job->next;
            }
          } else {
            if (curr_job->next == NULL){
              prev_job->next = NULL;
            }
            else {
              prev_job->next = curr_job->next;
            }
          }
          free(curr_job);
          break;
        }
        prev_job = curr_job;
        curr_job = curr_job->next;
        i++;
    }

    return sus_jobs_list;
}

void print_suspended_jobs(job_t *sus_jobs_list) {
    int i = 1;
    job_t* curr_job = sus_jobs_list;
    while (curr_job != NULL) {
        printf("[%d] %s\n", i, curr_job->input_str);
        curr_job = curr_job->next;
        i++;
    }
}

int get_sus_jobs_count(job_t *sus_jobs_list){
  int i = 0;
  job_t* curr_job = sus_jobs_list;
  while (curr_job != NULL) {
    i++;
    curr_job = curr_job->next;
  }
  return i;
}

int check_and_execute_builtin_command(char **input_arr, int input_arr_len, job_t *sus_jobs_list){
  if (strcmp(input_arr[0], "cd") == 0){
    if (input_arr_len != 2){
      fprintf(stderr, "Error: invalid command\n");
    }
    else {
      int status = chdir(input_arr[1]);
      if (status != 0){
        fprintf(stderr, "Error: invalid directory\n");
      }
    }
    return 1;
  }
  if (strcmp(input_arr[0], "exit") == 0){
    if (input_arr_len != 1){
      fprintf(stderr, "Error: invalid command\n");
      return 1;
    }
    else {
      if (get_sus_jobs_count(sus_jobs_list) > 0){
        fprintf(stderr, "Error: there are suspended jobs\n");
        return 1;
      }
      else {
        // free memory
        free_memory(input_arr, input_arr_len);
        exit(0);
      }
    }
  }
  if (strcmp(input_arr[0], "jobs") == 0){
    if (input_arr_len != 1){
      fprintf(stderr, "Error: invalid command\n");
    }
    else {
      print_suspended_jobs(sus_jobs_list);
    }
    return 1;
  }
  if (strcmp(input_arr[0], "fg") == 0){
    if (input_arr_len != 2){
      fprintf(stderr, "Error: invalid command\n");
    }
    else {
      if (atoi(input_arr[1]) < 1 ||  atoi(input_arr[1]) > get_sus_jobs_count(sus_jobs_list)){
        fprintf(stderr, "Error: invalid job\n");
      }
      else{
        return 2;
      }
    }
    return 1;
  }

  return 0;
}

int validate_pipes(char **input_arr){
  int input_arr_len = get_arr_len(input_arr);

  for (int i=0; i<input_arr_len; i++){
    if (strcmp(input_arr[i], "|") == 0){
      if (i == 0 || i == input_arr_len-1){
        return 1;
      }
      if (strcmp(input_arr[i-1], "|") == 0 || strcmp(input_arr[i+1], "|") == 0){
        return 1;
      }
      if (strcmp(input_arr[i-1], "<") == 0 || strcmp(input_arr[i+1], "<") == 0){
        return 1;
      }
      if (strcmp(input_arr[i-1], ">") == 0 || strcmp(input_arr[i+1], ">") == 0){
        return 1;
      }
    }
  }

  return 0;
}

int validate_and_process_output_redirects(char **input_arr){
  int out_count=0, out_pos=-1, out_append_count=0;
  char *out_filename=NULL, *out_append_filename=NULL;
  int input_arr_len = get_arr_len(input_arr);

  for (int i=0; i<input_arr_len; i++){
    if (strcmp(input_arr[i], ">") == 0){
      out_count++;
      if ((out_count + out_append_count) > 1){
        return 1;
      }
      if (input_arr[i+1] == NULL){
        return 1;
      }
      else{
        if(input_arr[i+2] != NULL && strcmp(input_arr[i+2], "<") != 0){
          return 1;
        }
      }
      out_filename = input_arr[i+1];
      out_pos = i;
    }

    if (strcmp(input_arr[i], ">>") == 0){
      out_append_count++;
      if ((out_count + out_append_count) > 1){
        return 1;
      }
      if (input_arr[i+1] == NULL){
        return 1;
      }
      else{
        if(input_arr[i+2] != NULL && strcmp(input_arr[i+2], "<") != 0){
          return 1;
        }
      }
      out_append_filename = input_arr[i+1];
      out_pos = i;
    }
  }

  if (out_count == 1){
    int fd = open(out_filename, O_CREAT | O_WRONLY | O_TRUNC, S_IRUSR | S_IWUSR);
    dup2(fd, 1);
    close(fd);
  }

  if (out_append_count == 1){
    int fd = open(out_append_filename, O_CREAT | O_WRONLY | O_APPEND, S_IRUSR | S_IWUSR);
    dup2(fd, 1);
    close(fd);
  }

  if (out_count == 1 || out_append_count == 1){
    int j;
    for (j = out_pos; j < input_arr_len-2; j++) {
      input_arr[j] = input_arr[j+2];
    }
    input_arr[j] = NULL;
    input_arr[j + 1] = NULL;
  }
  
  return 0;
}

int validate_and_process_input_redirects(char **input_arr){
  int in_count=0, in_pos=-1;
  char *in_filename=NULL;
  int input_arr_len = get_arr_len(input_arr);

  for (int i=0; i<input_arr_len; i++){
    if (strcmp(input_arr[i], "<<") == 0){
      return 1;
    }

    if (strcmp(input_arr[i], "<") == 0){
      in_count++;
      if (in_count > 1){
        return 1;
      }
      if (input_arr[i+1] == NULL){
        return 1;
      }
      else{
        if(input_arr[i+2] != NULL && strcmp(input_arr[i+2], ">") != 0 && strcmp(input_arr[i+2], ">>") != 0){
          return 1;
        }
      }
      in_filename = input_arr[i+1];
      in_pos = i;
    }
  }

  if (in_count == 1){
    if (access(in_filename, F_OK) != 0) {
      return 2;
    }

    int fd = open(in_filename, O_RDONLY);
    dup2(fd, 0);
    close(fd);

    int j;
    for (j = in_pos; j < input_arr_len-2; j++) {
      input_arr[j] = input_arr[j+2];
    }
    input_arr[j] = NULL;
    input_arr[j + 1] = NULL;
  }
  
  return 0;
}

job_t* add_suspended_job(char* input_str, pid_t pid, job_t *sus_jobs_list) {
    job_t* new_job = (job_t*) malloc(sizeof(job_t));
    new_job->pid = pid;
    strcpy(new_job->input_str, input_str);
    new_job->next = NULL;

    // Add to end of list
    if (sus_jobs_list == NULL) {
        sus_jobs_list = new_job;
    } else {
        job_t* curr_job = sus_jobs_list;
        while (curr_job->next != NULL) {
            curr_job = curr_job->next;
        }
        curr_job->next = new_job;
    }
    return sus_jobs_list;
}

void sigstop_handler(int signum __attribute__((unused))) {
    printf("Child process %d has been suspended.\n", getpid());
}

int find_pipes(char **input_arr){
  int i=0, count=0;
  while (input_arr[i] != NULL){
    if (strcmp(input_arr[i], "|") == 0){
      count++;
    }
    i++;
  }
  return count;
}

int get_pipe_index_and_commands(char **input_arr){
  int i=0;
  while(input_arr[i] != NULL){
    if (strcmp(input_arr[i], "|") == 0){
      break;
    }
    i++;
  }
  return i;
}

char** get_pipe_command_1(char **input_arr, int pipe_index){
  char **input_arr_1 = (char **)malloc(sizeof(char *) * (pipe_index+1));

  for (int j=0; j<pipe_index; j++){
    input_arr_1[j] = malloc((strlen(input_arr[j]) + 1) * sizeof(char));
    strcpy(input_arr_1[j], input_arr[j]);
  }
  input_arr_1[pipe_index] = NULL;

  return input_arr_1;
}

char** get_pipe_command_2(char **input_arr, int pipe_index){
  int input_arr_len = get_arr_len(input_arr);
  char **input_arr_2 = (char **)malloc(sizeof(char *) * (input_arr_len-pipe_index));

  int p=0;
  for (int k=pipe_index+1; k<input_arr_len; k++){
    input_arr_2[p] = malloc((strlen(input_arr[k]) + 1) * sizeof(char));
    strcpy(input_arr_2[p], input_arr[k]);
    p++;
  }
  input_arr_2[p] = NULL;

  return input_arr_2;
}

