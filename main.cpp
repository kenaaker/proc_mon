
#include <iostream>
#include <stdio.h>
#include <unistd.h>
#include <sys/sysinfo.h>
#include <string.h>
#include <list>

#include <proc/readproc.h>
#include <proc/wchan.h>

#include <map>
#include <getopt.h>
#include <string>
#include "pstream.h"

typedef struct xfer_hdr {
    unsigned int proc_info_offset;
    unsigned int cmdline_offset;
    unsigned int wchan_offset;
    unsigned short xfer_len;
    unsigned short proc_info_len;
    unsigned short cmdline_len;
    unsigned short wchan_len;
} xfer_hdr_t;

typedef struct xfer_struct {
    xfer_hdr_t xfer_hdr;
    proc_t proc_info;
    char strings[1];                /* This actually gets allocated large enough to hold the command line, wchan and any other strings */
} xfer_t;

static inline xfer_t* xfer_t_alloc(int cline_len, int wc_len) {
    unsigned int this_len = sizeof(xfer_t) + sizeof(proc_t)+cline_len+wc_len;
  xfer_t *ret = (xfer_t *)calloc(this_len, 1);
  if (ret) {
      memset(ret, 0, this_len);
      ret->xfer_hdr.xfer_len = (sizeof(xfer_t)+cline_len+wc_len);
      ret->xfer_hdr.proc_info_offset = offsetof(xfer_t, proc_info);
      ret->xfer_hdr.proc_info_len=sizeof(proc_t);
      ret->xfer_hdr.cmdline_offset = 0;
      ret->xfer_hdr.cmdline_len = cline_len;
      ret->xfer_hdr.wchan_offset = cline_len;
      ret->xfer_hdr.wchan_len = wc_len;
  } /* endif */
  return ret;
} /* xfer_t_alloc */

static long Hertz;                  /* Used to translate jiffies to seconds */
struct sysinfo this_sys_info;
unsigned long long seconds_since_boot;
using namespace std;

static void calculate_PCPU(proc_t &proc_r){
  unsigned long long used_jiffies;
  unsigned long pcpu = 0;
  unsigned long long seconds;

  used_jiffies = proc_r.utime + proc_r.stime;
  seconds = seconds_since_boot - proc_r.start_time / Hertz;
  if (seconds > 0) {
      pcpu = (used_jiffies * 1000ULL / Hertz) / seconds;
  } /* endif */

  proc_r.pcpu = pcpu;
}

int local_mon(string cmd_name, bool do_header=false) {
    PROCTAB *proc = openproc(PROC_FILLMEM | PROC_FILLSTAT | PROC_FILLSTATUS |PROC_FILLWCHAN | PROC_FILLCOM);
    proc_t proc_info = {};

    if (do_header) {
        cout << "host and command\t" << "state\t" << "CPU utilization\t" << "Resident memory pages\t" << "user mode CPU time\t" << "kernel mode CPU time" << endl;
    } /* endif */
    while(readproc(proc, &proc_info) != NULL) {
        string proc_name(proc_info.cmd);
        if (proc_name == cmd_name) {
            string host;
            calculate_PCPU(proc_info);
            host = "localhost";
            host.resize(8,' ');
            string prt_cmd = proc_info.cmd;
            prt_cmd.resize(8,' ');
            cout << host + " " + prt_cmd  << "\t  "<< proc_info.state << "\t\t" << (float)proc_info.pcpu / 10.0 << "%\t\t" << proc_info.resident << "\t\t\t" << proc_info.utime << "\t\t\t" << proc_info.stime << endl;
        } else {
            // cout << "Not interested in " << proc_info.cmd << endl;
        } /* endif */
    } /* endwhile */
    return 0;
} /* local_mon */

int binary_mon(string cmd_name) {
    PROCTAB *proc = openproc(PROC_FILLMEM | PROC_FILLSTAT | PROC_FILLSTATUS | PROC_FILLCOM | PROC_FILLWCHAN);
    struct xfer_struct *xfp;
    struct proc_t tmp_proc_t = { 0 };
    int entries_found = 0;

    xfp = NULL;
    while(readproc(proc, &tmp_proc_t) != NULL) {
        const char *wchan_name;
        int cmd_line_len;
        wchan_name = lookup_wchan(tmp_proc_t.wchan, tmp_proc_t.tid);
        if (tmp_proc_t.cmdline != NULL) {
            cmd_line_len = strnlen(*tmp_proc_t.cmdline,1024);
        } else {
            cmd_line_len = 0;
        } /* endif */
        string proc_name(tmp_proc_t.cmd);
        if (proc_name == cmd_name) {
            xfp = xfer_t_alloc(cmd_line_len, strnlen(wchan_name,64));
            if (!xfp) {
                abort();
            } else {
                xfp->proc_info = tmp_proc_t;
                calculate_PCPU(xfp->proc_info);
                strncpy(&xfp->strings[xfp->xfer_hdr.cmdline_offset], *tmp_proc_t.cmdline, cmd_line_len);
                strncpy(&xfp->strings[xfp->xfer_hdr.wchan_offset], wchan_name, xfp->xfer_hdr.wchan_len);
                cout.write((char *)xfp, xfp->xfer_hdr.xfer_len);
                ++entries_found;
                free(xfp);
                xfp = NULL;
            } /* endif */
        } else {
            // cout << "Not interested in " << proc_info.cmd << endl;
        } /* endif */
    } /* endwhile */
    return 0;
} /* binary_mon */

