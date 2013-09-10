/*
  Copyright (c) 2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/


#ifndef _GLFS_H
#define _GLFS_H

/*
  Enforce the following flags as libgfapi is built
  with them, and we want programs linking against them to also
  be built with these flags. This is necessary as it affects
  some of the structures defined in libc headers (like struct stat)
  and those definitions need to be consistently compiled in
  both the library and the application.
*/

#ifndef _FILE_OFFSET_BITS
#define _FILE_OFFSET_BITS 64
#endif

#ifndef __USE_FILE_OFFSET64
#define __USE_FILE_OFFSET64
#endif

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/uio.h>
#include <unistd.h>
#include <sys/cdefs.h>
#include <dirent.h>
#include <sys/statvfs.h>

/* Values for valid falgs to be used when using XXXsetattr, to set multiple 
   attribute values passed via the related stat structure.
*/
#define GLAPI_SET_ATTR_MODE  0x1
#define GLAPI_SET_ATTR_UID   0x2
#define GLAPI_SET_ATTR_GID   0x4
#define GLAPI_SET_ATTR_SIZE  0x8
#define GLAPI_SET_ATTR_ATIME 0x10
#define GLAPI_SET_ATTR_MTIME 0x20

__BEGIN_DECLS

/* The filesystem object. One object per 'virtual mount' */
struct glfs;
typedef struct glfs glfs_t;

struct glfs_gfid {
        unsigned char *id;
        int len;
};
typedef struct glfs_gfid glfs_gfid_t;



/*
  SYNOPSIS

  glfs_new: Create a new 'virtual mount' object.

  DESCRIPTION

  This is most likely the very first function you will use. This function
  will create a new glfs_t (virtual mount) object in memory.

  On this newly created glfs_t, you need to be either set a volfile path
  (glfs_set_volfile) or a volfile server (glfs_set_volfile_server).

  The glfs_t object needs to be initialized with glfs_init() before you
  can start issuing file operations on it.

  PARAMETERS

  @volname: Name of the volume. This identifies the server-side volume and
            the fetched volfile (equivalent of --volfile-id command line
	    parameter to glusterfsd). When used with glfs_set_volfile() the
	    @volname has no effect (except for appearing in log messages).

  RETURN VALUES

  NULL   : Out of memory condition.
  Others : Pointer to the newly created glfs_t virtual mount object.

*/

glfs_t *glfs_new (const char *volname);


/*
  SYNOPSIS

  glfs_set_volfile: Specify the path to the volume specification file.

  DESCRIPTION

  If you are using a static volume specification file (without dynamic
  volume management abilities from the CLI), then specify the path to
  the volume specification file.

  This is incompatible with glfs_set_volfile_server().

  PARAMETERS

  @fs: The 'virtual mount' object to be configured with the volume
       specification file.

  @volfile: Path to the locally available volume specification file.

  RETURN VALUES

   0 : Success.
  -1 : Failure. @errno will be set with the type of failure.

*/

int glfs_set_volfile (glfs_t *fs, const char *volfile);


/*
  SYNOPSIS

  glfs_set_volfile_server: Specify the address of management server.

  DESCRIPTION

  This function specifies the address of the management server (glusterd)
  to connect, and establish the volume configuration. The @volname
  parameter passed to glfs_new() is the volume which will be virtually
  mounted as the glfs_t object. All operations performed by the CLI at
  the management server will automatically be reflected in the 'virtual
  mount' object as it maintains a connection to glusterd and polls on
  configuration change notifications.

  This is incompatible with glfs_set_volfile().

  PARAMETERS

  @fs: The 'virtual mount' object to be configured with the volume
       specification file.

  @transport: String specifying the transport used to connect to the
              management daemon. Specifying NULL will result in the usage
	      of the default (tcp) transport type. Permitted values
	      are those what you specify as transport-type in a volume
	      specification file (e.g "tcp", "rdma", "unix".)

  @host: String specifying the address of where to find the management
         daemon. Depending on the transport type this would either be
	 an FQDN (e.g: "storage01.company.com"), ASCII encoded IP
	 address "192.168.22.1", or a UNIX domain socket path (e.g
	 "/tmp/glusterd.socket".)

  @port: The TCP port number where gluster management daemon is listening.
         Specifying 0 uses the default port number GF_DEFAULT_BASE_PORT.
	 This parameter is unused if you are using a UNIX domain socket.

  RETURN VALUES

   0 : Success.
  -1 : Failure. @errno will be set with the type of failure.

*/

