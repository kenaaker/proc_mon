
#include <iostream>

#include <proc/readproc.h>
#include <map>
#include <getopt.h>
#include <string>

using namespace std;

int main(int argc, char *argv[], char *env[]) {

    PROCTAB *proc = openproc(PROC_FILLMEM | PROC_FILLSTAT | PROC_FILLWCHAN | PROC_FILLCOM);
    proc_t proc_info = {};
    string cmd_name;
    string lalala;
    char ch;

    static struct option long_options[] = {
        { "procname", required_argument, NULL, 'p' },
        { "lalala", optional_argument, NULL, 'l' }
    }; /* struct option */

    /* loop over all of the options */
    while ((ch = getopt_long(argc, argv, "t:a:", long_options, NULL)) != -1) {
        switch (ch) {    /* check to see if a single character or long option came through */
             case 'p':
                 cmd_name = string(optarg);
                 break;
             case 'l':
                 lalala = string(optarg);
                 break;
        } /* endswitch */
    } /* endwhile */
    while(readproc(proc, &proc_info) != NULL) {
        string proc_name(proc_info.cmd);
        if (proc_name == cmd_name) {
            cout << proc_info.cmd << " \t" << proc_info.resident << " \t" << proc_info.utime << " \t" << proc_info.stime << endl;
        } else {
            // cout << "Not interested in " << proc_info.cmd << endl;
        } /* endif */
    } /* endwhile */
    return 0;

}

