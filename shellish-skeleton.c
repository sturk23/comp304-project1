#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <termios.h> // termios, TCSANOW, ECHO, ICANON
#include <unistd.h>
#include <fcntl.h>
const char *sysname = "shellish";

enum return_codes {
  SUCCESS = 0,
  EXIT = 1,
  UNKNOWN = 2,
};

struct command_t {
  char *name;
  bool background;
  bool auto_complete;
  int arg_count;
  char **args;
  char *redirects[3];     // in/out redirection
  struct command_t *next; // for piping
};

/**
 * Prints a command struct
 * @param struct command_t *
 */
void print_command(struct command_t *command) {
  int i = 0;
  printf("Command: <%s>\n", command->name);
  printf("\tIs Background: %s\n", command->background ? "yes" : "no");
  printf("\tNeeds Auto-complete: %s\n", command->auto_complete ? "yes" : "no");
  printf("\tRedirects:\n");
  for (i = 0; i < 3; i++)
    printf("\t\t%d: %s\n", i,
           command->redirects[i] ? command->redirects[i] : "N/A");
  printf("\tArguments (%d):\n", command->arg_count);
  for (i = 0; i < command->arg_count; ++i)
    printf("\t\tArg %d: %s\n", i, command->args[i]);
  if (command->next) {
    printf("\tPiped to:\n");
    print_command(command->next);
  }
}

/**
 * Release allocated memory of a command
 * @param  command [description]
 * @return         [description]
 */
int free_command(struct command_t *command) {
  if (command->arg_count) {
    for (int i = 0; i < command->arg_count; ++i)
      free(command->args[i]);
    free(command->args);
  }
  for (int i = 0; i < 3; ++i)
    if (command->redirects[i])
      free(command->redirects[i]);
  if (command->next) {
    free_command(command->next);
    command->next = NULL;
  }
  free(command->name);
  free(command);
  return 0;
}

/**
 * Show the command prompt
 * @return [description]
 */
int show_prompt() {
  char cwd[1024], hostname[1024];
  gethostname(hostname, sizeof(hostname));
  getcwd(cwd, sizeof(cwd));
  printf("%s@%s:%s %s$ ", getenv("USER"), hostname, cwd, sysname);
  return 0;
}

/**
 * Parse a command string into a command struct
 * @param  buf     [description]
 * @param  command [description]
 * @return         0
 */
int parse_command(char *buf, struct command_t *command) {
  const char *splitters = " \t"; // split at whitespace
  int index, len;
  len = strlen(buf);
  while (len > 0 && strchr(splitters, buf[0]) != NULL) // trim left whitespace
  {
    buf++;
    len--;
  }
  while (len > 0 && strchr(splitters, buf[len - 1]) != NULL)
    buf[--len] = 0; // trim right whitespace

  if (len > 0 && buf[len - 1] == '?') // auto-complete
    command->auto_complete = true;
  if (len > 0 && buf[len - 1] == '&') // background
    command->background = true;

  char *pch = strtok(buf, splitters);
  if (pch == NULL) {
    command->name = (char *)malloc(1);
    command->name[0] = 0;
  } else {
    command->name = (char *)malloc(strlen(pch) + 1);
    strcpy(command->name, pch);
  }

  command->args = (char **)malloc(sizeof(char *));

  int redirect_index;
  int arg_index = 0;
  char temp_buf[1024], *arg;
  while (1) {
    // tokenize input on splitters
    pch = strtok(NULL, splitters);
    if (!pch)
      break;
    arg = temp_buf;
    strcpy(arg, pch);
    len = strlen(arg);

    if (len == 0)
      continue; // empty arg, go for next
    while (len > 0 && strchr(splitters, arg[0]) != NULL) // trim left whitespace
    {
      arg++;
      len--;
    }
    while (len > 0 && strchr(splitters, arg[len - 1]) != NULL)
      arg[--len] = 0; // trim right whitespace
    if (len == 0)
      continue; // empty arg, go for next

    // piping to another command
    if (strcmp(arg, "|") == 0) {
      struct command_t *c =
          (struct command_t *)malloc(sizeof(struct command_t));
      int l = strlen(pch);
      pch[l] = splitters[0]; // restore strtok termination
      index = 1;
      while (pch[index] == ' ' || pch[index] == '\t')
        index++; // skip whitespaces

      parse_command(pch + index, c);
      pch[l] = 0; // put back strtok termination
      command->next = c;
      continue;
    }

    // background process
    if (strcmp(arg, "&") == 0)
      continue; // handled before

    // handle input redirection
    redirect_index = -1;
    if (arg[0] == '<')
      redirect_index = 0;
    if (arg[0] == '>') {
      if (len > 1 && arg[1] == '>') {
        redirect_index = 2;
        arg++;
        len--;
      } else
        redirect_index = 1;
    }
    if (redirect_index != -1) {
      command->redirects[redirect_index] = (char *)malloc(len);
      strcpy(command->redirects[redirect_index], arg + 1);
      continue;
    }

    // normal arguments
    if (len > 2 &&
        ((arg[0] == '"' && arg[len - 1] == '"') ||
         (arg[0] == '\'' && arg[len - 1] == '\''))) // quote wrapped arg
    {
      arg[--len] = 0;
      arg++;
    }
    command->args =
        (char **)realloc(command->args, sizeof(char *) * (arg_index + 1));
    command->args[arg_index] = (char *)malloc(len + 1);
    strcpy(command->args[arg_index++], arg);
  }
  command->arg_count = arg_index;

  // increase args size by 2
  command->args = (char **)realloc(command->args,
                                   sizeof(char *) * (command->arg_count += 2));

  // shift everything forward by 1
  for (int i = command->arg_count - 2; i > 0; --i)
    command->args[i] = command->args[i - 1];

  // set args[0] as a copy of name
  command->args[0] = strdup(command->name);
  // set args[arg_count-1] (last) to NULL
  command->args[command->arg_count - 1] = NULL;

  return 0;
}