int glfs_set_volfile_server (glfs_t *fs, const char *transport,
			     const char *host, int port);


/*
  SYNOPSIS

  glfs_set_logging: Specify logging parameters.

  DESCRIPTION

  This function specifies logging parameters for the virtual mount.
  Default log file is /dev/null.

  PARAMETERS

  @fs: The 'virtual mount' object to be configured with the logging parameters.

  @logfile: The logfile to be used for logging. Will be created if it does not
            already exist (provided system permissions allow.)

  @loglevel: Numerical value specifying the degree of verbosity. Higher the
             value, more verbose the logging.

  RETURN VALUES

   0 : Success.
  -1 : Failure. @errno will be set with the type of failure.

*/

int glfs_set_logging (glfs_t *fs, const char *logfile, int loglevel);


/*
  SYNOPSIS

  glfs_init: Initialize the 'virtual mount'

  DESCRIPTION

  This function initializes the glfs_t object. This consists of many steps:
  - Spawn a poll-loop thread.
  - Establish connection to management daemon and receive volume specification.
  - Construct translator graph and initialize graph.
  - Wait for initialization (connecting to all bricks) to complete.

  PARAMETERS

  @fs: The 'virtual mount' object to be initialized.

  RETURN VALUES

   0 : Success.
  -1 : Failure. @errno will be set with the type of failure.

*/

int glfs_init (glfs_t *fs);


int glfs_fini (glfs_t *fs);

/*
 * FILE OPERATION
 *
 * What follows are filesystem operations performed on the
 * 'virtual mount'. The calls here are kept as close to
 * the POSIX system calls as possible.
 *
 * Notes:
 *
 * - All paths specified, even if absolute, are relative to the
 *   root of the virtual mount and not the system root (/).
 *
 */

/* The file descriptor object. One per open file/directory. */

struct glfs_fd;
typedef struct glfs_fd glfs_fd_t;

/*
 * Notes:
 *
 * The file object handle. One per looked up, created file/directory 
 *
 * This had been introduced to facilitate gfid/inode based gfapi
 * - a requirement introduced by nfs-ganesha
 */
struct glfs_object;
typedef struct glfs_object glfs_object_t;


/*
  SYNOPSIS

  glfs_open: Open a file.

  DESCRIPTION

  This function opens a file on a virtual mount.

  PARAMETERS

  @fs: The 'virtual mount' object to be initialized.

  @path: Path of the file within the virtual mount.

  @flags: Open flags. See open(2). O_CREAT is not supported.
          Use glfs_creat() for creating files.

  RETURN VALUES

  NULL   : Failure. @errno will be set with the type of failure.
  Others : Pointer to the opened glfs_fd_t.

 */

glfs_fd_t *glfs_open (glfs_t *fs, const char *path, int flags);


/*
  SYNOPSIS

  glfs_creat: Create a file.

  DESCRIPTION

  This function opens a file on a virtual mount.

  PARAMETERS

  @fs: The 'virtual mount' object to be initialized.

  @path: Path of the file within the virtual mount.

  @mode: Permission of the file to be created.

  @flags: Create flags. See open(2). O_EXCL is supported.

  RETURN VALUES

  NULL   : Failure. @errno will be set with the type of failure.
  Others : Pointer to the opened glfs_fd_t.

 */

glfs_fd_t *glfs_creat (glfs_t *fs, const char *path, int flags,
		       mode_t mode);

int glfs_close (glfs_fd_t *fd);

glfs_t *glfs_from_glfd (glfs_fd_t *fd);

int glfs_set_xlator_option (glfs_t *fs, const char *xlator, const char *key,
			    const char *value);

/*

  glfs_io_cbk

  The following is the function type definition of the callback
  function pointer which has to be provided by the caller to the
  *_async() versions of the IO calls.

  The callback function is called on completion of the requested
  IO, and the appropriate return value is returned in @ret.

  In case of an error in completing the IO, @ret will be -1 and
  @errno will be set with the appropriate error.

  @ret will be same as the return value of the non _async() variant
  of the particular call

  @data is the same context pointer provided by the caller at the
  time of issuing the async IO call. This can be used by the
  caller to differentiate different instances of the async requests
  in a common callback function.
*/

typedef void (*glfs_io_cbk) (glfs_fd_t *fd, ssize_t ret, void *data);

// glfs_{read,write}[_async]

