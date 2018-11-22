/**
 * @Author: uapv1701795
 * @Date:   2018-11-22T15:02:30+01:00
 * @Last modified by:   uapv1701795
 * @Last modified time: 2018-11-22T18:55:32+01:00
 */
 #include "../sockin.h"
 #include <limits.h>
 #include <signal.h>
 #include <sys/wait.h>
 #include <dirent.h>
 #include <sys/stat.h>
 #include <time.h>

 #define PORT 9000
 #define BUFFER_MAX 512

 #define REQUESTFILE 100
 #define UNSUPPORTED 150
 #define SUPPORTED   160
 #define FILEOK      400
 #define BADFILE     200
 #define WELCOME     220
 #define PATHNAME    257

 static char *welcome_message = "Welcome !";
 static const char *users[] = {
   "root","anonymous","ftp","public","admin"
 };

 int run = 1;

 void printerr(char err[]) {
   printf("%s : %d\n",err, errno);
   exit(0);
 }

typedef struct ftp {
  int logged;
  int acc;
  int mode;
  int p_acc;
  char *username;
  char *msg;
} ftp;

typedef struct command {
  char comm[5];
  char arg[UCHAR_MAX];
} command;

 int readn(int sd,char *ptr,int size) {
   int no_left,no_read;
   no_left = size;
   while (no_left > 0) {
     no_read = read(sd,ptr,no_left);
     if(no_read <0)  return(no_read);
     if (no_read == 0) break;
     no_left -= no_read;
     ptr += no_read;
   }
   return(size - no_left);
 }

 int writen(int sd,char *ptr,int size) {
   int no_left,no_written;
   no_left = size;
   while (no_left > 0) {
     no_written = write(sd,ptr,no_left);
     if(no_written <=0)  return(no_written);
     no_left -= no_written;
     ptr += no_written;
   }
   return(size - no_left);
 }

 void wait_for(int signum)
 {
   int status;
   wait(&status);
 }

void welcome(int sock_acc) {
  char welcome[UCHAR_MAX];
  sprintf(welcome, "%d %s\n", WELCOME, welcome_message);
  write(sock_acc, welcome, strlen(welcome));
}

int create_socket(int port)
{
  int sock;
  int reuse = 1;

  /* Server addess */
  struct sockaddr_in server_address = (struct sockaddr_in){
     AF_INET,
     htons(port),
     (struct in_addr){INADDR_ANY}
  };


  if((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0){
    fprintf(stderr, "Cannot open socket");
    exit(EXIT_FAILURE);
  }

  /* Address can be reused instantly after program exits */
  setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof reuse);

  /* Bind socket to server address */
  if(bind(sock,(struct sockaddr*) &server_address, sizeof(server_address)) < 0){
    fprintf(stderr, "Cannot bind socket to address");
    exit(EXIT_FAILURE);
  }

  listen(sock,5);
  return sock;
}

void serve(int sock_acc) {
  int acc, msg, fl, Tcode;
  char fname[UCHAR_MAX];
  char out_buf[BUFFER_MAX];
  FILE *fp;

  acc = 0;
  if((readn(sock_acc,(char *)&acc,sizeof(acc))) < 0) printerr("read error");
  acc = ntohs(acc);
  printf("Request code  :%d\n", acc);
  if(acc != REQUESTFILE) {
    printf("Unknown command\n");
    msg = UNSUPPORTED;
    msg = htons(msg);
    if((writen(sock_acc, (char *)&msg, sizeof(msg)))<0) printerr("write error");
    exit(0);
  }
  msg = SUPPORTED;
  msg = htons(msg);
  if((writen(sock_acc, (char *)&msg, sizeof(msg)))<0) printerr("write error");

  fl = FILEOK;
  if((read(sock_acc, fname, UCHAR_MAX)) <0) {
    printf("Bad file %d\n", errno);
    fl = BADFILE;
  }

  if((fp = fopen(fname,"r")) == NULL) fl = BADFILE;
  Tcode = htons(fl);
  if((writen(sock_acc,(char *)&Tcode,sizeof(Tcode))) < 0) printerr("write (2) error");
  if(fl == BADFILE) {
    close(sock_acc);
    printerr("File open error");
  }
  printf("File is %s\n", fname);
  acc = 0;
  if ((readn(sock_acc,(char *)&acc,sizeof(acc))) < 0) printerr("Server read error");
  printf("command %d\n", ntohs(acc));


}

int acceptc(int socket)
{
  int addrlen = 0;
  struct sockaddr_in client_address;
  addrlen = sizeof(client_address);
  return accept(socket,(struct sockaddr*) &client_address,&addrlen);
}