void prompt_backspace() {
  putchar(8);   // go back 1
  putchar(' '); // write empty over
  putchar(8);   // go back 1 again
}

/**
 * Prompt a command from the user
 * @param  buf      [description]
 * @param  buf_size [description]
 * @return          [description]
 */
int prompt(struct command_t *command) {
  int index = 0;
  char c;
  char buf[4096];
  static char oldbuf[4096];

  // tcgetattr gets the parameters of the current terminal
  // STDIN_FILENO will tell tcgetattr that it should write the settings
  // of stdin to oldt
  static struct termios backup_termios, new_termios;
  tcgetattr(STDIN_FILENO, &backup_termios);
  new_termios = backup_termios;
  // ICANON normally takes care that one line at a time will be processed
  // that means it will return if it sees a "\n" or an EOF or an EOL
  new_termios.c_lflag &=
      ~(ICANON |
        ECHO); // Also disable automatic echo. We manually echo each char.
  // Those new settings will be set to STDIN
  // TCSANOW tells tcsetattr to change attributes immediately.
  tcsetattr(STDIN_FILENO, TCSANOW, &new_termios);

  show_prompt();
  buf[0] = 0;
  while (1) {
    c = getchar();
    // printf("Keycode: %u\n", c); // DEBUG: uncomment for debugging

    if (c == 9) // handle tab
    {
      buf[index++] = '?'; // autocomplete
      break;
    }

    if (c == 127) // handle backspace
    {
      if (index > 0) {
        prompt_backspace();
        index--;
      }
      continue;
    }

    if (c == 27 || c == 91 || c == 66 || c == 67 || c == 68) {
      continue;
    }

    if (c == 65) // up arrow
    {
      while (index > 0) {
        prompt_backspace();
        index--;
      }

      char tmpbuf[4096];
      printf("%s", oldbuf);
      strcpy(tmpbuf, buf);
      strcpy(buf, oldbuf);
      strcpy(oldbuf, tmpbuf);
      index += strlen(buf);
      continue;
    }

    putchar(c); // echo the character
    buf[index++] = c;
    if (index >= sizeof(buf) - 1)
      break;
    if (c == '\n') // enter key
      break;
    if (c == 4) // Ctrl+D
      return EXIT;
  }
  if (index > 0 && buf[index - 1] == '\n') // trim newline from the end
    index--;
  buf[index++] = '\0'; // null terminate string

  strcpy(oldbuf, buf);

  parse_command(buf, command);

  // print_command(command); // DEBUG: uncomment for debugging

  // restore the old settings
  tcsetattr(STDIN_FILENO, TCSANOW, &backup_termios);
  return SUCCESS;
}

// int recursive_pipe(struct command_t *command){
//   if(!command->next){
//     execvp(command->name, command->args);
//     return SUCCESS;  
//   }
//   int fd[2];

