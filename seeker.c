#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <syslog.h>
#include <string.h>
#include <dirent.h>
#include <time.h>
#include <errno.h>

volatile sig_atomic_t zrestartuj = 0;
volatile sig_atomic_t przerwij = 0;

void obsloz_przerwania(int sig) {
    if (sig == SIGUSR1) zrestartuj = 1;
    if (sig == SIGUSR2) przerwij = 1;
}

void stworz_Kondiego() { 
    pid_t pid = fork();

    if (pid < 0) exit(EXIT_FAILURE);
    if (pid > 0) exit(EXIT_SUCCESS);

    if (setsid() < 0) exit(EXIT_FAILURE);

    signal(SIGHUP, SIG_IGN);

    pid = fork();

    if (pid < 0) exit(EXIT_FAILURE);
    if (pid > 0) exit(EXIT_SUCCESS);

    umask(0);
    chdir("/");

    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);
}

int skanuj(const char *sciecha, char *wzorce[], int ile_wzorcow, int verbose) {
    struct dirent *plik;
    DIR *dir = opendir(sciecha);

    if (!dir) {
        return 0; 
    }

    while ((plik = readdir(dir)) != NULL) {
        if (zrestartuj || przerwij) {
            closedir(dir);
            return 1;
        }

        char path[1024];

        if (strcmp(plik->d_name, ".") == 0 || strcmp(plik->d_name, "..") == 0)
            continue;

        snprintf(path, sizeof(path), "%s/%s", sciecha, plik->d_name);

        if(access(path, R_OK) != 0) {
            if (verbose) {
                syslog(LOG_WARNING, "Debug: Odmowa dostepu do \"%s\"", path);
            }
            continue;
        }

        for (int i = 0; i < ile_wzorcow; i++) {
            if (verbose) {
                syslog(LOG_INFO, "Debug: Porownanie \"%s\" z \"%s\"", plik->d_name, wzorce[i]);
            }

            if (strstr(plik->d_name, wzorce[i]) != NULL) {
                // Znaleziono! Zapis do syslog
                syslog(LOG_INFO, "Znaleziono ~ Sciezka: \"%s\" | Wzorzec: \"%s\"", path, wzorce[i]);
            }
        }

        struct stat statbuf;
        if (stat(path, &statbuf) == 0 && S_ISDIR(statbuf.st_mode)) {
            if (access(path, R_OK | X_OK) == 0) {
                if (skanuj(path, wzorce, ile_wzorcow, verbose)) {
                    closedir(dir);
                    return 1;
                }
            }
        }
    }
    closedir(dir);
    return 0;
}

int main(int argc, char *argv[]) {
    int dlugosc_snu = 30;
    int verbose = 0;
    int opcje;
    
    while ((opcje = getopt(argc, argv, "vt:")) != -1) {
        switch (opcje) {
            case 'v': 
                verbose = 1; 
                break;
            case 't': 
                dlugosc_snu = atoi(optarg); 
                break;
            default:
                fprintf(stderr, "Aby uzyc seeker'a: ./seeker [-v] [-t sekundy] wzorzec1 [wzorzec2...]\n");   
                exit(EXIT_FAILURE);
        }
    }

    char **wzorce = &argv[optind];
    int ile_wzorcow = argc - optind;

    if (ile_wzorcow == 0) {
        fprintf(stderr, "Brak wzorcow do porownania\n");
        exit(EXIT_FAILURE);
    }

    stworz_Kondiego();
    openlog("Seeker", LOG_PID, LOG_DAEMON);

    struct sigaction przerwanie;
    przerwanie.sa_handler = obsloz_przerwania;
    sigemptyset(&przerwanie.sa_mask);
    przerwanie.sa_flags = 0;
    sigaction(SIGUSR1, &przerwanie, NULL);
    sigaction(SIGUSR2, &przerwanie, NULL);

    syslog(LOG_INFO, "Seeker uruchomiony. Dlugosc snu: %ds", dlugosc_snu);

    while (1) {
        zrestartuj = 0;
        przerwij = 0;

        if (verbose) syslog(LOG_INFO, "Debug: Seeker wybudzony");
        skanuj("/home", wzorce, ile_wzorcow, verbose);

        if (zrestartuj) {
            if (verbose) syslog(LOG_INFO, "Debug: Skanowanie przerwane przez SIGUSR1 - restartowanie skanowania");
            continue;
        }
        
        if (przerwij) {
            if (verbose) syslog(LOG_INFO, "Debug: Skanowanie przerwane przez SIGUSR2 - powrot do snu");
        }

        if (verbose) syslog(LOG_INFO, "Debug: Seeker uspiony");

        int remaining = dlugosc_snu;
        while (remaining > 0 && !zrestartuj) {
            remaining = sleep(remaining);
            if (przerwij) {
                przerwij = 0;
            }
        }
    }

    closelog();
    return 0;
}