ssize_t glfs_read (glfs_fd_t *fd, void *buf, size_t count, int flags);
ssize_t glfs_write (glfs_fd_t *fd, const void *buf, size_t count, int flags);
int glfs_read_async (glfs_fd_t *fd, void *buf, size_t count, int flags,
		     glfs_io_cbk fn, void *data);
int glfs_write_async (glfs_fd_t *fd, const void *buf, size_t count, int flags,
		      glfs_io_cbk fn, void *data);

// glfs_{read,write}v[_async]

ssize_t glfs_readv (glfs_fd_t *fd, const struct iovec *iov, int iovcnt,
		    int flags);
ssize_t glfs_writev (glfs_fd_t *fd, const struct iovec *iov, int iovcnt,
		     int flags);
int glfs_readv_async (glfs_fd_t *fd, const struct iovec *iov, int count,
		      int flags, glfs_io_cbk fn, void *data);
int glfs_writev_async (glfs_fd_t *fd, const struct iovec *iov, int count,
		       int flags, glfs_io_cbk fn, void *data);

// glfs_p{read,write}[_async]

ssize_t glfs_pread (glfs_fd_t *fd, void *buf, size_t count, off_t offset,
		    int flags);
ssize_t glfs_pwrite (glfs_fd_t *fd, const void *buf, size_t count,
		     off_t offset, int flags);
int glfs_pread_async (glfs_fd_t *fd, void *buf, size_t count, off_t offset,
		      int flags, glfs_io_cbk fn, void *data);
int glfs_pwrite_async (glfs_fd_t *fd, const void *buf, int count, off_t offset,
		       int flags, glfs_io_cbk fn, void *data);

// glfs_p{read,write}v[_async]

ssize_t glfs_preadv (glfs_fd_t *fd, const struct iovec *iov, int iovcnt,
		     off_t offset, int flags);
ssize_t glfs_pwritev (glfs_fd_t *fd, const struct iovec *iov, int iovcnt,
		      off_t offset, int flags);
int glfs_preadv_async (glfs_fd_t *fd, const struct iovec *iov, int count,
		       off_t offset, int flags, glfs_io_cbk fn, void *data);
int glfs_pwritev_async (glfs_fd_t *fd, const struct iovec *iov, int count,
			off_t offset, int flags, glfs_io_cbk fn, void *data);


off_t glfs_lseek (glfs_fd_t *fd, off_t offset, int whence);

int glfs_truncate (glfs_t *fs, const char *path, off_t length);

int glfs_ftruncate (glfs_fd_t *fd, off_t length);
int glfs_ftruncate_async (glfs_fd_t *fd, off_t length, glfs_io_cbk fn,
			  void *data);

int glfs_lstat (glfs_t *fs, const char *path, struct stat *buf);
int glfs_stat (glfs_t *fs, const char *path, struct stat *buf);
int glfs_fstat (glfs_fd_t *fd, struct stat *buf);

int glfs_fsync (glfs_fd_t *fd);
int glfs_fsync_async (glfs_fd_t *fd, glfs_io_cbk fn, void *data);

int glfs_fdatasync (glfs_fd_t *fd);
int glfs_fdatasync_async (glfs_fd_t *fd, glfs_io_cbk fn, void *data);

int glfs_access (glfs_t *fs, const char *path, int mode);

int glfs_symlink (glfs_t *fs, const char *oldpath, const char *newpath);

int glfs_readlink (glfs_t *fs, const char *path, char *buf, size_t bufsiz);

int glfs_mknod (glfs_t *fs, const char *path, mode_t mode, dev_t dev);

int glfs_mkdir (glfs_t *fs, const char *path, mode_t mode);

int glfs_unlink (glfs_t *fs, const char *path);

int glfs_rmdir (glfs_t *fs, const char *path);

int glfs_rename (glfs_t *fs, const char *oldpath, const char *newpath);

int glfs_link (glfs_t *fs, const char *oldpath, const char *newpath);

glfs_fd_t *glfs_opendir (glfs_t *fs, const char *path);

int glfs_readdir_r (glfs_fd_t *fd, struct dirent *dirent,
		    struct dirent **result);

int glfs_readdirplus_r (glfs_fd_t *fd, struct stat *stat, struct dirent *dirent,
			struct dirent **result);

long glfs_telldir (glfs_fd_t *fd);

void glfs_seekdir (glfs_fd_t *fd, long offset);

int glfs_closedir (glfs_fd_t *fd);

int glfs_statvfs (glfs_t *fs, const char *path, struct statvfs *buf);

int glfs_chmod (glfs_t *fs, const char *path, mode_t mode);

