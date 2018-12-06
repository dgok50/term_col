/*
    Временный демон для акумулирования и перенаправления данных 2 версия, (fork.c первая)
    На период разработки основного
    - Жёстко настроенное количество и характеристики датчиков
    - Отсутствие многопоточности
*/
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/file.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <sys/stat.h>
#include <syslog.h>
#include <errno.h>
#include <math.h>
#include <string.h>
#include <limits.h>
#include "ups_parse.c"
#include "usred.c"
#include "a1fl.c"

/*Сетевая часть*/
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "multicast_lib.c"

//#include <wiringPiI2C.h>	//Used for I2C
#include <unistd.h>        //Used for UART
#include <fcntl.h>        //Used for UART
#include <termios.h>        //Used for UART
//#define RND 20
#define buffer_size 4096

#define DSE -100
#define RXL 512

const char *sw_name = "KUIP Repiter";
const int sw_ver = 84;
const int hw_ver = 32;

int stop = 0;

void stop_all();

void usr_sig1();

void em_dump();

float computeHeatIndex(float temperature, float percentHumidity) {
    // Using both Rothfusz and Steadman's equations
    // http://www.wpc.ncep.noaa.gov/html/heatindex_equation.shtml
    float hi;

    temperature = temperature * 1.8 + 32;

    hi = 0.5 * (temperature + 61.0 + ((temperature - 68.0) * 1.2) + (percentHumidity * 0.094));

    if (hi > 79) {
        hi = -42.379 +
             2.04901523 * temperature +
             10.14333127 * percentHumidity +
             -0.22475541 * temperature * percentHumidity +
             -0.00683783 * pow(temperature, 2) +
             -0.05481717 * pow(percentHumidity, 2) +
             0.00122874 * pow(temperature, 2) * percentHumidity +
             0.00085282 * temperature * pow(percentHumidity, 2) +
             -0.00000199 * pow(temperature, 2) * pow(percentHumidity, 2);

        if ((percentHumidity < 13) && (temperature >= 80.0) && (temperature <= 112.0))
            hi -= ((13.0 - percentHumidity) * 0.25) * sqrt((17.0 - abs(temperature - 95.0)) * 0.05882);

        else if ((percentHumidity > 85.0) && (temperature >= 80.0) && (temperature <= 87.0))
            hi += ((percentHumidity - 85.0) * 0.1) * ((87.0 - temperature) * 0.2);
    }

    return (hi - 32) * 0.55555;
}

int get_a1pr_data(char *host, char *rx, int rxl) {
    char url[256];
    bzero(url, 256);
    sprintf(url, "http://%s/a1pr", host);
    CURL *curl;
    CURLcode res;
    //return 1;
    bzero(rx, rxl);
    curl = curl_easy_init();
    if (curl) {
        int rc = 0;
        struct string s;
        init_string(&s);

        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
        res = curl_easy_perform(curl);
        strncpy(rx, s.ptr, rxl);
        //syslog(LOG_NOTICE,"vin=%fv load=%fprc frqin=%fhz upsbat=%fprc",*vin,*load,*frqin,*upsbat);
        free(s.ptr);
        for (int r = 0; r < rxl; r++) {
            if (rx[r] == ';') {
                rx[r + 1] = '\0';
                break;
            } else if (rx[r] == ':') {
                rc++;
            }
        }
        /* always cleanup */
        curl_easy_cleanup(curl);
        return rc;
    }
    return -1;
}

void skeleton_daemon() {
    pid_t pid;

    /* Fork off the parent process */
    pid = fork();

    /* An error occurred */
    if (pid < 0)
        exit(EXIT_FAILURE);

    /* Success: Let the parent terminate */
    if (pid > 0)
        exit(EXIT_SUCCESS);

    /* On success: The child process becomes session leader */
    if (setsid() < 0)
        exit(EXIT_FAILURE);

    /* Catch and ignore signals */
    /*TODO: Implement a working signal handler */
    signal(SIGCHLD, SIG_IGN);
    signal(SIGHUP, SIG_IGN);

    /* Fork off for the second time */
    pid = fork();

    /* An error occurred */
    if (pid < 0)
        exit(EXIT_FAILURE);

    /* Success: Let the parent terminate */
    if (pid > 0)
        exit(EXIT_SUCCESS);

    /* Set new file permissions */
    umask(0);

    /* Change the working directory to the root directory */
    /* or another appropriated directory */
    //chdir ("/");

    /* Close all open file descriptors */
    int x;
    for (x = sysconf(_SC_OPEN_MAX); x > 0; x--) {
        close(x);
    }

}

