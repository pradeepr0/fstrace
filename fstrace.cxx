/**
 * A passthrough FUSE filesystem that traces `open` and `create` calls
 * adapted from the libfuse example fusexmp_fh.c
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FUSE_USE_VERSION 26
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <fuse.h>
#include <pthread.h>
#include <signal.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/xattr.h>
#include <unistd.h>
#include <set>
#include <string>

extern "C" {
#include <ulockmgr.h>
}

#include "sysutil.hxx"

class IntrofsState {
public:
  IntrofsState(pid_t delegate_pid_, const char* logfilename, const char* mount_point_)
      : delegate_pid(delegate_pid_), logfp(openFile(logfilename, "w")), mount_point(mount_point_) {}

  ~IntrofsState() { fclose(logfp); }

  const pid_t delegate_pid;
  FILE* logfp;
  const std::string mount_point;
  std::set<std::string> links;
  std::set<std::string> ifiles;
  std::set<std::string> ofiles;
};

static IntrofsState* getFuseContextData() {
  struct fuse_context* context = fuse_get_context();
  if (context == nullptr) {
    fprintf(stderr, "NULL fuse context!");
    exit(-1);
  }
  return static_cast<IntrofsState*>(fuse_get_context()->private_data);
}

static int logPrintf(const char* format, ...) noexcept {
  va_list args;
  va_start(args, format);
  int n = vfprintf(getFuseContextData()->logfp, format, args);
  va_end(args);
  return n;
}

#define RETURN_ON_ERROR(x)        \
  do {                            \
    if ((x) == -1) return -errno; \
  } while (0)

//
// FUSE delegate definitions
//
namespace introfs {

static void* init(struct fuse_conn_info* conn) noexcept {
  IntrofsState* state = getFuseContextData();
  sigqueue(state->delegate_pid, SIGUSR2, sigval());
  return state;
}

static void destroy(void* pdata) noexcept {
  try {
    IntrofsState* state = getFuseContextData();
    for (const auto& path : state->links) logPrintf("L\t%s\n", path.c_str());
    for (const auto& path : state->ifiles) logPrintf("R\t%s\n", path.c_str());
    for (const auto& path : state->ofiles) logPrintf("W\t%s\n", path.c_str());
  } catch (...) {
  }
}

static int getattr(const char* path, struct stat* stbuf) {
  RETURN_ON_ERROR(::lstat(path, stbuf));
  return 0;
}

static int fgetattr(const char* /*path*/, struct stat* stbuf, struct fuse_file_info* fi) {
  RETURN_ON_ERROR(::fstat(fi->fh, stbuf));
  return 0;
}

static int access(const char* path, int mask) {
  RETURN_ON_ERROR(::access(path, mask));
  return 0;
}

static int readlink(const char* path, char* buf, size_t size) {
  auto* state = getFuseContextData();

  state->links.insert(path);
  int res = ::readlink(path, buf, size - 1);
  if (res == -1) return -errno;

  std::string redirected;
  if (buf[0] == '/') {  // absolute link
    redirected = state->mount_point + std::string(buf, res);
  } else {  // relative link
    const auto& dirname = [](const std::string& path) { return path.substr(0, path.rfind('/')); };
    redirected = state->mount_point + dirname(path).c_str() + "/" + std::string(buf, res);
  }

  if (size < (redirected.size() + 1)) {
    errno = ENAMETOOLONG;
    return -1;
  }
  memcpy(buf, redirected.c_str(), redirected.size());
  buf[redirected.size()] = '\0';

  return 0;
}

static int opendir(const char* path, struct fuse_file_info* fi) {
  DIR* dp = ::opendir(path);
  if (dp == nullptr) return -errno;

  fi->fh = reinterpret_cast<unsigned long>(dp);
  return 0;
}

