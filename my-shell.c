#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <libgen.h>
#include <limits.h>
#include <fcntl.h>
#include <sys/wait.h>

#include "helper.h"

int main() {
  // signal handling - parent process ignore SIGINT, SIGQUIT, SIGTSTP
  signal(SIGINT, signal_handler);
  signal(SIGQUIT, signal_handler);
  signal(SIGTSTP, signal_handler);
  signal(SIGSTOP, signal_handler);

  job_t* sus_jobs_list = NULL;
  pid_t pid, status, wait_result;
  
  while(1){

    // print prompt
    char cwd[PATH_MAX];
    getcwd(cwd, sizeof(cwd));
    if (getcwd(cwd, sizeof(cwd)) == NULL) {
      fprintf(stderr, "Error: Failed to get base dir, getcwd() failed.");
      exit(-1);
    }
    printf("[nyush %s]$ ", basename(cwd));
    fflush(stdout);

    // read user input, tokenize and store in array
    int input_arr_len = 0;
    char *input_str = malloc(1024);
    char *input_command_path = malloc(1024);
    char **input_arr = process_input_command(&input_arr_len, &input_str);
    int resume_job = 0;
    pid_t resume_pid = -1;

    // if input array null (i.e. user just hit enter), loop back to prompt
    if(input_arr[0] == NULL){
      continue;
    }

    // check if built in commands (cd, exit), execute, loop back to prompt
    if (check_and_execute_builtin_command(input_arr, input_arr_len, sus_jobs_list) == 1){
      free_memory(input_arr, input_arr_len);
      continue;
    }
    if (check_and_execute_builtin_command(input_arr, input_arr_len, sus_jobs_list) == 2){
      sus_jobs_list = remove_suspended_job(sus_jobs_list, atoi(input_arr[1]), &resume_pid, &input_str);
      resume_job = 1;
      kill(resume_pid, SIGCONT);
    }

    if (resume_job == 0){
      // check command path and convert to absolute form (only relative path or basename cases)
      process_command_path(input_arr, &input_command_path);
    
      // fork child process
      pid = fork();

      // print error if fork fails
      if (pid < 0) {
        fprintf(stderr, "Error: Fork failed. Command as not executed. Try again.\n");
        free_memory(input_arr, input_arr_len);
        continue;
      } 
      
      else if (pid == 0) {
        // child process

        // handle pipes
        int pipe_invalid = validate_pipes(input_arr);
        if (pipe_invalid != 0){
          fprintf(stderr, "Error: invalid command\n");
          free_memory(input_arr, input_arr_len);
          exit(1);
        }

        int num_pipe = find_pipes(input_arr);

        if (num_pipe > 0){
          char **input_arr_1=NULL, **input_arr_2=NULL, **input_arr_3=NULL;
          char *input_command_1_path=NULL, *input_command_2_path=NULL, *input_command_3_path=NULL;
          int pipe_index=-1;

          if (num_pipe == 1){
            pipe_index = get_pipe_index_and_commands(input_arr);
            input_arr_1 = get_pipe_command_1(input_arr, pipe_index);
            input_arr_2 = get_pipe_command_2(input_arr, pipe_index);
            input_command_1_path = malloc(1024);
            input_command_2_path = malloc(1024);

            process_command_path(input_arr_1, &input_command_1_path);
            process_command_path(input_arr_2, &input_command_2_path);

            // handle output redirects
            int pipe_out_invalid = validate_and_process_output_redirects(input_arr_2);
            if (pipe_out_invalid == 1){
              fprintf(stderr, "Error: invalid command\n");
              exit(1);
            }
          }

          else if (num_pipe == 2){
            pipe_index = get_pipe_index_and_commands(input_arr);
            input_arr_1 = get_pipe_command_1(input_arr, pipe_index);
            input_command_1_path = malloc(1024);
            process_command_path(input_arr_1, &input_command_1_path);

            char **input_arr_2_3 = get_pipe_command_2(input_arr, pipe_index);
            pipe_index = get_pipe_index_and_commands(input_arr_2_3);
            input_arr_2 = get_pipe_command_1(input_arr_2_3, pipe_index);
            input_command_2_path = malloc(1024);
            process_command_path(input_arr_2, &input_command_2_path);

            input_arr_3 = get_pipe_command_2(input_arr_2_3, pipe_index);
            input_command_3_path = malloc(1024);
            process_command_path(input_arr_3, &input_command_3_path);

            // handle output redirects
            int pipe_out_invalid = validate_and_process_output_redirects(input_arr_3);
            if (pipe_out_invalid == 1){
              fprintf(stderr, "Error: invalid command\n");
              exit(1);
            }
          }

          else {
            exit(1);
          }

          // handle input redirects
          int pipe_in_invalid = validate_and_process_input_redirects(input_arr_1);
          if (pipe_in_invalid != 0){
            if (pipe_in_invalid == 1){
            fprintf(stderr, "Error: invalid command\n");
            }
            if (pipe_in_invalid == 2){
              fprintf(stderr, "Error: invalid file\n");
            }
            exit(1);
          }


          if (num_pipe == 1){
            // single pipe case

            int pipefd[2];
            if (pipe(pipefd) == -1) {
              perror("pipe");
              exit(1);
            }
            
            // pipe child 1
            pid_t pipe_pid1 = fork();
            if (pipe_pid1 == 0) {
              close(pipefd[0]);
              dup2(pipefd[1], STDOUT_FILENO);
              close(pipefd[1]);
              execv(input_command_1_path, input_arr_1);
              perror("execvp");
              exit(1);
            }

            // pipe child 2
            pid_t pipe_pid2 = fork();
            if (pipe_pid2 == 0) { 
              close(pipefd[1]);
              dup2(pipefd[0], STDIN_FILENO);
              close(pipefd[0]);
              execv(input_command_2_path, input_arr_2);
              perror("execvp");
              exit(1);
            }

            // pipe parent process
            int pipestatus;
            close(pipefd[0]);
            close(pipefd[1]);
            waitpid(pipe_pid1, &pipestatus, 0);
            waitpid(pipe_pid2, &pipestatus, 0);
            exit(0);
          }
          else {
            // 2 pipe case

            int pipefd1[2], pipefd2[2];
            if (pipe(pipefd1) == -1) {
              perror("pipe1");
              exit(1);
            }
            if (pipe(pipefd2) == -1) {
              perror("pipe2");
              exit(1);
            }
            
            pid_t pipe_pid1 = fork();
            if (pipe_pid1 == 0) { 
              close(pipefd2[0]);
              close(pipefd2[1]);
              close(pipefd1[0]); 
              dup2(pipefd1[1], STDOUT_FILENO); 
              close(pipefd1[1]); 
              execv(input_command_1_path, input_arr_1);
              perror("execvp");
              exit(1);
            }

            pid_t pipe_pid2 = fork();
            if (pipe_pid2 == 0) { 
              close(pipefd1[1]); 
              close(pipefd2[0]);
              dup2(pipefd1[0], STDIN_FILENO);
              dup2(pipefd2[1], STDOUT_FILENO);
              close(pipefd1[0]);
              close(pipefd2[1]);
              execv(input_command_2_path, input_arr_2);
              perror("execvp");
              exit(1);
            }

            pid_t pipe_pid3 = fork();
            if (pipe_pid3 == 0) {
              close(pipefd1[0]);
              close(pipefd1[1]);
              close(pipefd2[1]);
              dup2(pipefd2[0], STDIN_FILENO);
              close(pipefd2[0]); 
              execv(input_command_3_path, input_arr_3);
              perror("execvp");
              exit(1);
            }

            // pipe parent process
            int pipestatus;
            close(pipefd1[0]);
            close(pipefd1[1]);
            close(pipefd2[0]);
            close(pipefd2[1]);
            waitpid(pipe_pid1, &pipestatus, 0);
            waitpid(pipe_pid2, &pipestatus, 0);
            waitpid(pipe_pid3, &pipestatus, 0);
            exit(0);
          }
        
        }

        else {

          // handle output redirects
          int out_invalid = validate_and_process_output_redirects(input_arr);
          if (out_invalid == 1){
            fprintf(stderr, "Error: invalid command\n");
            free_memory(input_arr, input_arr_len);
            exit(1);
          }

          // handle input redirects
          int in_invalid = validate_and_process_input_redirects(input_arr);
          if (in_invalid != 0){
            if (in_invalid == 1){
            fprintf(stderr, "Error: invalid command\n");
            }
            if (in_invalid == 2){
              fprintf(stderr, "Error: invalid file\n");
            }
            free_memory(input_arr, input_arr_len);
            exit(1);
          }

          // handle invalid pipe commands
          int pipe_invalid = validate_pipes(input_arr);
          if (pipe_invalid != 0){
            fprintf(stderr, "Error: invalid command\n");
            free_memory(input_arr, input_arr_len);
            exit(1);
          }
          execv(input_command_path, input_arr);

          fprintf(stderr, "Error: invalid program\n");
          exit(1);

        }

      }
    }
    
    // signal handler for child process
    if (resume_job == 0){
      wait_result = waitpid(pid, &status, WUNTRACED);
    }
    if (resume_job == 1){
      wait_result = waitpid(resume_pid, &status, WUNTRACED);
    }

    if (wait_result == -1) {
      fprintf(stderr, "Error: waitpid() failed.\n");
      exit(1);
    }
    
    if (WIFSTOPPED(status)) {
      if (resume_job == 0){
        sus_jobs_list = add_suspended_job(input_str, pid, sus_jobs_list);
      }
      if (resume_job == 1){
        sus_jobs_list = add_suspended_job(input_str, wait_result, sus_jobs_list);
      }

    }

    // free allocated memory for input array
    free_memory(input_arr, input_arr_len);
        
  }

  return 0;
}



