#ifndef __ProcessTree_hxx__
#define __ProcessTree_hxx__



#include <unordered_map>

#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>


static pid_t _ppid_from_proc_stat(const pid_t pid)
{
    char buf[256] = {};
    sprintf(buf, "/proc/%d/stat", pid);

    FILE* fp = fopen(buf, "r");
    if (fp == NULL)
        return -errno;

    // the stat pseudofile under /proc/#procid/ contains
    // pid{%d} command{%s} state{%c} ppid{%d} ...
    char* _ = buf;
    pid_t ppid = -1;
    fscanf(fp, "%s %s %s %d", _, _, _, &ppid);
    fclose(fp);

    return ppid;
}


class ProcessTree
{
public:
    const pid_t ppidof(const pid_t &pid) const noexcept
    {
        try {
            if (cache_.count(pid) == 0)
                cache_[pid] = _ppid_from_proc_stat(pid);

            return cache_[pid];
        }
        catch (...) {
            return -1;
        }
    }

    bool is_ancestor(const pid_t &aid, pid_t pid) const
    {
        while (pid != 0 && pid != aid)
            pid = ppidof(pid);

        return pid == aid;
    }

private:
    mutable std::unordered_map<pid_t, pid_t> cache_;
};



#endif // include guard