static int readdir(
    const char* /*path*/,
    void* buf,
    fuse_fill_dir_t filler,
    off_t offset,
    struct fuse_file_info* fi) {
  DIR* dp = reinterpret_cast<DIR*>(fi->fh);
  ::seekdir(dp, offset);

  struct dirent* de;
  while ((de = ::readdir(dp)) != nullptr) {
    struct stat st = {};
    st.st_ino = de->d_ino;
    st.st_mode = de->d_type << 12;

    if (filler(buf, de->d_name, &st, ::telldir(dp))) break;
  }
  return 0;
}

static int releasedir(const char* /*path*/, struct fuse_file_info* fi) {
  ::closedir(reinterpret_cast<DIR*>(fi->fh));
  return 0;
}

static int mknod(const char* path, mode_t mode, dev_t rdev) {
  RETURN_ON_ERROR((S_ISFIFO(mode)) ? (::mkfifo(path, mode)) : (::mknod(path, mode, rdev)));
  return 0;
}

static int mkdir(const char* path, mode_t mode) {
  RETURN_ON_ERROR(::mkdir(path, mode));
  return 0;
}

static int unlink(const char* path) {
  RETURN_ON_ERROR(::unlink(path));
  return 0;
}

static int rmdir(const char* path) {
  RETURN_ON_ERROR(::rmdir(path));
  return 0;
}

static int symlink(const char* from, const char* to) {
  RETURN_ON_ERROR(::symlink(from, to));
  return 0;
}

static int rename(const char* from, const char* to) {
  RETURN_ON_ERROR(::rename(from, to));
  return 0;
}

static int link(const char* from, const char* to) {
  RETURN_ON_ERROR(::link(from, to));
  return 0;
}

static int chmod(const char* path, mode_t mode) {
  RETURN_ON_ERROR(::chmod(path, mode));
  return 0;
}

static int chown(const char* path, uid_t uid, gid_t gid) {
  RETURN_ON_ERROR(::lchown(path, uid, gid));
  return 0;
}

static int truncate(const char* path, off_t size) {
  RETURN_ON_ERROR(::truncate(path, size));
  return 0;
}

static int ftruncate(const char* /*path*/, off_t size, struct fuse_file_info* fi) {
  RETURN_ON_ERROR(::ftruncate(fi->fh, size));
  return 0;
}

static int utimens(const char* path, const struct timespec ts[2]) {
  struct timeval tv[2] = {};
  tv[0].tv_sec = ts[0].tv_sec;
  tv[0].tv_usec = ts[0].tv_nsec / 1000;
  tv[1].tv_sec = ts[1].tv_sec;
  tv[1].tv_usec = ts[1].tv_nsec / 1000;

  RETURN_ON_ERROR(::utimes(path, tv));
  return 0;
}

static int create(const char* path, mode_t mode, struct fuse_file_info* fi) {
  int fd = ::open(path, fi->flags, mode);
  RETURN_ON_ERROR(fd);

  try {
    auto* context = fuse_get_context();
    auto* state = static_cast<IntrofsState*>(context->private_data);
    state->ofiles.emplace(path);
  } catch (...) {
    return -1;
  }

  fi->fh = fd;
  return 0;
}

static int open(const char* path, struct fuse_file_info* fi) noexcept {
  int fd = ::open(path, fi->flags);
  RETURN_ON_ERROR(fd);

  try {
    auto* state = getFuseContextData();
    if (fi->flags & (O_WRONLY | O_RDWR | O_CREAT | O_TRUNC)) {
      state->ofiles.emplace(path);
    } else {
      state->ifiles.emplace(path);
    }
  } catch (...) {
    return -1;
  }

  fi->fh = fd;
  return 0;
}

static int read(
    const char* /*path*/, char* buf, size_t size, off_t offset, struct fuse_file_info* fi) {
  int res = ::pread(fi->fh, buf, size, offset);
  if (res == -1) res = -errno;
  return res;
}

static int write(
    const char* /*path*/, const char* buf, size_t size, off_t offset, struct fuse_file_info* fi) {
  int res = ::pwrite(fi->fh, buf, size, offset);
  if (res == -1) res = -errno;
  return res;
}

