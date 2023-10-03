fstrace
========

A tool for intercepting and logging files accessed by a process.

`fstrace` sets up a FUSE (Filesystem in user space) mirror of the entire
rooted filesystem at `/`. It then spawns the specified command with in the
mirrored FUSE filesystem. As a result, relative file paths accessed by the
(sub-)command go through the mirrored FUSE filesystem and can be
intercepted and the absolute path that is finally accessed can be logged.


Building from source
--------------------
Install required libraries:
```sh
sudo apt-get install libfuse-dev
```
Build from sources:
```sh
# from within the cloned repository folder
mkdir bin
make
```
You will also need to allow `root` to access user created filesystems: edit
`/etc/fuse.conf` and uncomment the line `user_allow_other`.


Usage
-----
```sh
fstrace <cmd> [<args>...]
```

## Example:
```sh
fstrace touch sample_file.txt
```
When the command executes, the FUSE filesystem is mounted on
`/home/user/__introfs__`. The file access log is written to
`/home/user/__introfs__.log` when the command completes.

The file access log contains two columns separated by a TAB character, as below:
```tsv
# type	path
L	/symlink/accessed/by/command
R	/path/to/file/read/by/command
W	/path/to/file/written/by/command
```

The access log is overwritten by each new invocation of `fstrace`.


Intercepting every file path access
-----------------------------------
`fstrace` intercepts relative path accesses only. This means that if your
command accesses absolute paths (such as `/tmp/data.txt`), those accesses will
not be intercepted and logged. Often, information about the relative paths
accessed by a process is sufficient.

To intercept *all* file accesses, we will need to combine FUSE based tracing
with *chroot* style sandboxing: execute the process as if the FUSE filesystem
at `/home/user/__introfs__` is the root filesystem mounted on `/`. A simple
way to do this is to containerize your process with Docker along with
appropriate bind mounts.

```diff
- WARNING! the following command could trash your `~/.bash_history`,
- if you execute the docker container without a command and drop into
- an interactive shell. Backup the file if necessary.
```

For example,
```sh
fstrace docker run -it --rm \
    -u $(id -u):$(id -g) \
    -v /home/user/__introfs__/bin:/bin \
    -v /home/user/__introfs__/cdrom:/cdrom \
    -v /home/user/__introfs__/etc:/etc \
    -v /home/user/__introfs__/home:/home \
    -v /home/user/__introfs__/lib:/lib \
    -v /home/user/__introfs__/lib32:/lib32 \
    -v /home/user/__introfs__/lib64:/lib64 \
    -v /home/user/__introfs__/libx32:/libx32 \
    -v /home/user/__introfs__/media:/media \
    -v /home/user/__introfs__/opt:/opt \
    -v /home/user/__introfs__/run:/run \
    -v /home/user/__introfs__/sbin:/sbin \
    -v /home/user/__introfs__/snap:/snap \
    -v /home/user/__introfs__/srv:/srv \
    -v /home/user/__introfs__/sys:/sys \
    -v /home/user/__introfs__/usr:/usr \
    -v /home/user/__introfs__/var:/var \
  ubuntu:16.04 \
cat /etc/crontab
```
should leave entries for `/etc/crontab`, `/bin/cat` etc., in the access log at
`/home/user/__introfs__.log`.

It is possible to use *chroot* + bind mounts for sandboxing; but containerizing
your process using docker is certainly a friendlier option.


Troubleshooting
---------------
If something goes really wrong and causes `fstrace` to hang, you can explicitly
kill the command launched by `fstrace` and then force unmount the filesystem
with
```sh
umount -lf "$HOME/__introfs__"
```


Disclaimer
----------
I haven't tested the FUSE filesystem implementation to verify that all
operations work as expected. At this point, this is a best effort
implementation.