void push(ftp *Ftp)
{
  write(Ftp->acc, Ftp->msg, strlen(Ftp->msg));
}

void get_command(char *buf, command *cmd) {
  sscanf(buf,"%s %s",cmd->comm,cmd->arg);
}

int in(char *p, const char ** stack, int sz) {
  for (size_t i = 0; i < sz; i++) {
    if(strcmp(p, stack[i])==0) return i;
  }
  return -1;
}

int new_port() {
  srand(time(NULL));
  return 128 + (rand()%64);
}

void str_perm(int perm, char *str_perm) {
  int curperm = 0;
  int flag = 0;
  int read, write, exec;

  /* Flags buffer */
  char fbuff[3];

  read = write = exec = 0;

  int i;
  for(i = 6; i>=0; i-=3){
  /* Explode permissions of user, group, others; starting with users */
  curperm = ((perm & ALLPERMS) >> i ) & 0x7;

  memset(fbuff,0,3);
  /* Check rwx flags for each*/
  read = (curperm >> 2) & 0x1;
  write = (curperm >> 1) & 0x1;
  exec = (curperm >> 0) & 0x1;

  sprintf(fbuff,"%c%c%c",read?'r':'-' ,write?'w':'-', exec?'x':'-');
  strcat(str_perm,fbuff);

  }
}

void _pass(command *cmd, ftp *Ftp) {
  //TODO : password check
  if(Ftp->logged == 1) {
    Ftp->msg = "230 Login success\n";
  } else {
    Ftp->msg = "500 Invalid session\n";
  }
  push(Ftp);
}

void _user(command *cmd, ftp *Ftp) {
  if(in(cmd->arg, users, 5)>=0) {
    Ftp->username = malloc(32);
    memset(Ftp->username,0,32);
    strcpy(Ftp->username,cmd->arg);
    Ftp->logged = 1;
    Ftp->msg = "331 password needed\n";
    //Ftp->msg = "230 Login success\n";
  } else {
    Ftp->msg = "530 invalid username\n";
  }
  push(Ftp);
}

void _quit(command *cmd, ftp *Ftp) {
  Ftp->msg = "221 Goodbye\n";
  push(Ftp);
  close(Ftp->acc);
  exit(0);
}

void _pwd(command *cmd, ftp *Ftp) {
  if(Ftp->logged) {
    char cwd[UCHAR_MAX];
    char rtrn[UCHAR_MAX];
    if(getcwd(cwd, UCHAR_MAX)!=NULL) {
      sprintf(rtrn, "%d %s\n", PATHNAME, cwd);
      Ftp->msg = rtrn;
    } else {
      Ftp->msg = "550 Failed to get folder\n";
    }
    push(Ftp);
  }
}

void _type(command *cmd, ftp *Ftp) {
  if(Ftp->logged) {
    if(cmd->arg[0]=='I'){
      Ftp->msg = "200 Switch to binary\n";
    } else if(cmd->arg[0]=='A') {
      Ftp->msg = "200 Switch to ASCII\n";
    } else {
      Ftp->msg = "504 Swicth mode unailable\n";
    }
  } else {
    Ftp->msg = "530 please login\n";
  }
  push(Ftp);
}

void sockize(int sock, int *ip) {
  socklen_t addrl = sizeof(struct sockaddr_in);
  struct sockaddr_in addr;
  getsockname(sock, (struct sockaddr *)&addr, &addrl);
  char* host = inet_ntoa(addr.sin_addr);
  sscanf(host,"%d.%d.%d.%d",&ip[0],&ip[1],&ip[2],&ip[3]);
}

void _pasv(command *cmd, ftp *Ftp) {
  if(Ftp->logged) {
    int ip[4];
    char buff[UCHAR_MAX];
    int port = new_port();
    sockize(Ftp->acc, ip);
    close(Ftp->acc);
    Ftp->p_acc = create_socket((256*port)+port);
    printf("Port: %d\n",(256*port)+port);
    sprintf(buff, "227 Passive Mode (%d,%d,%d,%d,%d,%d)\n", ip[0],ip[1],ip[2],ip[3],256*port, port);
    Ftp->msg = buff;
    Ftp->mode = 1;
    puts(Ftp->msg);
  } else {
    Ftp->msg = "530 please login\n";
  }
  push(Ftp);
}