static int statfs(const char* path, struct statvfs* stbuf) {
  RETURN_ON_ERROR(::statvfs(path, stbuf));
  return 0;
}

static int flush(const char* /*path*/, struct fuse_file_info* fi) {
  /* This is called from every close on an open file, so call the
     close on the underlying filesystem.  But since flush may be
     called multiple times for an open file, this must not really
     close the file.  This is important if used on a network
     filesystem like NFS which flush the data/metadata on close() */
  RETURN_ON_ERROR(::close(dup(fi->fh)));
  return 0;
}

static int release(const char* /*path*/, struct fuse_file_info* fi) {
  ::close(fi->fh);
  return 0;
}

static int fsync(const char* /*path*/, int /*isdatasync*/, struct fuse_file_info* fi) {
  RETURN_ON_ERROR(::fsync(fi->fh));
  return 0;
}

static int setxattr(const char* path, const char* name, const char* value, size_t size, int flags) {
  RETURN_ON_ERROR(::lsetxattr(path, name, value, size, flags));
  return 0;
}

static int getxattr(const char* path, const char* name, char* value, size_t size) {
  int res = ::lgetxattr(path, name, value, size);
  if (res == -1) return -errno;
  return res;
}

static int listxattr(const char* path, char* list, size_t size) {
  int res = ::llistxattr(path, list, size);
  if (res == -1) return -errno;
  return res;
}

static int removexattr(const char* path, const char* name) {
  RETURN_ON_ERROR(::lremovexattr(path, name));
  return 0;
}

static int lock(const char* /*path*/, struct fuse_file_info* fi, int cmd, struct flock* lock) {
  return ulockmgr_op(fi->fh, cmd, lock, &fi->lock_owner, sizeof(fi->lock_owner));
}

}  // namespace introfs

static struct IntroFsOperations : public fuse_operations {
  IntroFsOperations() {
    this->init = introfs::init;
    this->destroy = introfs::destroy;

    this->getattr = introfs::getattr;
    this->fgetattr = introfs::fgetattr;
    this->access = introfs::access;
    this->readlink = introfs::readlink;
    this->opendir = introfs::opendir;
    this->readdir = introfs::readdir;
    this->releasedir = introfs::releasedir;
    this->mknod = introfs::mknod;
    this->mkdir = introfs::mkdir;
    this->symlink = introfs::symlink;
    this->unlink = introfs::unlink;
    this->rmdir = introfs::rmdir;
    this->rename = introfs::rename;
    this->link = introfs::link;
    this->chmod = introfs::chmod;
    this->chown = introfs::chown;
    this->truncate = introfs::truncate;
    this->ftruncate = introfs::ftruncate;
    this->utimens = introfs::utimens;
    this->create = introfs::create;
    this->open = introfs::open;
    this->read = introfs::read;
    this->write = introfs::write;
    this->statfs = introfs::statfs;
    this->flush = introfs::flush;
    this->release = introfs::release;
    this->fsync = introfs::fsync;
    this->setxattr = introfs::setxattr;
    this->getxattr = introfs::getxattr;
    this->listxattr = introfs::listxattr;
    this->removexattr = introfs::removexattr;
    this->lock = introfs::lock;

    this->flag_nullpath_ok = 0;
  }

} introfs_ops;

//
// `fstrace` works by using FUSE (filesystem in user space) to setup a new
// filesystem that mirrors the filesystem rooted under the / directory. The new
// filesystem serves content from the original filesystem while keeping track of
// file opens and creates.
//
// When `fstrace` is invoked inside a directory `dir`, it first sets up the
// mirroring-introspecting filesystem and then proceeds to spawn the
// delegate-tool inside the mirrored copy of `dir`. This allows fstrace to
// monitor the file operations performed by delegate-tool and its subprocesses.
//
// `fstrace` then waits for the spawned delegate-tool to complete and then sets up
// a lazy unmount of the mirroring filesystem with an invocation of
// `fusermount -uz`
//
// Implementation detail:
// ----------------------
//
// The sequence of filesystem setup, make invocation and filesystem unmount
// could possibly have been written as a shell script. However, we need some
// fine grained synchronization that is best achieved in code with some system
// calls. This synchronization is described next.
//
// After delegate-tool is spawned, it needs to pause till the FUSE filesystem is
// setup by the parent `fstrace` process. Otherwise it will begin building
// inside a non-existent directory tree. In code, this is achieved by making the
// spawned delegate-tool process pause util a signal is sent. The parent
// `fstrace` process sends this signal on filesystem init.
//

