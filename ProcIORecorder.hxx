#ifndef __ProcIORecorder_hxx__
#define __ProcIORecorder_hxx__


#include <sys/types.h>
#include <vector>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <memory>

#include "FilenameInternTable.hxx"



class ProcIORecorder
{
    typedef FilenameInternTable::id_type file_id_type;

public:
    void record_process_input(const pid_t& pid, const char *fname)
    {
        get_node(pid).infile_ids.insert(ftable_.id(fname));
    }

    void record_process_output(const pid_t& pid, const char *fname)
    {
        get_node(pid).outfile_ids.insert(ftable_.id(fname));
    }

    const FilenameInternTable& get_filename_table() const
    {
        return ftable_;
    }

    typedef std::unordered_multimap<file_id_type, file_id_type> dependency_map_type;

    void populate_dependency_map(dependency_map_type &depmap) const
    {
        for (auto const &r : records_)
            for (auto const &ofid : r.second->outfile_ids)
                for (auto const &ifid : r.second->infile_ids)
                    depmap.insert(std::make_pair(ofid, ifid));
    }

private:
    struct ProcIORecord
    {
        const pid_t pid;
        std::unordered_set<unsigned> infile_ids;
        std::unordered_set<unsigned> outfile_ids;

        ProcIORecord(const pid_t& procid)
            : pid(procid)
        {}
    };

    std::unordered_map<pid_t, std::unique_ptr<ProcIORecord>> records_;
    FilenameInternTable ftable_;

    ProcIORecord& get_node(const pid_t& pid)
    {
        if (records_.count(pid) == 0) /* new process id */ {
            std::unique_ptr<ProcIORecord> pnode( new ProcIORecord(pid) );
            records_.insert(std::make_pair(pid, std::move(pnode)));
        }

        return *records_[pid];
    }
};



#endif // include guard