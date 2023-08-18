#include <stdio.h>
#include <stdlib.h>
#include <pwd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <mush.h>

int sigint = 0;
void handler(int signum);
void freePipe(int** cmdPipe, int num);
void cdCommand(struct clstage * cStage);
int runCmd(pipeline commandPipe, sigset_t *set);
int main(int argc, const char * argv[]) {
    sigset_t set;
    struct sigaction sa;
    FILE* in;
    char* commandLine = NULL;
    pipeline commandPipe = NULL;
    sa.sa_flags = 0;
    sa.sa_handler = handler;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
    sigemptyset(&set);
    sigaddset(&set, SIGINT);
    
    if(argc == 2){
        if((in = fopen(argv[1], "r")) == NULL){
            perror(argv[1]);
            exit(EXIT_FAILURE);
        }
    }else if(argc > 2){
        fprintf(stderr,"usage: mush2 [file]\n");
        exit(EXIT_FAILURE);
    }else{
        in = stdin;
    }
    while(!feof(in) && !ferror(in)){
        if(in == stdin){
            printf("8-P ");
            fflush(stdout);
        }
        if((commandLine = readLongString(in))){
            if((commandPipe = crack_pipeline(commandLine)))
                runCmd(commandPipe, &set);
        }
        if(sigint){
            clearerr(in);
            sigint = 0;
        fflush(stdout);
        }
        free(commandLine);
        commandLine = NULL;
        free_pipeline(commandPipe);
        commandPipe = NULL;
        }
    if(in != stdin)
        fclose(in);
    else
        printf("\n");
    return 0;
}
void handler(int signum){
    sigint = 1;
    printf("\n8-P \n");
}
/*free array store cmd*/
void freePipe(int** cmdPipe, int num){
    int i=0;
    for(i=0; i < num; ++i)
        if((cmdPipe)[i])
            free((cmdPipe)[i]);
    free(cmdPipe);
}
/*run the command from input*/
int runCmd(pipeline commandPipe, sigset_t *set){
    int flag = 0, flagParent = 0, child = 0, size = commandPipe->length;
    int i, in, out, status;
    pid_t pidFork;
    int **cmdPipe = NULL;
    struct clstage* stage;
    pid_t* pid  = NULL;
    if(size - 1 > 0){
        cmdPipe = malloc(size * sizeof(int*));
        if(cmdPipe == NULL){
            freePipe(cmdPipe, size);
            perror("malloc");
            exit(EXIT_FAILURE);
        }
        for(i = 0; i < size; i++){
            cmdPipe[i] = malloc(2 * sizeof(int));
            if(cmdPipe[i])
                pipe(cmdPipe[i]);
            else{
                freePipe(cmdPipe, i - 1);
                perror("malloc pipe");
                return -1;
            }
        }
    }
    pid = malloc(size * sizeof(pid_t));
    if(!pid){
        perror("malloc pid");
        exit(EXIT_FAILURE);
    }
    /*go through all stage and fork*/
    for(i=0; i < size; ++i){
        stage = &commandPipe->stage[i];
        if(strcmp(stage->argv[0], "cd") == 0){
            cdCommand(stage);
            continue;
        }
        if((pidFork = fork()) == 0){
            /*picking in and out file*/
            if(child != 0)
                in = cmdPipe[i-1][0];
            else
                in = STDIN_FILENO;
            if(child == size - 1)
                out = STDOUT_FILENO;
            else
                out = cmdPipe[i][1];
            if(stage->inname){
                if((in = open(stage->inname, O_RDONLY)) == -1){
                    perror(stage->inname);
                    flag = 1;
                }
                dup2(in, STDIN_FILENO);
                close(in);
            }
            else{
                dup2(in, STDIN_FILENO);
            }
            if(stage->outname){
                if((out = open(stage->outname, O_WRONLY|O_CREAT|O_TRUNC,S_IRUSR|S_IWUSR)) == -1){
                    perror(stage->outname);
                    flag = 1;
                }
                else{
                    dup2(out, STDOUT_FILENO);
                    close(out);
                }
            }
            else{
                dup2(out, STDOUT_FILENO);
            }
            
            for(i = 0; i < size - 1; i++){
                if(cmdPipe[i]){
                    close(cmdPipe[i][0]);
                    close(cmdPipe[i][1]);
                }
            }
            execvp(stage->argv[0], stage->argv);
            if(size - 1 > 0)
                freePipe(cmdPipe, size - 1);
            free(pid);
            perror(stage->argv[0]);
            exit(EXIT_FAILURE);
        }
        pid[child] = pidFork;
        child++;
    }
    /*parent*/
    for(i = 0; i < size - 1; i++){
        if(cmdPipe[i]){
            close(cmdPipe[i][0]);
            close(cmdPipe[i][1]);
        }
    }
    /*block sigint*/
    sigprocmask(SIG_BLOCK, set, NULL);
    for(i = 0; i < child; i++){
        if(waitpid(pid[i], &status, 0) != pid[i]){
            perror(" waitpid");
            flagParent = 1;
        }
        if(WEXITSTATUS(status) || !(WIFEXITED(status)))
            flagParent = 1;
    }
    /*unblock sigint*/
    sigprocmask(SIG_UNBLOCK, set, NULL);
    if(cmdPipe)
        freePipe(cmdPipe, size);
    if(pid)
        free(pid);
    if(flag == 0 && flagParent == 0)
        return 0;
    return -1;
}
void cdCommand(struct clstage *stage){
    struct passwd* pass;
    char *path;
    /*check cd cmd, if no path, then look for home dir or passwd dir*/
    if(stage->argv[1] != NULL){
        path = stage->argv[1];
    }
    else{
        if((path = getenv("HOME")) == NULL){
            pass = getpwuid(getuid());
            path = pass->pw_dir;
            if(path == NULL){
                fprintf(stderr, "no home directory\n");
            }
        }
    }
    if((chdir(path)) == -1){
        perror(path);
    }
}


