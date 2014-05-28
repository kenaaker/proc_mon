
#include <iostream>
#include <stdio.h>
#include <unistd.h>
#include <sys/sysinfo.h>
#include <string.h>

#include <proc/readproc.h>
#include <proc/wchan.h>

#include <map>
#include <getopt.h>
#include <string>
#include "pstream.h"

typedef struct xfer_hdr {
    unsigned short xfer_len;
    unsigned int proc_info_offset;
    unsigned short proc_info_len;
    unsigned int cmdline_offset;
    unsigned short cmdline_len;
    unsigned short wchan_offset;
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
  double uptime_secs;
  unsigned long long seconds;
  used_jiffies = proc_r.utime + proc_r.stime;

  seconds = seconds_since_boot - proc_r.start_time / Hertz;
  if (seconds > 0) {
      pcpu = (used_jiffies * 1000ULL / Hertz) / seconds;
  } /* endif */

  proc_r.pcpu = pcpu;
}

int local_mon(string cmd_name) {
    PROCTAB *proc = openproc(PROC_FILLMEM | PROC_FILLSTAT | PROC_FILLWCHAN | PROC_FILLCOM);
    proc_t proc_info = {};

    cout << "host and command\t" << "state\t" << "\tCPU utilization\t" << "Resident memory pages\t" << "user mode CPU time\t" << "kernel mode CPU time" << endl;
    while(readproc(proc, &proc_info) != NULL) {
        string proc_name(proc_info.cmd);
        if (proc_name == cmd_name) {
            calculate_PCPU(proc_info);
            cout << "localhost " << proc_info.cmd << "\t"<< proc_info.state << "\t\t\t" << proc_info.pcpu << "\t\t" << proc_info.resident << "\t\t\t" << proc_info.utime << "\t\t\t" << proc_info.stime << endl;
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
        xfp = xfer_t_alloc(cmd_line_len, strnlen(wchan_name,64));
        if (!xfp) {
            abort();
        } else {
            xfp->proc_info = tmp_proc_t;
            string proc_name(xfp->proc_info.cmd);
            if (proc_name == cmd_name) {
                calculate_PCPU(xfp->proc_info);
                strncpy(&xfp->strings[xfp->xfer_hdr.cmdline_offset], *tmp_proc_t.cmdline, cmd_line_len);
                strncpy(&xfp->strings[xfp->xfer_hdr.wchan_offset], wchan_name, xfp->xfer_hdr.wchan_len);
                cout.write((char *)xfp, xfp->xfer_hdr.xfer_len);
                ++entries_found;
            } else {
                // cout << "Not interested in " << proc_info.cmd << endl;
            } /* endif */
            free(xfp);
            xfp = NULL;
        } /* endif */
    } /* endwhile */
    return 0;
} /* binary_mon */

int remote_mon(string host_name, string cmd_name) {
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
    cout << "host and command\t" << "state\t" << "CPU utilization\t" << "Resident memory pages\t" << "user mode CPU time\t" << "kernel mode CPU time" << endl;
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
                in.read((char*)&xfp->proc_info, xfp->xfer_hdr.xfer_len-sizeof(xfp_header));
//                cout << "xfer len is " << xfp->xfer_hdr.xfer_len << endl;
                if (xfp->xfer_hdr.proc_info_len != sizeof(xfp->proc_info)) {
                    cerr << " Remote report structure size is not what I expect, quitting." << endl;
                    exit(1);
                } /* endif */
                string proc_name(xfp->proc_info.cmd);
                if (proc_name == cmd_name) {
                    cout << host + " " + xfp->proc_info.cmd << "\t\t  " << xfp->proc_info.state << "\t\t" << xfp->proc_info.pcpu << "\t\t" << xfp->proc_info.resident << "\t\t\t" << xfp->proc_info.pcpu << "\t\t\t" << xfp->proc_info.stime << endl;
                } else {
                    // cout << "Not interested in " << proc_info.cmd << endl;
                } /* endif */
                free(xfp);
                xfp = NULL;
            } /* endif */
        } /* endif */
    } /* endwhile */

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

    Hertz = sysconf(_SC_CLK_TCK);
    sysinfo(&this_sys_info);
    seconds_since_boot = this_sys_info.uptime;

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