//   pipe(fd);
//   pid_t pid = fork();
//   if(pid == 0){
//     dup2(fd[1], STDOUT_FILENO);
//     close(fd[0]);
//     close(fd[1]);
//     execvp(command->name, command->args);
//   }else{
//     dup2(fd[0], STDIN_FILENO);
//     close(fd[0]);
//     close(fd[1]);    
//     recursive_pipe(command->next);
//     wait(0);
//   }
// }

int custom_cut(struct command_t *command){
  char *default_delimeter = "\t";
  char *indices = "";
  char com[1000];
  int size = 50;

  int i = 0;

  while(i < command->arg_count - 1){
    if(strcmp(command->args[i], "-d" ) == 0 || strcmp(command->args[i], "--delimiter") == 0){
      default_delimeter = command->args[i+1];
    }else if(strcmp(command->args[i], "-f") == 0){
      indices = command->args[i+1];
    }
    i++;
  }

  int *ind = malloc(sizeof(int) * strlen(indices));

  char *copy = strdup(indices);

  char *c = strtok(copy, ",");
  int j = 0;
  while (c)
  {
    ind[j] = atoi(c);
    j++;
    c  = strtok(NULL, ",");
  }
  int len_ind = j;
  
  while(fgets(com, sizeof(com), stdin)){
    char **parts = malloc(strlen(com) * 8);
    char *cur = strtok(com,default_delimeter);
    int i = 0;
    while(cur){
      parts[i] = cur;
      i++;
      cur = strtok(NULL, default_delimeter);
    }
    int m = 0;
    int j = 0;
    while(j < len_ind){
      if(m == 0){
        printf("%s", parts[ind[j] -1]);
        m++;
      }else{
        printf("%s", default_delimeter);
        printf("%s", parts[ind[j] -1]);
      }
      j++;
    }
    
    printf("\n");
    
  }
  

  return 0;
}

double calc_sum(double *nums, int size){
  int i = 0;
  double sum = 0;
  while (i < size)
  {
    sum += nums[i];
    i++;
  }
  return sum;
}

double find_min(double *nums, int size){
  double min = nums[0];
  int i = 0;
  while(i < size){
    if (nums[i] < min)
    {
      min = nums[i]; 
    }
    
    i++;
  }

  return min;
}

double find_max(double *nums, int size){
  double max = nums[0];
  int i = 0;
  while(i < size){
    if (nums[i] > max)
    {
      max = nums[i]; 
    }
    
    i++;
  }

  return max;
}

int comparator(const void *p, const void *q) { 
  double i = *(double *)p;
  double j = *(double *)q;
  if (i > j)
  {
    return 1;
  }else if(j > i){
    return -1;
  }
  return 0;
}

int custom_nums(struct command_t *command){
  char *delim = ",";
  int print_sum = 0;
  int print_avg = 0;
  int print_sorted = 0;
  int print_min = 0;
  int print_max = 0;
  int print_count = 0;

  char line[1000];

  
  int i = 0;

  while(i < command->arg_count - 1){
    if(strcmp(command->args[i], "-d" ) == 0 || strcmp(command->args[i], "--delimiter") == 0){
      delim = command->args[i+1];
    }else if(strcmp(command->args[i], "--sum") == 0){
      print_sum = 1;
    }else if(strcmp(command->args[i], "--avg") == 0){
      print_avg = 1;
    }else if(strcmp(command->args[i], "--min") == 0){
      print_min = 1;
    }else if(strcmp(command->args[i], "--max") == 0){
      print_max = 1;
    }else if(strcmp(command->args[i], "--sort") == 0){
      print_sorted = 1;
    }else if(strcmp(command->args[i], "--count") == 0){
      print_count = 1;
    }
    i++;
  }
  
  while(fgets(line, sizeof(line), stdin)){
    double *nums = malloc(sizeof(double) * strlen(line));
    char *word = strtok(line, " ");
    int i = 0;
    while(word)
    {
      double cur_num = atof(word);
      if (cur_num || strcmp(word, "0") == 0)
      {
        nums[i] = cur_num;
        i++;
      }
      word = strtok(NULL, " ");
    }

    if (print_sorted)
    {
      qsort(nums, i, sizeof(double), comparator);
    }

    //printing the numbers
    int j = 0;
    while(j < i){
      if(j != 0){
        printf("%s", delim);
      }
      printf("%.2f", nums[j]);
      j++;
    }
    printf("\n");
    
    if (print_sum)
    {
      double sum = calc_sum(nums, i);
      printf("Sum: %.2f\n", sum);
    }
    if(print_min){
      double min = find_min(nums, i);
      printf("Min: %.2f\n", min);
    }
    if(print_max){
      double max = find_max(nums, i);
      printf("Max: %.2f\n", max);
    }
    if(print_count){
      printf("Count: %d\n", i);
    }
    if (print_avg)
    {
      double average = calc_sum(nums, i) / i;
      printf("Average: %.2f\n", average);
    }
    
    
    free(nums);
  }



  return 0;
}