int remote_mon(string host_name, string cmd_name, bool do_header=false) {
    // print names of all header files in current directory
    size_t dot_found = host_name.find('.');
    string host;

    struct xfer_struct *xfp;
    redi::ipstream in("/usr/bin/ssh -x " + host_name + " ./bin/proc_mon -b -p " + cmd_name,
                      redi::pstreams::pstdout | redi::pstreams::pstderr);

    if (dot_found != std::string::npos) {
        host = host_name.substr(0,dot_found);
    } else {
        host = host_name;               /* No dot found, just use the name */
    }
    if (do_header) {
        cout << "host and command\t" << "state\t" << "CPU utilization\t" << "Resident memory pages\t" << "user mode CPU time\t" << "kernel mode CPU time" << endl;
    } /* endif */
    while (!in.eof()) {
        xfer_hdr_t xfp_header;

        in.read((char*)&xfp_header, sizeof(xfp_header));
        if (in.eof()) {
            bool theres_errout = false;
            /* ssh ended, check for an error message */
            string err_line;
            in.clear();
            in.err();
            while(!in.eof()) {
                getline(in, err_line);
                if (!in.eof()) {
                    if (!theres_errout) {
                        cerr << "\t\t\tError output from ssh" << endl;
                        theres_errout = true;
                    } /* endif */
                    cerr << err_line << endl;
                } /* endif */
            } /* endwhile */
        } else {
            xfp = xfer_t_alloc(xfp_header.cmdline_len, xfp_header.wchan_len);
            if (!xfp) {
                abort();
            } else {
                xfp->xfer_hdr = xfp_header;
                in.read((char*)xfp + sizeof(xfp_header), xfp->xfer_hdr.xfer_len-sizeof(xfp_header));
                if (in.eof()) {
                    abort();
                }
//                cout << "xfer len is " << xfp->xfer_hdr.xfer_len << " bytes_read = " << xfp->xfer_hdr.xfer_len-sizeof(xfp_header) << endl;
                if (xfp->xfer_hdr.proc_info_len != sizeof(xfp->proc_info)) {
                    cerr << " Remote report structure size is not what I expect, quitting." <<
                         " Remote struct size " <<  xfp->xfer_hdr.proc_info_len <<
                         " local struct size " <<  sizeof(xfp->proc_info) << endl;
                    exit(1);
                } /* endif */
                string proc_name(xfp->proc_info.cmd);
//                cout << "xfer cmd name " << proc_name << " cmd name is " << cmd_name << endl;
                if (proc_name == cmd_name) {
                    host.resize(8,' ');
                    string prt_cmd = xfp->proc_info.cmd;
                    prt_cmd.resize(8,' ');
                    cout << host + " " + prt_cmd << "\t  " << xfp->proc_info.state << "\t\t" << (float)xfp->proc_info.pcpu / 10.0 << "%\t\t" << xfp->proc_info.resident << "\t\t\t" << xfp->proc_info.utime << "\t\t\t" << xfp->proc_info.stime << endl;
                } else {
                    // cout << "Not interested in " << proc_info.cmd << endl;
                } /* endif */
                free(xfp);
                xfp = NULL;
            } /* endif */
        } /* endif */
    } /* endwhile */
    return 0;
} /* remote_mon */

int main(int argc, char *argv[], char *env[]) {
    list<string> cmd_hostpairs;                             // List of commands @ hosts to monitor
    string cmd_name;
    string host_name;
    string lalala;
    char ch;
    bool bin_mon = false;
    size_t at_found;

    static struct option long_options[] = {
        { "binary", no_argument, NULL, 'b' },               // Output proc entries in binary for remote
        { "procname", required_argument, NULL, 'p' },       // Process/command@hostname name to look for multiples are acceptable
        { "lalala", optional_argument, NULL, 'l' },         // (Fingers in ears)
        { 0,0,0,0}
    }; /* struct option */

    /* loop over all of the options */
    while ((ch = getopt_long(argc, argv, "bh:p:l:", long_options, NULL)) != -1) {
        switch (ch) {    /* check to see if a single character or long option came through */
             case 'b':
                 bin_mon = true;
                 break;
            case 'p':
                cmd_hostpairs.push_back(string(optarg));
                break;
             case 'l':
                 lalala = string(optarg);
                 break;
        } /* endswitch */
    } /* endwhile */

    Hertz = sysconf(_SC_CLK_TCK);
    sysinfo(&this_sys_info);
    seconds_since_boot = this_sys_info.uptime;
    bool do_header = true;
    for (string &this_cmd : cmd_hostpairs) {
        at_found = this_cmd.find('@');
        if (at_found == string::npos) {
            host_name = "";
        } else {
            host_name = this_cmd.substr(at_found+1);
        } /* endif */
        cmd_name = this_cmd.substr(0, at_found);
//        cerr << "this_cmd = " << this_cmd << " host_name = " << host_name << " cmd = " << cmd_name << endl;
        if (host_name.empty()) { /* Assume a local monitor */
            if (bin_mon) {
                binary_mon(cmd_name);
            } else {
                local_mon(cmd_name, do_header);
            } /* endif */
        } else {
            /* Do a remote monitor by running an ssh command to the host */
            remote_mon(host_name, cmd_name, do_header);
        } /* endif */
        do_header = false;
    } /* endfor */

    return 0;

}

