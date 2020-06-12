/*
    ttcdt-cgi-thr
    ttcdt <dev@triptico.com>, public domain
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <signal.h>
#include <errno.h>
#include <sys/file.h>
#include <sys/types.h>
#include <sys/wait.h>

#define VERSION "1.04"

#define MAX_HOSTS 1024

struct host_info {
    struct in_addr ip;  /* IP address */
    int cnt;            /* # of connections in the time window */
    time_t last_t;      /* last record update time */
};

struct host_info hosts[MAX_HOSTS];

int max_conn_in_tw  = 15;
int time_window     = 30;
char stat_file[512] = "/tmp/ttcdt-cgi-thr.stat";
char log_file[512]  = "/tmp/ttcdt-cgi-thr.log";
int log_level       = 1;
int ip_mask         = 0x00ffffff;


void do_log(char *msg1, char *msg2, char *fmt, int v)
{
    FILE *f;
    time_t t;
    struct tm *tm;
    char timestr[32];

    t = time(NULL);
    tm = localtime(&t);
    strftime(timestr, sizeof(timestr), "%Y/%m/%d %H:%M:%S", tm);

    if ((f = fopen(log_file, "a")) != NULL) {
        fprintf(f, "%s [%05d]: %s", timestr, getpid(), msg1);

        if (msg2 != NULL)
            fprintf(f, " %s", msg2);

        if (fmt != NULL)
            fprintf(f, fmt, v);

        fprintf(f, "\n");

        fclose(f);
    }
}


int open_stat_file(void)
/* open, lock and load stat file */
{
    int i;

    if ((i = open(stat_file, O_CREAT | O_RDWR, 0666)) == -1) {
        do_log("ERROR : cannot open stat file", stat_file, NULL, 0);
        exit(2);
    }

    flock(i, LOCK_EX);

    if (read(i, hosts, sizeof(hosts)) < sizeof(hosts))
        memset(hosts, '\0', sizeof(hosts));

    return i;
}


void close_stat_file(int i)
/* update, unlock and close stat file */
{
    lseek(i, 0, 0);
    write(i, hosts, sizeof(hosts));
    flock(i, LOCK_UN);
    close(i);
}


int throttle(struct in_addr ip)
{
    int n, f, err;
    struct host_info *e = NULL;
    struct host_info *c = NULL;
    time_t t;

    err = 0;

    f = open_stat_file();

    t = time(NULL);

    /* find an entry for this ip */
    for (n = 0; n < MAX_HOSTS; n++) {
        struct host_info *i = &hosts[n];

        if (i->last_t && t - i->last_t > time_window) {
            /* used entry older than time window? free it */
            i->last_t = 0;
            i->cnt    = 0;

            if (log_level >= 2)
                do_log("FLUSH :", inet_ntoa(i->ip), NULL, 0);
        }

        if ((i->ip.s_addr & ip_mask) == (ip.s_addr & ip_mask)) {
            /* IP match; entry found */
            e = i;
            break;
        }
        else
        if (!c && i->last_t == 0) {
            /* if this entry is free, remember it */
            c = i;
        }

        if (i->ip.s_addr == 0) {
            /* empty IP? there is nothing more to search */
            break;
        }
    }

    if (e == NULL && c != NULL) {
        /* IP not already registered? use the free entry just found */
        e     = c;
        e->ip = ip;
    }

    if (e != NULL) {
        /* process entry */

        /* limit exceeded? fail */
        if (e->cnt > max_conn_in_tw)
            err = -2;
        else
            e->cnt++;

        /* update time */
        e->last_t = t;

        if (log_level >= 2)
            do_log("CONN  :", inet_ntoa(ip), " cnt=%d", e->cnt);
    }
    else {
        /* host database is full */
        do_log("ERROR : Out of host slots -- passing through", NULL, NULL, 0);
        err = 1;
    }

    close_stat_file(f);

    return err;
}


int main(int argc, char *argv[], char *env[])
{
    char *ips;
    struct in_addr ip;
    int fs = 1;
    int st;

    while (fs < argc) {
        if (strcmp(argv[fs], "-n") == 0) {
            fs++;
            max_conn_in_tw = atoi(argv[fs++]);
        }
        else
        if (strcmp(argv[fs], "-w") == 0) {
            fs++;
            time_window = atoi(argv[fs++]);
        }
        else
        if (strcmp(argv[fs], "-v") == 0) {
            fs++;
            log_level = atoi(argv[fs++]);
        }
        else
        if (strcmp(argv[fs], "-l") == 0) {
            fs++;
            strcpy(log_file, argv[fs++]);
        }
        else
        if (strcmp(argv[fs], "-s") == 0) {
            fs++;
            strcpy(stat_file, argv[fs++]);
        }
        else
            break;
    }

    if (argc - fs < 2) {
        printf("ttcdt-cgi-thr %s -- CGI throttler\n", VERSION);
        printf("ttcdt <dev@triptico.com>, public domain\n\n");
        printf("Usage: %s [opts] {remote_ip_addr} {prg} [{args}...]\n", argv[0]);
        printf("\nOptions:\n");
        printf("-n {num}        Max connections in time window (default: %d)\n", max_conn_in_tw);
        printf("-w {secs}       Time window in seconds (default: %d)\n", time_window);
        printf("-l {log_file}   Log file (default: %s)\n", log_file);
        printf("-s {stat_file}  Stat file (default: %s)\n", stat_file);
        printf("-v {num}        Log level (0, 1, 2) (default: %d)\n", log_level);
        return 1;
    }

    ips = argv[fs++];
    inet_aton(ips, &ip);

    if (fork() > 0) {
        int s;
        wait(&s);
        if (s != 0)
            do_log("ERROR :", ips, " Self wait(): %d", s);
        exit(0);
    }

    setsid();

    if ((st = throttle(ip)) >= 0) {
        pid_t pid;

        if (log_level >= 2)
            do_log("SPAWN :", ips, NULL, 0);

        pid = fork();

        if (pid > 0) {
            int s = 0;

            if (wait(&s) != pid)
                do_log("ERROR :", ips, " Unexpected child pid %d", pid);

            if (s != 0)
                do_log("WARN  :", ips, " CGI exit code %d", s);
        }
        else
        if (pid == 0) {
            setsid();
            execve(argv[fs], &argv[fs], env);
            do_log("ERROR :", ips, " execve(): %d", errno);
            exit(1);
        }
        else
            do_log("ERROR :", ips, " fork(): %d", errno);
    }
    else {
        do_log("FILTER:", ips, NULL, 0);

        printf("HTTP/1.1 503 Service Unavailable\r\n\r\n");
    }

    return 0;
}