/*
References:

Lecture Notes
Man pages
https://stackoverflow.com/questions/22452314/how-to-get-the-current-working-directorycurrent-working-folder-name-instead-of
https://c-for-dummies.com/blog/?p=1112
https://linuxhint.com/getline-function-c/
https://www.tutorialspoint.com/cprogramming/c_structures.htm
https://stackoverflow.com/questions/42478868/how-do-i-properly-free-memory-related-to-getline-function
https://c-for-dummies.com/blog/?p=5445#:~:text=The%20getline()%20function%20allocates,automatically%20re%2Dallocates%20the%20pointer.
https://stackoverflow.com/questions/21248840/example-of-waitpid-in-use
https://stackoverflow.com/questions/41884685/implicit-declaration-of-function-wait
https://stackoverflow.com/questions/15472299/split-string-into-tokens-and-save-them-in-an-array
https://www.geeksforgeeks.org/strtok-strtok_r-functions-c-examples/
https://www.tutorialspoint.com/c_standard_library/c_function_strcpy.htm
https://stackoverflow.com/questions/15961253/c-correct-usage-of-strtok-r
https://www.geeksforgeeks.org/count-words-in-a-given-string/
https://man7.org/linux/man-pages/man3/getline.3.html
https://stackoverflow.com/questions/33485806/using-strtok-strtok-r-in-a-while-loop-in-c
https://stackoverflow.com/questions/41421391/how-to-strtok-in-for-loop
https://man7.org/linux/man-pages/man3/strtok_r.3.html
https://www.geeksforgeeks.org/dynamic-memory-allocation-in-c-using-malloc-calloc-free-and-realloc/
https://www.tutorialspoint.com/c_standard_library/c_function_strcpy.htm
https://www.geeksforgeeks.org/how-to-append-a-character-to-a-string-in-c/
https://www.geeksforgeeks.org/g-fact-66/
https://www.tutorialspoint.com/c_standard_library/c_function_strchr.htm
https://www.tutorialspoint.com/single-quotes-vs-double-quotes-in-c-or-cplusplus#:~:text=In%20C%20and%20C%2B%2B%20the,character%20array%20in%20this%20case.
https://stackoverflow.com/questions/3599160/how-can-i-suppress-unused-parameter-warnings-in-c
https://www.geeksforgeeks.org/chdir-in-c-language-with-examples/
https://stackoverflow.com/questions/11042218/c-restore-stdout-to-terminal
https://linux.die.net/man/2/access
https://www.eg.bucknell.edu/~whutchis/linux_install/node136.html#:~:text=To%20restart%20the%20job%20in,fg%20(for%20%60%60foreground'').&text=The%20shell%20prints%20the%20name,the%20job%20into%20the%20background.
https://askubuntu.com/questions/897392/list-only-processes-that-are-in-suspended-mode
https://www.educative.io/answers/how-to-use-the-typedef-struct-in-c
https://www.tutorialspoint.com/cprogramming/c_typedef.htm
http://web.stanford.edu/~hhli/CS110Notes/CS110NotesCollection/Topic%202%20Multiprocessing%20(4).html
https://www.geeksforgeeks.org/gdb-step-by-step-introduction/
https://linuxhint.com/signal_handlers_c_programming_language/
https://stackoverflow.com/questions/2191414/implement-piping-using-c-fork-used
https://stackoverflow.com/questions/21914632/implementing-pipe-in-c
*/