#ifndef __sysutil_hxx__
#define __sysutil_hxx__

#include <errno.h>
#include <stdexcept>
#include <string>

template <class T, std::size_t N>
constexpr std::size_t array_size(const T (&array)[N]) noexcept {
  return N;
}

class SystemException : public std::runtime_error {
public:
  SystemException(const char* msg, int errnum) : std::runtime_error(msg), errno_(errnum) {}

  ~SystemException() noexcept {}

  virtual const char* what() const noexcept {
    return (std::string(std::runtime_error::what()) + ": " + std::string(strerror(errno_))).c_str();
  }

private:
  std::string msg_;
  const int errno_;
};

std::string get_current_dir() {
  char name[4096] = {};
  if (getcwd(name, sizeof(name)) == NULL) throw SystemException("getcwd failed", errno);

  return std::string(name);
}

void change_dir(const std::string& path) {
  if (chdir(path.c_str()) != 0) throw SystemException("chdir failed", errno);
}

void ensure_mount_point(const char* mount_point) {
  if (mkdir(mount_point, 0777) == -1)
    if (errno != EEXIST) throw SystemException("Invalid mount point", errno);
}

FILE* open_file(const char* filename, const char* mode) {
  FILE* fp = fopen(filename, mode);
  auto message = std::string("Cannot open file: ") + filename;
  if (fp == nullptr) throw SystemException(message.c_str(), errno);
  return fp;
}

#endif  // include guard