// fstrace configuration.
// TODO: Maybe load this from a config file in the future.
static struct Configuration {
  const char* mount_point;
  const char* log_filepath;

  Configuration(
      const char* mount_point_ = "/home/lyft/__introfs__",
      const char* log_filepath_ = "/home/lyft/__introfs__.log")
      : mount_point(mount_point_), log_filepath(log_filepath_) {}

  std::string get_mirrored_path(const std::string& path) const {
    return std::string(mount_point) + "/" + path;
  }

} config;

void* fuseOpsThreadFunc(void* state) {
  static char* args[] = {
      const_cast<char*>("introfs"),
      const_cast<char*>(config.mount_point),
      const_cast<char*>("-f"),
      const_cast<char*>("-o"),
      const_cast<char*>("allow_root"),
      const_cast<char*>("-o"),
      const_cast<char*>("default_permissions"),
  };

  fuse_main(array_size(args), args, &introfs_ops, state);
  return nullptr;
}

void setupFsAndWaitForChild(const pid_t& child_pid) {
  ensureMountPoint(config.mount_point);

  IntrofsState state(child_pid, config.log_filepath, config.mount_point);

  //
  // spawn a thread to handle FUSE events
  //
  pthread_t fuse_ops_thread;
  int err = pthread_create(&fuse_ops_thread, nullptr, fuseOpsThreadFunc, &state);
  if (err != 0) throw SystemException("pthread_create failed", err);

  //
  // Meanwhile wait for the spawned delegate-tool process to complete
  //
  while (true) {
    int wstatus;
    int cid = waitpid(child_pid, &wstatus, WNOHANG | WUNTRACED);

    if (cid == 0)
      /* The waitpid call would have hanged. The child process
       is possibly not executing. try again */
      continue;

    if (cid == -1) continue;

    if (WIFEXITED(wstatus) || WIFSIGNALED(wstatus)) /* The child actually terminated */
      break;
  }

  //
  // Lazy unmount the introfs filesystem
  //
  system((std::string("fusermount -uz ") + config.mount_point).c_str());
}

/*
 * No-op signal handler
 */
void onUserSignal2(int) { return; }

void spawnDelegateProcess(const char* tool_name, char* tool_argv[]) {
  //
  // Prepare to be woken up by the `fstrace` parent process
  // and then pause for a signal
  //
  signal(SIGUSR2, onUserSignal2);
  pause();

  //
  // Change directory to the mirrored copy.
  //
  const auto& curdir = getCurrentDir();
  changeDir(config.get_mirrored_path(curdir));

  //
  // spawn sub process
  //
  if (execvp(tool_name, tool_argv) == -1) {
    throw SystemException("Failed to spawn sub process", errno);
  }
}

//
// This tool should be invoked as:
// ```sh
//  fstrace <cmd> [args...]
// ```
//
int main(int argc, char* argv[]) {
  if (argc < 2) {
    fprintf(stderr, "USAGE: %s <cmd> [<args>...]\n", argv[0]);
    return -1;
  }

  const pid_t child_pid = fork();
  if (child_pid > 0) {
    setupFsAndWaitForChild(child_pid);
  } else if (child_pid == 0) /* this is the child */ {
    spawnDelegateProcess(argv[1], argv + 1);
  } else {
    throw SystemException("fork() failed", errno);
  }

  return 0;
}
