fstrace
========

An tools `make` that file access activity

`fstrace` sets up a FUSE (Filesystem in user space) mirror of the directory it
is invoked in. It then spawns the specified command chroot-ed in the mirrored
FUSE filesystem. The FUSE layer allows intercepting file accesses. This
in turn allows us to examine what files were read / written by the command.

The intercepted file accesses are written out to `/tmp/__introfs__.log`

Usage
-----
```sh
fstrace <cmd> [<args>...]
```

Example:
```sh
fstrace gcc utility.c
```