int main(int argc, char *argv[]) {
    int nodem = 1, wait = 0; // Обработчик входных параметров
    if (argc > 1) {
        for (int i = 0; i < argc; i++) {
            if (argv[i][0] == '-') {
                if (argv[i][1] == 'd') { //Запустить как демон
                    nodem = 0;
                } else if (argv[i][1] == 'b') { //запустить с задержкой 2 мин
                    char tst[256];
                    sprintf(tst, "echo '%s SERVER VER: %d.%d.%d  HW_VER: %d.%d.%d\n' | lpr -l -h ", sw_name,
                            sw_ver / 100, (sw_ver % 100) / 10, sw_ver % 10, hw_ver / 100, (hw_ver % 100) / 10,
                            hw_ver % 10);
                    system(tst);
                    system("echo 'BOOT UP AT ' | lpr -l -h ");
                    system("date --rfc-2822 | lpr -l -h ");
                    wait = 1;
                } else if (argv[i][1] == 'v') { //запустить с задержкой 2 мин
                    printf("KUIP REPEATER SERVER V%d.%d.%d\n HW_VER: %d.%d.%d\n", sw_ver / 100, (sw_ver % 100) / 10,
                           sw_ver % 10, hw_ver / 100, (hw_ver % 100) / 10, hw_ver % 10);
                    return 0;
                } else if (argv[i][1] == 'h') { //запустить с задержкой 2 мин
                    printf("KUIP REPEATER SERVER V%d.%d.%d\n Параметры:\n  -d Запуск в режиме демона\n  -b Пауза 2 мин перед началом работы сервиса\n  -v Вывести версию и выйти\n",
                           sw_ver / 100, (sw_ver % 100) / 10, sw_ver % 10);
                    return 0;
                }
            }
        }
    }
    if (nodem == 0)
        skeleton_daemon();

    /* Open the log file */
    openlog("uart_to_html", LOG_PID, LOG_DAEMON);
    syslog(LOG_NOTICE, "Запуск сервиса");

    void (*TermSignal)(int);
    TermSignal = signal(SIGTERM, stop_all);

    void (*IntSignal)(int);
    IntSignal = signal(SIGINT, stop_all);

    void (*Usr1Signal)(int);
    Usr1Signal = signal(SIGUSR1, usr_sig1);

    void (*SegvSignal)(int);
    SegvSignal = signal(SIGSEGV, em_dump);

    void (*QuitSignal)(int);

    QuitSignal = signal(SIGQUIT, stop_all);

    int rc = 0, y = 0, c = 0, itc = 0, src = 0, secr = 0;
    char **name_mas, **name_mas_sec;
    char typec[10], strtmp[100], raw_message[buffer_size];
    float *dat_mas, *dat_mas_sec;
    unsigned char rx[RXL], rx_s[RXL];
    time_t itime;
    pid_t old_pid;
    struct tm *Tm;
    FILE *PID;

    struct usred sred;
    struct usred sred_sec;
    struct usred sred_main;

    int i = 0, rs = RXL;

    PID = fopen("/tmp/kuip_t.pid", "r");
    if (PID != NULL) {
        while (fgets(strtmp, sizeof(strtmp), PID)) {
            old_pid = atoi(strtmp);
        }
        fclose(PID);
        if (old_pid != 0) {
            kill(old_pid, SIGUSR1);
            sleep(3);
            //syslog (LOG_CRIT, "Статус:%d", access("/tmp/kuip_t.here", 0));
            if (access("/tmp/kuip_t.here", 0) == 0) {
                remove("/tmp/kuip_t.here");
                syslog(LOG_ERR, "Приложение уже запущено и отвечает, выхожу.");
                exit(5);
            } else {
                kill(old_pid, SIGKILL);
            }

        }
    }
    syslog(LOG_NOTICE, "pid: %d\n", getpid());
    PID = fopen("/tmp/kuip_t.pid", "w+");
    fprintf(PID, "%d", getpid());
    fclose(PID);


    if (wait == 1) {
        sleep(60); //Дадим подгрузится системе
    }

    for (int errorr = 0; errorr < 20; errorr++) {
        rc = get_a1pr_data("192.168.0.61", rx, RXL);
        if (rc >= 2) {
            syslog(LOG_NOTICE, "Полученны данные от 1 модуля\n");
            break;
        }
    }

    for (int errorr = 0; errorr < 20; errorr++) {
        src = get_a1pr_data("192.168.0.89", rx_s, RXL);
        if (src >= 2) {
            syslog(LOG_NOTICE, "Полученны данные от 2 модуля\n");
            break;
        }
    }

    syslog(LOG_NOTICE, "rc:%d, rx:%s\n", rc, rx);

    syslog(LOG_NOTICE, "src:%d, rx_s:%s\n", src, rx_s);

    syslog(LOG_NOTICE, "получено переменных:%d\n", rc);
    dat_mas = (float *) malloc(rc * sizeof(float));    //Выделение массива для паказаний датчиков
    name_mas = (char **) malloc(rc * sizeof(char *));    //Выделение массива указателей

    if (name_mas == NULL)        //Проверка выдиления
    {
        syslog(LOG_EMERG,
               "Ошибка!\n Невозможно выдилить память.\n");
        exit(EXIT_FAILURE);
    }

    for (i = 0; i < rc; i++) {
        name_mas[i] = (char *) malloc(
                25 * sizeof(char));    //Выдиление массива char для каждого указателя(создание 2 мерного массива)

        if (name_mas[i] != NULL) {
            memset(name_mas[i], '\0', 25 * sizeof(char));    //Заполнение нулями массива
        }
    }


    dat_mas_sec = (float *) malloc(src * sizeof(float));    //Выделение массива для паказаний датчиков
    name_mas_sec = (char **) malloc(src * sizeof(char *));    //Выделение массива указателей
    if (name_mas_sec == NULL)        //Проверка выдиления
    {
        syslog(LOG_EMERG,
               "Ошибка!\n Невозможно выдилить память.\n");
        exit(EXIT_FAILURE);
    }

    for (i = 0; i < src; i++) {
        name_mas_sec[i] = (char *) malloc(
                25 * sizeof(char));    //Выдиление массива char для каждого указателя(создание 2 мерного массива)

        if (name_mas_sec[i] != NULL) {
            memset(name_mas_sec[i], '\0', 25 * sizeof(char));    //Заполнение нулями массива
        }
    }

    c = rc;
    double unixtime = 0, e_hum = -100, e_temp = -100, e_fw = 0, e_lux = -100, e_b_temp = DSE;
    double e_mvc = -100, e_evc = DSE, e_pre = DSE, e_mctmp = DSE, e_vin = DSE, e_mq7 = DSE, e_mq9 = DSE, e_mq9l = DSE;
    double s_evc = 0, s_temp = 0, s_hum = 0, s_fw = 0, hum = -100, hi_temp = -100, t_temp = -100;
    double ups_v = -100, ups_load = -100, ups_frq = -100, ups_bat_stat = -100, ups_stat = DSE, error_rate_f = 0, error_rate_s = 0;
    int tmp_fw = 0;
    unsigned int errors_f = 0, errors_s = 0, cicles_f = 0, cicles_s = 0, error_now_f = 0, error_now_s = 0;
    char tst[256];
    bool lpg_warn = 0;
    char ds = '*';
    //int MSB, LSB, XLSB, timing, DEVICE_ADDRESS;
    //DEVICE_ADDRESS = 0x77;

    //fd = wiringPiI2CSetup (DEVICE_ADDRESS);
    //read_calibration_values (fd);

    syslog(LOG_NOTICE,
           "Данные успешно распознаны, сервис запущен.");
    //rc = rc - 1;
    FILE *HTML, *GIGASET_XML, *XML, *NAROD, *RTF, *RAW;
    init_usred(&sred, 20, 4);
    init_usred(&sred_main, 20, 13);
    init_usred(&sred_sec, 20, 3);
    while (1) {
        //sleep(10);
        //syslog (LOG_NOTICE, "rec: %s\n", rx);
        //syslog (LOG_NOTICE, "col: %d\n", rc);
        //syslog (LOG_NOTICE, "srec: %s\n", rx_s);
        //syslog (LOG_NOTICE, "scol: %d\n", src);
        if (stop == 1)
            break;
        secr = 0;
        //if (readDA (uart0_filestream, rx) == rc)
        if (get_a1pr_data("192.168.0.61", rx, RXL) == rc) {
            itime = time(NULL);
            Tm = localtime(&itime);
            //localtime_r (&itime, Tm);
            y = splint_rtoa(rx, RXL, rc, name_mas, dat_mas);
            // syslog (LOG_NOTICE, "SPLINT_RTOA: y=%d, rs=%d, rc=%d", y, rs, rc);
            e_temp = -100;
            e_hum = -100;
            e_mctmp = -100;
            e_pre = -100;
            e_b_temp = -100;
            e_mvc = -100;
            e_lux = -100;
            e_evc = -100;
            e_vin = -100;
            e_mq7 = -100;
            e_mq9 = -100;
            e_mq9l = -100;
            //ups_v= -100;
            //ups_load= -100;
            //ups_frq = -100;
            //ups_bat_stat = -100;
            ups_stat = get_ups_data("http://192.168.0.114:8000/ups.txt", &ups_v, &ups_load, &ups_frq, &ups_bat_stat);


            for (int errors = 0; errors < 5; errors++) {
                if (cicles_s >= UINT_MAX - 2 || errors_s >= UINT_MAX - errors) {
                    cicles_s = 0;
                    errors_s = 0;
                }
                cicles_s++;
                if (get_a1pr_data("192.168.0.89", rx_s, RXL) == src) {

                    error_now_s = errors;
                    errors_s += errors;

                    y = splint_rtoa(rx_s, RXL, src, name_mas_sec, dat_mas_sec);
                    //syslog (LOG_NOTICE, "SPLINT_RTOA: y=%d, rs=%d, rc=%d", y, RXL, src);
                    secr = 1;
                    s_evc = -100;
                    s_temp = -100;
                    s_hum = -100;
                    for (i = 0; i < src; i++) {
                        if (strcmp(name_mas_sec[i], "DTMP") == 0) {
                            s_temp = dat_mas_sec[i];
                        } else if (strcmp(name_mas_sec[i], "DHUM") == 0) {
                            s_hum = dat_mas_sec[i];
                        } else if (strcmp(name_mas_sec[i], "EVC") == 0) {
                            s_evc = dat_mas_sec[i];
                        } else if (strcmp(name_mas_sec[i], "FW") == 0) {
                            s_fw = dat_mas_sec[i];
                        }
                    }
                    write_usred(&sred_sec, 3, &s_temp, &s_hum, &s_evc);
                    break;
                }
                sleep(1);
            }
            if (ups_stat == 0) {
                ups_load = (ups_load / 100) * 375;
                write_usred(&sred, 4, &ups_frq, &ups_v, &ups_load,
                            &ups_bat_stat);
            }
            if (ups_stat != 0)
                syslog(LOG_WARNING,
                       "Не удаётся получить данные UPS");

            unixtime = time(NULL) / 1000.0;

            for (i = 0; i < rc; i++) {
                if (strcmp(name_mas[i], "DTMP") == 0) {
                    e_temp = dat_mas[i];
                } else if (strcmp(name_mas[i], "DHUM") == 0) {
                    e_hum = dat_mas[i];
                } else if (strcmp(name_mas[i], "BTMP") == 0) {
                    e_b_temp = dat_mas[i];
                } else if (strcmp(name_mas[i], "BPRE") == 0) {
                    e_pre = dat_mas[i];
                } else if (strcmp(name_mas[i], "LUX") == 0) {
                    e_lux = dat_mas[i];
                } else if (strcmp(name_mas[i], "MTMP") == 0) {
                    e_mctmp = dat_mas[i];
                } else if (strcmp(name_mas[i], "MVC") == 0) {
                    e_mvc = dat_mas[i];
                } else if (strcmp(name_mas[i], "EVC") == 0) {
                    e_evc = dat_mas[i];
                } else if (strcmp(name_mas[i], "VIN") == 0) {
                    e_vin = dat_mas[i];
                } else if (strcmp(name_mas[i], "MQ7") == 0) {
                    e_mq7 = dat_mas[i];
                } else if (strcmp(name_mas[i], "MQ9") == 0) {
                    e_mq9 = dat_mas[i];
                } else if (strcmp(name_mas[i], "MQ9L") == 0) {
                    e_mq9l = dat_mas[i];
                    if (e_mq9l >= 300) {
                        if (lpg_warn == 0) {
                            sprintf(tst, "echo '%d/%d/%d %d:%d:%d GAS LPG WARNING! VALUE:%f ppm ' | lpr -l -h ",
                                    Tm->tm_mday, Tm->tm_mon + 1, Tm->tm_year + 1900, Tm->tm_hour, Tm->tm_min,
                                    Tm->tm_sec, e_mq9l);
                            system(tst);
                            //system("date --rfc-2822 | lpr -l -h ");
                        }
                        lpg_warn = 1;
                    } else {
                        if (lpg_warn == 1) {
                            //sprintf(tst, "echo 'END GAS LPG WARNING NOW VALUE:%f ppm ' | lpr -l -h ", e_mq9l);
                            sprintf(tst, "echo '%d/%d/%d %d:%d:%d END GAS LPG WARNING NOW VALUE:%f ppm ' | lpr -l -h ",
                                    Tm->tm_mday, Tm->tm_mon + 1, Tm->tm_year + 1900, Tm->tm_hour, Tm->tm_min,
                                    Tm->tm_sec, e_mq9l);
                            system(tst);
                            //system("date --rfc-2822 | lpr -l -h ");
                        }
                        lpg_warn = 0;
                    }
                } else if (strcmp(name_mas[i], "FW") == 0) {
                    e_fw = dat_mas[i];
                }
            }
            if (e_temp == -100 || e_hum == -100) {
                sleep(20);
                syslog(LOG_WARNING,
                       "Не удаётса распознать строку %s, сброс...",
                       rx);
                for (i = 0; i < rc; i++) {
                    syslog(LOG_WARNING, "Переменная %s = %f",
                           name_mas[i], dat_mas[i]);
                    bzero(name_mas[i], 25);
                }
                bzero(rx, RXL);
                bzero(dat_mas, rc);
                //rc = readDA (uart0_filestream, rx);
                rc = get_a1pr_data("192.168.0.61", rx, RXL);
                continue;
                //exit(1);
            }
            hum = e_hum;
            t_temp = e_temp;
            //TODO!
            if (secr == 1) {
                if (e_temp > s_temp && s_temp != -100) {
                    t_temp = s_temp;
                }
                if (e_hum >= 90 || e_hum >= s_hum * 1.3) {
                    hum = s_hum;
                }
                if (cicles_s != 0) {
                    error_rate_s = (float) errors_s / cicles_s;
                }

            }
            hi_temp = computeHeatIndex(t_temp, hum);
            if (cicles_f != 0) {
                error_rate_f = (float) errors_f / cicles_f;
            }


	    /*Raw data output*/
	    bzero(raw_message, buffer_size);
            RAW = fopen("/usr/share/nginx/html/tmp/arduino_raw.txt", "w+");
            if (RAW == NULL) {
                syslog(LOG_WARNING, "Cannot Open arduino_raw.txt\n");
                exit(EXIT_FAILURE);
            }
            if (flock(fileno(RAW), LOCK_EX | LOCK_NB)) {
                syslog(LOG_WARNING, "arduino_raw.txt file is blocking\n");
                exit(EXIT_FAILURE);
            }
            if (ups_stat == 0) {
                fprintf(RAW, "ups_frq:%f ups_v:%f ups_load:%f ups_bat_stat:%f ",
                        ups_frq, ups_v, ups_load, ups_bat_stat);
                sprintf(raw_message, "ups_frq:%f ups_v:%f ups_load:%f ups_bat_stat:%f ",
                        ups_frq, ups_v, ups_load, ups_bat_stat);
            }
            if (hum != -100 && t_temp != -100) {
                sprintf(raw_message, "%sHUM:%f TEMP:%f ", raw_message,
                        hum, t_temp);
                fprintf(RAW, "HUM:%f TEMP:%f ",
                        hum, t_temp);
            }
            
            sprintf(raw_message, "%stime:%f %s ;", raw_message, itime / 100000.0, rx);
            fprintf(RAW, "time:%f %s ;", itime / 100000.0, rx);
            send_multicast("239.243.42.19", 6219, raw_message);
            //fseek (RAW, 0, SEEK_END);
            flock(fileno(RAW), LOCK_UN);
            fclose(RAW);



            //syslog (LOG_NOTICE, "y=%d\n", y);
            HTML = fopen("/usr/share/nginx/html/tmp/arduino.html", "w+");
            if (HTML == NULL) {
                syslog(LOG_NOTICE, "Cannot Open arduino.html\n");
                exit(EXIT_FAILURE);
            }

            if (flock(fileno(HTML), LOCK_EX | LOCK_NB)) {
                syslog(LOG_NOTICE, "arduino.html file is blocking\n");
                exit(EXIT_FAILURE);
            }
            fprintf(HTML,
                    "<!DOCTYPE HTML PUBLIC " "-//W3C//DTD HTML 4.01//EN" " "
                    "http://www.w3.org/TR/html4/strict.dtd"
                    ">\n<html>\n <head>\n  <meta http-equiv=" "Content-Type"
                    " content=" "text/html; charset=utf-8"
                    ">\n  <meta http-equiv=" "refresh" " content=" "5;"
                    "> \n  <title>KUIP data</title>\n </head>\n <body>\n");
            fprintf(HTML, "  <h1>HUM: %f </h1>\n", hum);
            fprintf(HTML, "  <h1>TEMP: %f </h1>\n", t_temp);
            fprintf(HTML, "  <h1>Hate Index: %f </h1>\n", hi_temp);
            for (i = 0; i < rc; i++) {
                fprintf(HTML, "  <h1>sens_%d %s: %f </h1>\n", i, name_mas[i],
                        dat_mas[i]);
            }
            if (secr == 1) {
                for (i = 0; i < src; i++) {
                    fprintf(HTML, "  <h1>s_sens_%d %s: %f </h1>\n", i, name_mas_sec[i],
                            dat_mas_sec[i]);
                }
            }
            if (ups_stat == 0) {
                fprintf(HTML,
                        "<h1>now ups_frq:%f \nups_v:%f \nups_load:%f \nups_bat_stat:%f \ndata_redy:%d \ndata_index:%u </h1>\n",
                        ups_frq, ups_v, ups_load, ups_bat_stat, sred.data_redy, sred.data_index);
            }
            fprintf(HTML, " </body>\n</html>");
            flock(fileno(HTML), LOCK_UN);
            fclose(HTML);

            //Gigaset screen sever XHTML gen
            GIGASET_XML = fopen("/usr/share/nginx/html/tmp/arduino.xml", "w+");
            if (GIGASET_XML == NULL) {
                syslog(LOG_WARNING, "Cannot Open arduino.xml\n");
                exit(EXIT_FAILURE);
            }
            if (flock(fileno(GIGASET_XML), LOCK_EX | LOCK_NB)) {
                syslog(LOG_WARNING, "arduino.xml file is blocking\n");
                exit(EXIT_FAILURE);
            }
            fprintf(GIGASET_XML,
                    "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<!DOCTYPE html PUBLIC \"-//OMA//DTD XHTML Mobile 1.2//EN\" \"http://www.openmobilealliance.org/tech/DTD/xhtmlmobile12.dtd\">\n<html xmlns=\"http://www.w3.org/1999/xhtml\">\n<head>\n <meta name=\"expires\" content=\"0.15\" /> \n <title>Out home</title>\n</head>\n<body>\n");
            for (i = 1; i < (rc - 2); i++) {
                sprintf(typec, "\0");
                if (strcmp(name_mas[i], "DTMP") == 0) {
                    //typec[0]=' ';
                    typec[0] = 'C';
                    typec[1] = '\0';
                    //sprintf (name_mas[i], "Temp");
                } else if (strcmp(name_mas[i], "DHUM") == 0) {
                    //sprintf(typec," ");
                    typec[0] = 37;
                    typec[1] = '\0';
                }
                //else
                //    {sprintf(typec, "\0");}
                fprintf(GIGASET_XML,    /*  <h1>sens_%d %s: %.2f </h1>\n  */
                        "  <p style=\"text-align:left\"> %s: %.2f  ",
                        name_mas[i], dat_mas[i]);
                fprintf(GIGASET_XML, "%s", typec);
                //syslog (LOG_NOTICE, "typec=%s", typec);
                fprintf(GIGASET_XML, "</p>\n");
                //if (i < (rc - 1))
                //fprintf (GIGASET_XML, "  <br/> \n");
            }
            fprintf(GIGASET_XML,
                    " <p style=\"text-align:left\"> Pre: %.2f  </p>\n", e_pre);
            fprintf(GIGASET_XML, " </body>\n</html>");
            //fseek (F, 0, SEEK_END);
            flock(fileno(GIGASET_XML), LOCK_UN);
            fclose(GIGASET_XML);

            if (e_temp > -40 && e_temp < 80 && hum > 0 && hum <= 100) {
                NAROD = fopen("/usr/share/nginx/html/tmp/arduino.txt", "w+");
                if (NAROD == NULL) {
                    syslog(LOG_NOTICE, "Cannot Open arduino.txt\n");
                    exit(EXIT_FAILURE);
                }
                if (flock(fileno(NAROD), LOCK_EX | LOCK_NB)) {
                    syslog(LOG_NOTICE, "arduino.txt file is blocking\n");
                    exit(EXIT_FAILURE);
                }
                write_usred(&sred_main, 13, &e_temp, &e_hum, &e_lux, &e_mctmp, &e_pre, &e_b_temp, &e_mvc, &e_vin,
                            &e_evc, &e_mq7, &e_mq9, &e_mq9l, &t_temp);
                fprintf(NAROD,
                        "#b8-27-eb-8d-59-05#KUIP#55.7241#37.8174#155\n#TempN#%f#Температура св (DHT21)\n#LUX#%f#Освещённость\n#MCTMP#%f#Темп ВБ1 МК\n",
                        e_temp, e_lux, e_mctmp);
                if (secr == 1) {
                    fprintf(NAROD, "#TempS#%f#Температура юг (DHT22)\n", s_temp);
                    fprintf(NAROD, "#HumS#%f#Влажность юг\n", s_hum);
                }
                fprintf(NAROD, "#TEMP#%f#Температура\n", t_temp);
                fprintf(NAROD, "#Hum#%f#Влажность\n", hum);
                fprintf(NAROD, "#HumE#%f#Влажность св\n", e_hum);
                fprintf(NAROD, "#PRE#%f#Давление (BMP180)\n", e_pre);
                /*if(ups_stat == 0 && sred.data_redy == 1) {
                   fprintf (NAROD, "#UPSFRQ#%f#Частота сети\n", sred.data_usred[0]);
                   fprintf (NAROD, "#UPSV#%f#Напряжение сети\n", sred.data_usred[1]);
                   fprintf (NAROD, "#UPSLW#%f#Нагрузка на ИБП 2\n", sred.data_usred[2]);
                   fprintf (NAROD, "#UPSBAT#%f#Заряд батареи ИБП 2\n", sred.data_usred[3]);
                   } */
                if (ups_stat == 0) {
                    fprintf(NAROD, "#UPSFRQ#%f#Частота сети\n", ups_frq);
                    fprintf(NAROD, "#UPSV#%f#Напряжение сети\n", ups_v);
                    fprintf(NAROD, "#UPSLW#%f#Нагрузка на ИБП 2\n", ups_load);
                    fprintf(NAROD, "#UPSBAT#%f#Заряд батареи ИБП 2\n", ups_bat_stat);
                }
                fprintf(NAROD, "#INTEMP#%f#Темп ВБ1\n", e_b_temp);
                fprintf(NAROD, "#MCVCC#%f#Напр ВБ1 МК\n", e_mvc);
                if (e_mq9l >= 0) {
                    fprintf(NAROD, "#LPG#%f#Концентрация LPG гор газов\n", e_mq9l);
                    fprintf(NAROD, "#CO#%f#Концентрация CO\n", e_mq7);
                }
                //fprintf (NAROD, "#%s#%f#%u\n", name_mas[i], dat_mas[i], (unsigned)time(NULL));
                fprintf(NAROD, "##");
                //fseek (NAROD, 0, SEEK_END);
                flock(fileno(NAROD), LOCK_UN);
                fclose(NAROD);
            }

            //RTF Generator
            RTF = fopen("/usr/share/nginx/html/tmp/ar_report.rtf", "w+");
            fprintf(RTF,
                    "{\\rtf1\\ansi\\ansicpg1251\\deff0\\deflang1049{\\fonttbl{\\f0\\fnil\\fprq2\\fcharset0 DS-Terminal;}}\n");
            fprintf(RTF,
                    "{\\*\\generator A1Template_base_gen 0.2 ;}"
                    "{\\*\\template kuip_t.c}"
                    "{\\title KUIP sensor data report}\n"
                    "{\\author %s_%d.%d.%d}{\\category Report}"
                    "{\\doccomm KUIP_Report}"
                    "{\\creatim\\yr%d\\mo%d\\dy%d\\hr%d\\min%d\\sec%d}"
                    "\\viewkind4\\uc1\\pard\\qc\\lang1033\\f0\\fs26 KUIP sensor data report\\par\n",
                    sw_name, sw_ver / 100, (sw_ver % 100) / 10, sw_ver % 10,
                    Tm->tm_year + 1900, Tm->tm_mon + 1, Tm->tm_mday, Tm->tm_hour, Tm->tm_min, Tm->tm_sec);
            fprintf(RTF, "%d/%d/%d %d:%d:%d\\par\n", Tm->tm_mday,
                    Tm->tm_mon + 1, Tm->tm_year + 1900, Tm->tm_hour,
                    Tm->tm_min, Tm->tm_sec);

            fprintf(RTF, "\\pard Main module:\\par\n");
            fprintf(RTF, " App ver: %d.%d.%d\\par\n", sw_ver / 100, (sw_ver % 100) / 10, sw_ver % 10);
            fprintf(RTF, " HW ver: %d.%d.%d\\par\n\\par\n", hw_ver / 100, (hw_ver % 100) / 10, hw_ver % 10);

            fprintf(RTF, "External module 1:\\par\n");
            fprintf(RTF, " Module type: esp8266, atmega328p-pu(base dev)\\par\n");
            tmp_fw = e_fw;
            ds = '*';
            fprintf(RTF, " Module FW ver: %d.%d.%d\\par\n", tmp_fw / 100, (tmp_fw % 100) / 10, tmp_fw % 10);
            //fprintf(RTF, "\\par\n");
            fprintf(RTF, " Temp(BMP180): %.3f%cC\\par\n", e_b_temp, ds);
            fprintf(RTF, " Pressure(BMP180): %.3fmm.Hg.\\par\n", e_pre);
            fprintf(RTF, " Temp (ATmega): %.3f%cC\\par\n", e_mctmp, ds);
            fprintf(RTF, " Module vin: %.3fV\\par\n", e_vin);
            fprintf(RTF, " Module VIN-5V: %.3fV\\par\n", e_mvc);
            fprintf(RTF, " Module 5V-3V: %.3fV\\par\n", e_evc);
            fprintf(RTF, " Light sensor(BH1750): %.3f Lux\\par\n", e_lux);
            fprintf(RTF, " Temp (DHT21): %.3f%cC\\par\n", e_temp, ds);
            fprintf(RTF, " Gas CO sensor(MQ7): %.3f ppm\\par\n", e_mq7);
            fprintf(RTF, " Gas CO sensor(MQ9): %.3f ppm\\par\n", e_mq9);
            fprintf(RTF, " Gas LPG sensor(MQ9): %.3f ppm\\par\n", e_mq9l);
            fprintf(RTF, " Humidity (DHT21): %.3f%c\\par\n\\par\n", e_hum, 37);

            if (secr == 1) {
                fprintf(RTF, "External module 2:\\par\n");
                fprintf(RTF, " Module type: esp8266(base dev)\\par\n");
                tmp_fw = s_fw;
                fprintf(RTF, " Module FW ver: %d.%d.%d\\par\n", tmp_fw / 100, (tmp_fw % 100) / 10, tmp_fw % 10);
                ds = '*';
                //fprintf(RTF, "\\par\n");
                fprintf(RTF, " Module VIN-3V: %.3fV\\par\n", s_evc);
                fprintf(RTF, " Temp (DHT22): %.3f%cC\\par\n", s_temp, ds);
                fprintf(RTF, " Humidity (DHT22): %.3f%c\\par\n\\par\n", s_hum, 37);
            }

            if (ups_stat == 0) {
                fprintf(RTF,
                        "Com server UPS:\\par\n Line Frequency: %.2fHz\\par\n",
                        ups_frq);
                fprintf(RTF, " Line Voltage: %.2fV\\par\n", ups_v);
                fprintf(RTF, " Load: %f%c\\par\n", ups_load, 37);
                fprintf(RTF, " Power cons: %.3fW\\par\n", ups_load);
                fprintf(RTF, " Bat charge: %.2f%c\\par\n\\par\n", ups_bat_stat,
                        37);
            }
            fprintf(RTF, "Summary:\\par\n");
            fprintf(RTF, " Humidity: %f\\par\n", hum);
            fprintf(RTF, " Temperature: %f\\par\n", t_temp);
            fprintf(RTF, " Hateindex: %f\\par\n\\par\n", hi_temp);

            fprintf(RTF, "Timestamp: %u\\par\n", (unsigned) itime);
            fprintf(RTF, "}\n\n");

            fclose(RTF);


            //TXT REPORT Generator
            RTF = fopen("/usr/share/nginx/html/tmp/ar_report.txt", "w+");
            fprintf(RTF,
                    "KUIP sensor data report\n");
            fprintf(RTF, " generate at %d/%d/%d %d:%d:%d\n\n", Tm->tm_mday,
                    Tm->tm_mon + 1, Tm->tm_year + 1900, Tm->tm_hour,
                    Tm->tm_min, Tm->tm_sec);

            fprintf(RTF, "Main module:\n");
            fprintf(RTF, " App ver: %d.%d.%d\n", sw_ver / 100, (sw_ver % 100) / 10, sw_ver % 10);
            fprintf(RTF, " HW ver: %d.%d.%d\n\n", hw_ver / 100, (hw_ver % 100) / 10, hw_ver % 10);

            fprintf(RTF, "External module 1:\n");
            fprintf(RTF, " Module type: esp8266, atmega328p-pu(base dev)\n");
            tmp_fw = e_fw;
            fprintf(RTF, " Module FW ver: %d.%d.%d\n", tmp_fw / 100, (tmp_fw % 100) / 10, tmp_fw % 10);
            //fprintf(RTF, " Module get errors: %d\n", error_now_f);
            fprintf(RTF, " Module get error: %.0f%c\n", error_rate_f * 100, 37);
            //fprintf(RTF, " Module get errors sum: %d\n", errors_f);
            //fprintf(RTF, " Module get cicles: %d\n", cicles_f);
            ds = '*';
            fprintf(RTF, "\n");
            fprintf(RTF, " Temp(BMP180): %.3f%cC\n", e_b_temp, ds);
            fprintf(RTF, " Pressure(BMP180): %.3fmm.Hg.\n", e_pre);
            fprintf(RTF, " Temp (ATmega): %.3f%cC\n", e_mctmp, ds);
            fprintf(RTF, " Module vin: %.3fV\n", e_vin);
            fprintf(RTF, " Module VIN-5V: %.3fV\n", e_mvc);
            fprintf(RTF, " Module 5V-3V: %.3fV\n", e_evc);
            fprintf(RTF, " Light sensor(BH1750): %.3f Lux\n", e_lux);
            fprintf(RTF, " Gas CO sensor(MQ7): %.3f ppm\n", e_mq7);
            fprintf(RTF, " Gas CO sensor(MQ9): %.3f ppm\n", e_mq9);
            fprintf(RTF, " Gas LPG sensor(MQ9): %.3f ppm\n", e_mq9l);
            fprintf(RTF, " Temp (DHT21): %.3f%cC\n", e_temp, ds);
            fprintf(RTF, " Humidity (DHT21): %.3f%c\n\n", e_hum, 37);

            if (secr == 1) {
                fprintf(RTF, "External module 2:\n");
                fprintf(RTF, " Module type: esp8266(base dev)\n");
                tmp_fw = s_fw;
                fprintf(RTF, " Module FW ver: %d.%d.%d\n", tmp_fw / 100, (tmp_fw % 100) / 10, tmp_fw % 10);
                //fprintf(RTF, " Module get errors: %d from 5\n", error_now_s);
                fprintf(RTF, " Module get error: %.0f%c\n", error_rate_s * 100, 37);
                //fprintf(RTF, " Module get errors sum: %d\n", errors_s);
                //fprintf(RTF, " Module get cicles: %d\n", cicles_s);
                fprintf(RTF, "\n");
                fprintf(RTF, " Module VIN-3V: %.3fV\n", s_evc);
                fprintf(RTF, " Temp (DHT22): %.3f%cC\n", s_temp, ds);
                fprintf(RTF, " Humidity (DHT22): %.3f%c\n\n", s_hum, 37);
            }

            if (ups_stat == 0) {
                fprintf(RTF,
                        "Com server UPS:\n Line Frequency: %.2fHz\n",
                        ups_frq);
                fprintf(RTF, " Line Voltage: %.2fV\n", ups_v);
                fprintf(RTF, " Load: %f%c\n", ups_load, 37);
                fprintf(RTF, " Power cons: %.3fW\n", ups_load);
                fprintf(RTF, " Bat charge: %.2f%c\n\n", ups_bat_stat,
                        37);
            }

            fprintf(RTF, "Summary:\n");
            fprintf(RTF, " Humidity: %f\n", hum);
            fprintf(RTF, " Temperature: %f\n", t_temp);
            fprintf(RTF, " Hateindex: %f\n\n", hi_temp);

            fprintf(RTF, "Timestamp: %u\n", (unsigned) itime);
            fprintf(RTF, "\n");
            fprintf(RTF, "\n");

            fclose(RTF);

            //Web XML gen
            XML = fopen("/usr/share/nginx/html/tmp/arduino_web.xml", "w+");
            if (XML == NULL) {
                syslog(LOG_NOTICE, "Cannot Open arduino_web.xml\n");
                exit(EXIT_FAILURE);
            }
            if (flock(fileno(XML), LOCK_EX | LOCK_NB)) {
                syslog(LOG_NOTICE, "arduino_web.xml file is blocking\n");
                exit(EXIT_FAILURE);
            }
            fprintf(XML,
                    "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<uart_kuip>\n <time>%u</time>\n <pre>%f</pre>\n <in_temp>%f</in_temp>\n <ups_frq>%f</ups_frq>\n <ups_v>%f</ups_v>\n <ups_load>%f</ups_load>\n <ups_bat_stat>%f</ups_bat_stat>\n",
                    (unsigned) time(NULL), e_pre, e_b_temp, ups_frq, ups_v,
                    ups_load, ups_bat_stat);

            //fprintf (XML, " <HUM>%.2f</HUM>\n", hum);
            fprintf(XML, " <Hum>%.2f</Hum>\n", hum);
            fprintf(XML, " <OT>%.2f</OT>\n", t_temp);
            fprintf(XML, " <HIOT>%.2f</HIOT>\n", hi_temp);
            for (i = 0; i < rc; i++) {
                fprintf(XML,    /*  <h1>sens_%d %s: %.2f </h1>\n  */
                        " <%s>%.2f", name_mas[i], dat_mas[i]);
                //syslog (LOG_NOTICE, "typec=%s", typec);B
                fprintf(XML, "</%s>\n", name_mas[i]);
            }
            if (secr == 1) {
                for (i = 0; i < src; i++) {
                    fprintf(XML,    /*  <h1>sens_%d %s: %.2f </h1>\n  */
                            " <S%s>%.2f", name_mas_sec[i], dat_mas_sec[i]);
                    //syslog (LOG_NOTICE, "typec=%s", typec);B
                    fprintf(XML, "</S%s>\n", name_mas_sec[i]);
                }
            }
            fprintf(XML, "</uart_kuip>");
            //fseek (F, 0, SEEK_END);
            flock(fileno(XML), LOCK_UN);
            fclose(XML);
            /*if(itc>=3) {
               sprintf(strtmp,"/usr/bin/curl \"http://192.168.0.113:82/objects/?object=OutSide0&op=m&m=dataChange&h=%.2f&t=%.2f&ai=%.2f&vcc=%.2f\" -m 2", hum, temp, ai, vcc);
               system(strtmp);
               sprintf(strtmp,"/usr/bin/curl \"http://192.168.0.113:82/objects/?object=int_sens0&op=m&m=dataChange&p=%.2f&t=%.2f\" -m 2", pre, in_temp);
               system(strtmp);
               itc=0;
               }
               itc++; */
            sleep(15);
            error_now_f = 0;
            //syslog (LOG_NOTICE, "\033[2J"); /* Clear the entire screen. */
            //syslog (LOG_NOTICE, "\033[0;0f"); /* Move cursor to the top left hand corner*/
        } else {
            error_now_f++;
            errors_f++;
        }

        if (cicles_f >= UINT_MAX - 2 || errors_f >= UINT_MAX - error_now_f) {
            cicles_f = 0;
            errors_f = 0;
        }

        cicles_f++;
    }
    //syslog (LOG_NOTICE, "Закрытие uart порта.");
    //close (uart0_filestream);
    //for (i = 0; i < y; i++)
    //{
    //      syslog (LOG_NOTICE, "sens_%d name: %s, data: %f\n", i, name_mas[i], dat_mas[i]);
    //}
    //syslog (LOG_NOTICE, "recev: %s",rx);
    syslog(LOG_NOTICE, "Высвобождение памяти.");
    for (i = 0; i < rc; i++) {
        free(name_mas[i]);
    }
    free_usred(&sred_main);
    free_usred(&sred_sec);
    free_usred(&sred);
    free(dat_mas_sec);
    free(name_mas_sec);
    free(dat_mas);
    free(name_mas);
    syslog(LOG_NOTICE,
           "Закрытие лога и завершение работы..");
    closelog();
    return EXIT_SUCCESS;
}

void stop_all() {
    if (stop == 1) {
        remove("/tmp/kuip_t.here");
        remove("/tmp/kuip_t.pid");
        syslog(LOG_CRIT, "Пробую завершится аварийно...");
        exit(10);
    }
    stop = 1;
    //int g;
    syslog(LOG_NOTICE, "Остановка сервиса...");
}

void em_dump() {
    syslog(LOG_CRIT, "Ошибка сегментирования, завершаю аварийно...");
    remove("/tmp/kuip_t.here");
    system("echo 'SEGFAIL AT ' | lpr -l -h ");
    system("date --rfc-2822 | lpr -l -h ");
    exit(20);
}

void usr_sig1() {
    FILE *TEMPF;
    syslog(LOG_NOTICE, "Получен сигнал SIGUSR1, отвечаю.");
    TEMPF = fopen("/tmp/kuip_t.here", "w+");
    if (TEMPF != NULL) fprintf(TEMPF, "%d", sizeof(int));
    fclose(TEMPF);
}