int process_command(struct command_t *command) {
  int r;
  if (strcmp(command->name, "") == 0)
    return SUCCESS;

  if (strcmp(command->name, "exit") == 0)
    return EXIT;

  if (strcmp(command->name, "cd") == 0) {
    if (command->arg_count > 0) {
      r = chdir(command->args[1]);
      if (r == -1)
        printf("-%s: %s: %s\n", sysname, command->name, strerror(errno));
      return SUCCESS;
    }
  }
  // if(command->next){
  //   pid_t pid = fork();
  //   if(pid == 0){
  //     recursive_pipe(command);
  //     return SUCCESS;
  //   }else{
  //      wait(0);
  //      return SUCCESS;
  //   }
  // }
  pid_t pid = fork();
  if (pid == 0) // child
  {

    // piping the processes and breaking the loop so the process continues to the custom exec logic in the child 
    while (command->next)
    {
      int fd[2];

      pipe(fd);
      pid_t pid = fork();
      if(pid == 0){
        dup2(fd[1], STDOUT_FILENO);
        close(fd[0]);
        close(fd[1]);
        break;
      }else{
        dup2(fd[0], STDIN_FILENO);
        close(fd[0]);
        close(fd[1]);    
        wait(0);
        command = command->next;
      }
    }
    
    /// This shows how to do exec with environ (but is not available on MacOs)
    // extern char** environ; // environment variables
    // execvpe(command->name, command->args, environ); // exec+args+path+environ

    /// This shows how to do exec with auto-path resolve
    // add a NULL argument to the end of args, and the name to the beginning
    // as required by exec

    // TODO: do your own exec with path resolving using execv()
    // do so by replacing the execvp call below
    

    // Handling the input when the redirects[0] is not empty 	  
    if(command->redirects[0]){
    	int fd = open(command->redirects[0], O_RDONLY);
      if(fd < 0){
        perror("File opening error");
        exit(1);
      }
      dup2(fd, STDIN_FILENO);
      close(fd);
    }
    if(command->redirects[1]){
      int fd = open(command->redirects[1], O_WRONLY | O_CREAT | O_TRUNC, 0644);
      if(fd < 0){
        perror("File opening error");
        exit(1);
      }
      dup2(fd, STDOUT_FILENO);
      close(fd);
    }
    if(command->redirects[2]){
      int fd = open(command->redirects[2], O_WRONLY | O_CREAT | O_APPEND, 0644);
      if(fd < 0){
        perror("File opening error");
        exit(1);
      }
      dup2(fd, STDOUT_FILENO);
      close(fd);
    }

    if(strcmp(command->name, "cut") == 0){
      custom_cut(command);
      exit(0);
    }

    if(strcmp(command->name, "num") == 0){
      custom_nums(command);
      exit(0);
    }

    char *path = getenv("PATH");
    char *cur_path = strtok(path, ":");

    while(cur_path){
      int len_command = strlen(command->name);
      int len_path = strlen(cur_path);
      char *full = malloc(len_command + len_path + 2);
      strcpy(full, cur_path);
      strcat(full, "/");
      strcat(full, command->name);
      
      execv(full, command->args);
      cur_path = strtok(NULL, ":");
    }

    //execvp(command->name, command->args); // exec+args+path
    printf("-%s: %s: command not found\n", sysname, command->name);
    exit(127);

  } else {
    // TODO: implement background processes here
    if(command->background == true){
    	return SUCCESS;
    }
    wait(0); // wait for child process to finish
    return SUCCESS;
  }
}

int main() {
  while (1) {
    struct command_t *command =
        (struct command_t *)malloc(sizeof(struct command_t));
    memset(command, 0, sizeof(struct command_t)); // set all bytes to 0

    int code;
    code = prompt(command);
    if (code == EXIT)
      break;

    code = process_command(command);
    if (code == EXIT)
      break;

    free_command(command);
  }

  printf("\n");
  return 0;
}