void _list(command *cmd, ftp *Ftp) {
  if(Ftp->logged) {
    struct dirent *ent;
    struct stat statbuf;
    struct tm *mtime;
    char timebuff[80], current_dir[UCHAR_MAX];
    int acc;
    time_t rtime;

    char cwd[UCHAR_MAX], cwd_o[UCHAR_MAX];
    memset(cwd,0,UCHAR_MAX);
    memset(cwd_o,0,UCHAR_MAX);
    getcwd(cwd_o,UCHAR_MAX);
    if(strlen(cmd->arg)>0&&cmd->arg[0]!='-') chdir(cmd->arg);
    getcwd(cwd, UCHAR_MAX);
    DIR *d = opendir(cwd);
    if(!d) Ftp->msg = "550 Failed to opendir\n";
    else {
      //acc = acceptc()
      Ftp->msg = "150 getting dir list\n";
      push(Ftp);
      while(ent=readdir(d)) {
        stat(ent->d_name,&statbuf);
        char *perm = malloc(9);
        memset(perm,0,9);
        rtime = statbuf.st_mtime;
        mtime = localtime(&rtime);
        strftime(timebuff,80,"%b %d %H:%M", mtime);
        str_perm(statbuf.st_mode, perm);
        dprintf(Ftp->acc, "%c%s %5ld %4d %4d %8ld %s %s\n",
          (ent->d_type==DT_DIR)?'d':'-',
          perm, statbuf.st_nlink, statbuf.st_uid, statbuf.st_gid, statbuf.st_size, timebuff, ent->d_name);
      }
      push(Ftp);
      Ftp->msg = "226 Dir send done.\n";
      //close()
    }
    closedir(d);
    chdir(cwd_o);
  } else {
    Ftp->msg = "530 please login\n";
  }
  push(Ftp);
}

void reply(command *cmd, ftp *Ftp) {
  if(strcmp(cmd->comm, "USER")==0) _user(cmd, Ftp);
  else if (strcmp(cmd->comm, "PASS")==0) _pass(cmd, Ftp);
  else if (strcmp(cmd->comm, "QUIT")==0) _quit(cmd, Ftp);
  else if (strcmp(cmd->comm, "PWD")==0) _pwd(cmd, Ftp);
  else if (strcmp(cmd->comm, "LIST")==0) _list(cmd, Ftp);
  else if (strcmp(cmd->comm, "TYPE")==0) _type(cmd, Ftp);
  else if (strcmp(cmd->comm, "PASV")==0) _pasv(cmd, Ftp);
  else {
    Ftp->msg = "500 unknown command\n";
    push(Ftp);
  }
}

 int main(int argc, char const *argv[]) {
   struct sockaddr_in srv_addr, cli_addr;
   struct hostent *hp;
   int sock, sock_acc, cli_len, br;
   pid_t pid;
   char buffer[UCHAR_MAX];

   printf("\t[Server]\n");
   if ((sock = socket(AF_INET,SOCK_STREAM,0)) < 0) printerr("Socket error");
   //if(gethostname(buffer, UCHAR_MAX)==-1) printerr("Hostname error");
   //if ((hp = gethostbyname(buffer)) == NULL) { printerr("Hostname error");}
   bzero((char *) &srv_addr,sizeof(srv_addr));
   //bcopy(hp->h_addr, (char *)& srv_addr.sin_addr, hp->h_length);
   srv_addr.sin_family = AF_INET;
   srv_addr.sin_port = htons(PORT);
   srv_addr.sin_addr.s_addr = htons(INADDR_ANY);
   if (bind(sock ,(struct sockaddr *) &srv_addr,sizeof(srv_addr)) < 0) printerr("Bind error");
   printf("[Listening . . .]\n");
   if (listen(sock,5) < 0) printerr("listen error");

   while (run) {
     command *cmd = malloc(sizeof(command));
     ftp *Ftp = malloc(sizeof(ftp));
     if ((sock_acc=accept(sock ,(struct sockaddr *) &cli_addr, &cli_len)) <0) printerr("accept error");
     printf("socket : %d\n", sock_acc);
     if((pid=fork()) == 0) {
       close(sock);
       welcome(sock_acc);

       while(br = read(sock_acc, buffer, UCHAR_MAX)) {
         signal(SIGCHLD,wait_for);
         if(!(br>UCHAR_MAX)) {
           buffer[UCHAR_MAX-1] = '\0';
           get_command(buffer, cmd);
           Ftp->acc = sock_acc;
           if(buffer[0]<=127 || buffer[0]>=0){
             printf("%s\n", cmd->comm);
             reply(cmd,Ftp);
            }

           memset(buffer,0,UCHAR_MAX);
         } else {
           perror("read");
         }

       }

       serve(sock_acc);
       close(sock_acc);
       exit(0);
       printf("Client disconnected.\n");
     } else {
       printf("Closing\n");
       close(sock_acc);
     }
   }

   return 0;
 }