int glfs_fchmod (glfs_fd_t *fd, mode_t mode);

int glfs_chown (glfs_t *fs, const char *path, uid_t uid, gid_t gid);

int glfs_lchown (glfs_t *fs, const char *path, uid_t uid, gid_t gid);

int glfs_fchown (glfs_fd_t *fd, uid_t uid, gid_t gid);

int glfs_utimens (glfs_t *fs, const char *path, struct timespec times[2]);

int glfs_lutimens (glfs_t *fs, const char *path, struct timespec times[2]);

int glfs_futimens (glfs_fd_t *fd, struct timespec times[2]);

ssize_t glfs_getxattr (glfs_t *fs, const char *path, const char *name,
		       void *value, size_t size);

ssize_t glfs_lgetxattr (glfs_t *fs, const char *path, const char *name,
			void *value, size_t size);

ssize_t glfs_fgetxattr (glfs_fd_t *fd, const char *name,
			void *value, size_t size);

ssize_t glfs_listxattr (glfs_t *fs, const char *path, void *value, size_t size);

ssize_t glfs_llistxattr (glfs_t *fs, const char *path, void *value,
			 size_t size);

ssize_t glfs_flistxattr (glfs_fd_t *fd, void *value, size_t size);

int glfs_setxattr (glfs_t *fs, const char *path, const char *name,
		   const void *value, size_t size, int flags);

int glfs_lsetxattr (glfs_t *fs, const char *path, const char *name,
		    const void *value, size_t size, int flags);

int glfs_fsetxattr (glfs_fd_t *fd, const char *name,
		    const void *value, size_t size, int flags);

int glfs_removexattr (glfs_t *fs, const char *path, const char *name);

int glfs_lremovexattr (glfs_t *fs, const char *path, const char *name);

int glfs_fremovexattr (glfs_fd_t *fd, const char *name);

int glfs_fallocate(glfs_fd_t *fd, int keep_size, off_t offset, size_t len);

int glfs_discard(glfs_fd_t *fd, off_t offset, size_t len);

int glfs_discard_async (glfs_fd_t *fd, off_t length, size_t lent,
			glfs_io_cbk fn, void *data);

char *glfs_getcwd (glfs_t *fs, char *buf, size_t size);

int glfs_chdir (glfs_t *fs, const char *path);

int glfs_fchdir (glfs_fd_t *fd);

char *glfs_realpath (glfs_t *fs, const char *path, char *resolved_path);

/*
 * @cmd and @flock are as specified in man fcntl(2).
 */
int glfs_posix_lock (glfs_fd_t *fd, int cmd, struct flock *flock);

glfs_fd_t *glfs_dup (glfs_fd_t *fd);

/* Handle based operations */
struct glfs_object *glfs_h_lookupat (struct glfs *fs, 
				     struct glfs_object *parent, 
				     const char *path, struct stat *stat);

int glfs_h_getattrs (struct glfs *fs, struct glfs_object *object, 
		     struct stat *stat);

int glfs_h_setattrs (struct glfs *fs, struct glfs_object *object, 
		     struct stat *sb, int valid, int follow);

struct glfs_fd *glfs_h_open (struct glfs *fs, struct glfs_object *object, 
			     int flags);

struct glfs_object *glfs_h_creat (struct glfs *fs, struct glfs_object *parent, 
				  const char *path, int flags, mode_t mode, 
				  struct stat *sb);

struct glfs_object *glfs_h_mkdir (struct glfs *fs, struct glfs_object *parent, 
				  const char *path, mode_t flags, 
				  struct stat *sb);

struct glfs_object *glfs_h_mknod (struct glfs *fs, struct glfs_object *parent, 
				  const char *path, mode_t mode, dev_t dev, 
				  struct stat *sb);

struct glfs_gfid *glfs_h_extract_gfid (struct glfs_object *object);

struct glfs_object *glfs_h_create_from_gfid (struct glfs *fs, 
					     struct glfs_gfid *gfid, 
					     struct stat *sb);

struct glfs_fd *glfs_h_opendir (struct glfs *fs, struct glfs_object *object);

int glfs_h_unlink (struct glfs *fs, struct glfs_object *parent, 
		   const char *path);

int glfs_h_close (struct glfs_object *object);

int glfs_caller_specific_init (void *uid_caller_key, void *gid_caller_key, 
			       void *future);
int
glfs_h_truncate (struct glfs *fs, struct glfs_object *object, int offset);

__END_DECLS

#endif /* !_GLFS_H */

