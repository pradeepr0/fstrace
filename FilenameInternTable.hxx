#ifndef __FilenameInternTable_hxx__
#define __FilenameInternTable_hxx__



#include <sys/types.h>
#include <vector>
#include <string>
#include <unordered_map>


class FilenameInternTable
{
public:
    typedef std::vector<std::string>::size_type id_type;

    const id_type id(const std::string& fname)
    {
        if (id_lookup_.count(fname) == 0) /* new filename */ {
            table_.push_back(fname);
            id_lookup_[fname] = table_.size() - 1;
        }

        return id_lookup_.at(fname);
    }

    const id_type id(const std::string& fname) const
    {
        return id_lookup_.at(fname);
    }

    const std::string& name(const id_type& id) const
    {
        return table_[id];
    }

    const std::vector<std::string>::size_type size() const
    {
        return table_.size();
    }

private:
    std::unordered_map<std::string, id_type> id_lookup_;
    std::vector<std::string> table_;
};



#endif // include guard