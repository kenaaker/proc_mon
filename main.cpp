
#include <iostream>
#include <stdio.h>

#include <proc/readproc.h>
#include <map>
#include <getopt.h>
#include <string>
#include "pstream.h"

struct xfer_struct {
    unsigned short xfer_len;
    proc_t proc_info;
};

using namespace std;

int local_mon(string cmd_name) {

    PROCTAB *proc = openproc(PROC_FILLMEM | PROC_FILLSTAT | PROC_FILLWCHAN | PROC_FILLCOM);
    proc_t proc_info = {};

    while(readproc(proc, &proc_info) != NULL) {
        string proc_name(proc_info.cmd);
        if (proc_name == cmd_name) {
            cout << proc_info.cmd << " \t" << proc_info.resident << " \t" << proc_info.utime << " \t" << proc_info.stime << endl;
        } else {
            // cout << "Not interested in " << proc_info.cmd << endl;
        } /* endif */
    } /* endwhile */
    return 0;
} /* local_mon */

int binary_mon(string cmd_name) {

    PROCTAB *proc = openproc(PROC_FILLMEM | PROC_FILLSTAT | PROC_FILLWCHAN | PROC_FILLCOM);
    struct xfer_struct xfp;
    int entries_found = 0;

    xfp.xfer_len = sizeof(xfp.proc_info);
    while(readproc(proc, &xfp.proc_info) != NULL) {
        string proc_name(xfp.proc_info.cmd);
        if (proc_name == cmd_name) {
            cout.write((char *)&xfp, sizeof(xfp));
            ++entries_found;
        } else {
            // cout << "Not interested in " << proc_info.cmd << endl;
        } /* endif */
    } /* endwhile */
    cerr << entries_found << "entries for " << cmd_name << "found." << endl;
    return 0;
} /* local_mon */

int remote_mon(string host_name, string cmd_name) {
    // print names of all header files in current directory
    struct xfer_struct xfp;
    redi::ipstream in("/usr/bin/ssh -x " + host_name + " ./bin/proc_mon -b -p " + cmd_name,
                      redi::pstreams::pstdout | redi::pstreams::pstderr);

    while (!in.eof()) {
        in.read((char*)&xfp, sizeof(xfp));
        cout << "xfer len is " << xfp.xfer_len << " local proc_t len is " << sizeof(xfp.proc_info) << endl;
        string proc_name(xfp.proc_info.cmd);
        if (proc_name == cmd_name) {
            cout << xfp.proc_info.cmd << " \t" << xfp.proc_info.resident << " \t" << xfp.proc_info.utime << " \t" << xfp.proc_info.stime << endl;
        } else {
            // cout << "Not interested in " << proc_info.cmd << endl;
        } /* endif */
    }

//    cout << "remote_mon not done yet " << endl;
    return 0;
} /* remote_mon */

int main(int argc, char *argv[], char *env[]) {
    string cmd_name;
    string host_name;
    string lalala;
    char ch;
    bool bin_mon = false;

    static struct option long_options[] = {
        { "binary", no_argument, NULL, 'b' },               // Output proc entries in binary for remote
        { "hostname", required_argument, NULL, 'h' },       // Hostname to ssh to for remote monitor
        { "procname", required_argument, NULL, 'p' },       // Process/command name to look for
        { "lalala", optional_argument, NULL, 'l' },         // (Fingers in ears)
        { 0,0,0,0}
    }; /* struct option */

    /* loop over all of the options */
    while ((ch = getopt_long(argc, argv, "bh:p:l:", long_options, NULL)) != -1) {
        switch (ch) {    /* check to see if a single character or long option came through */
             case 'b':
                 bin_mon = true;
                 break;
            case 'h':
                host_name = string(optarg);
                break;
            case 'p':
                cmd_name = string(optarg);
                break;
             case 'l':
                 lalala = string(optarg);
                 break;
        } /* endswitch */
    } /* endwhile */

    if (host_name.empty()) { /* Assume a local monitor */
        if (bin_mon) {
            binary_mon(cmd_name);
        } else {
            local_mon(cmd_name);
        } /* endif */
    } else {
        /* Do a remote monitor by running an ssh command to the host */
        remote_mon(host_name, cmd_name);
    } /* endif */

    return 0;

}

