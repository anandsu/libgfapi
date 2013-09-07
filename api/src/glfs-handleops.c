/*
 *  Copyright (c) 2013 Red Hat, Inc. <http://www.redhat.com>
 *  This file is part of GlusterFS.
 * 
 *  This file is licensed to you under your choice of the GNU Lesser
 *  General Public License, version 3 or any later version (LGPLv3 or
 *  later), or the GNU General Public License, version 2 (GPLv2), in all
 *  cases as published by the Free Software Foundation.
 */


#include "glfs-internal.h"
#include "glfs-mem-types.h"
#include "syncop.h"
#include "glfs.h"

#define DEFAULT_REVAL_COUNT 1

#define ESTALE_RETRY(ret,errno,reval,loc,label) do {	\
if (ret == -1 && errno == ESTALE) {	        \
	if (reval < DEFAULT_REVAL_COUNT) {	\
		reval++;			\
		loc_wipe (loc);			\
		goto label;			\
		}					\
		}						\
		} while (0)

void 
revalidateinode(struct glfs *fs, struct glfs_object *object)
{
	inode_t *updated = NULL;

#ifdef DEBUG1
	printf ("%s:%d: Object ref count IN = %d\n", __FUNCTION__, __LINE__, 
		object->inode->ref);
#endif
	/* TODO: Check if we can upgrade this lock to a R/W with writer 
	 * promotion for faster common case.
	 * Is the new inode linked? Do we have a ref count on that already? */
	glfs_lock (fs);

	if (object->inode->table->xl != fs->active_subvol) {
		/* TODO: Unref the old inode handle? */
		updated = __glfs_refresh_inode (fs, fs->active_subvol, 
						object->inode);
		object->inode = updated;
	}

	glfs_unlock (fs);

#ifdef DEBUG1
	printf ("%s:%d: Object ref count OUT = %d\n", __FUNCTION__, __LINE__, 
		object->inode->ref);
#endif
	return;
}

void
glfs_iatt_from_stat(struct stat *sb, int valid, struct iatt *iatt, 
		    int *glvalid)
{
	*glvalid = 0;

	if (valid & GLAPI_SET_ATTR_MODE) {
		iatt->ia_prot = ia_prot_from_st_mode (sb->st_mode);
		*glvalid |= GF_SET_ATTR_MODE;
	}

	if (valid & GLAPI_SET_ATTR_UID) {
		iatt->ia_uid = sb->st_uid;
		*glvalid |= GF_SET_ATTR_UID;
	}

	if (valid & GLAPI_SET_ATTR_GID) {
		iatt->ia_gid = sb->st_gid;
		*glvalid |= GF_SET_ATTR_GID;
	}

	if (valid & GLAPI_SET_ATTR_ATIME) {
		iatt->ia_atime = sb->st_atime;
		iatt->ia_atime_nsec = ST_ATIM_NSEC (sb);
		*glvalid |= GF_SET_ATTR_ATIME;
	}

	if (valid & GLAPI_SET_ATTR_MTIME) {
		iatt->ia_mtime = sb->st_mtime;
		iatt->ia_mtime_nsec = ST_MTIM_NSEC (sb);
		*glvalid |= GF_SET_ATTR_MTIME;
	}

	return;
}

struct glfs_object *
glfs_h_lookupat(struct glfs *fs, struct glfs_object *parent, 
	      const char *path, struct stat *stat)
{
	int 			 ret = 0;
	xlator_t		*subvol = NULL;
	struct iatt		 iatt = {0, };
	struct glfs_object 	*object = NULL;
	loc_t 			 loc = {0, };

#ifdef DEBUG1
	if (parent)
		printf ("%s:%d: Parent ref count IN = %d\n", __FUNCTION__, 
			__LINE__, parent->inode->ref);
#endif

	__glfs_entry_fs (fs);

	/* get the active volume */
	subvol = glfs_active_subvol (fs);
	if (!subvol) {
		errno = EIO;
		goto out;
	}

	if (parent) {
		revalidateinode (fs, parent);
	}

	/* FIXME: validate relative or absolute path irrespective of parent 
	 * passed in for API correctness. in args validation.
	 * Check if the passed in parent is a directory? the type is cached in 
	 * the iNode so lookup is not required. */
	/* TODO: stale error handling? */
	ret = glfs_resolve_at (fs, subvol, (parent? parent->inode : NULL), 
			       path, &loc, &iatt, 0 /*TODO: links? */, 0);
	
	if (!ret) {
		/* allocate a return object */
		object = calloc(1, sizeof(struct glfs_object));
		if (object == NULL) {
			errno = ENOMEM;
			goto out;
		}
		
		/* populate the return object */
		object->inode = loc.inode;
		uuid_copy (object->gfid, object->inode->gfid);
		
		/* populate stat */
		glfs_iatt_to_stat (fs, &iatt, stat);
		
		/* we hold the reference */
		loc.inode = NULL;
	}

out:
	loc_wipe(&loc);
	glfs_subvol_done (fs, subvol);

#ifdef DEBUG1
	if (parent)
		printf ("%s:%d: Parent ref count OUT = %d\n", __FUNCTION__, 
			__LINE__, parent->inode->ref);
	if (object)
		printf ("%s:%d: Object ref count OUT = %d\n", __FUNCTION__, 
			__LINE__, object->inode->ref);
#endif
	return object;
}

int
glfs_h_getattrs(struct glfs *fs, struct glfs_object *object, 
		struct stat *stat)
{
	int 			 ret = 0;
	xlator_t		*subvol = NULL;
	struct iatt		 iatt = {0, };

#ifdef DEBUG1
	if (object)
		printf ("%s:%d: Object ref count IN = %d\n", __FUNCTION__, 
			__LINE__, object->inode->ref);
#endif

	__glfs_entry_fs (fs);

	/* get the active volume */
	subvol = glfs_active_subvol (fs);
	if (!subvol) {
		ret = -1;
		errno = EIO;
		goto out;
	}

	if (object) {
		revalidateinode (fs, object);
	}

	/* FIXME: validate relative or absolute path irrespective of parent 
	 * passed in for API correctness. in args validation.
	 * Check if the passed in parent is a directory? the type is cached in 
	 * the iNode so lookup is not required. */
	/* TODO: stale error handling? */
	/* TODO: No error returned from glfs_resolve_base? */
	glfs_resolve_base (fs, subvol, object->inode, &iatt);
	//if (!ret) {
		glfs_iatt_to_stat (fs, &iatt, stat);
	//}

out:
	glfs_subvol_done (fs, subvol);

#ifdef DEBUG1
	if (object)
		printf ("%s:%d: Object ref count OUT = %d\n", __FUNCTION__, 
			__LINE__, object->inode->ref);
#endif
	return ret;
}

int
glfs_h_setattrs (struct glfs *fs, struct glfs_object *object, struct stat *sb, 
		int valid, int follow)
{
	int              ret = -1;
	xlator_t        *subvol = NULL;
	loc_t            loc = {0, };
	struct iatt      iatt = {0, };
	int              reval = 0;
	int              glvalid = 0;

#ifdef DEBUG1
	if (object)
		printf ("%s:%d: Object ref count IN = %d\n", __FUNCTION__, 
			__LINE__, object->inode->ref);
#endif
	__glfs_entry_fs (fs);

	subvol = glfs_active_subvol (fs);
	if (!subvol) {
		ret = -1;
		errno = EIO;
		goto out;
	}

	/* map out the in valid mask to the settable mask */
	glfs_iatt_from_stat(sb, valid, &iatt, &glvalid);

retry:
	if (object) {
		revalidateinode (fs, object);
	}

	/* TODO: do we need the UUID copied? */
	loc.inode = inode_ref (object->inode);
	uuid_copy (loc.gfid, object->inode->gfid);
	ret = glfs_loc_touchup (&loc);
	if (ret != 0) {
		errno = EINVAL;
		goto out;
	}

	ret = syncop_setattr (subvol, &loc, &iatt, glvalid, 0, 0);

	ESTALE_RETRY (ret, errno, reval, &loc, retry);
out:
	loc_wipe (&loc);

	glfs_subvol_done (fs, subvol);

#ifdef DEBUG1
	if (object)
		printf ("%s:%d: Object ref count OUT = %d\n", __FUNCTION__, 
			__LINE__, object->inode->ref);
#endif
	return ret;
}

struct glfs_fd *
glfs_h_open(struct glfs *fs, struct glfs_object *object, int flags)
{
	int              ret = -1;
	struct glfs_fd  *glfd = NULL;
	xlator_t        *subvol = NULL;
	loc_t            loc = {0, };
	int              reval = 0;

	__glfs_entry_fs (fs);

	subvol = glfs_active_subvol (fs);
	if (!subvol) {
		errno = EIO;
		goto out;
	}

	if (!object) {
		errno = EINVAL;
		goto out;
	}

#ifdef DEBUG1
	printf ("%s:%d: Object ref count IN = %d\n", __FUNCTION__, 
		__LINE__, object->inode->ref);
#endif

	glfd = glfs_fd_new (fs);
	if (!glfd) {
		errno = ENOMEM;
		goto out;
	}

retry:
	/* TODO: Is this enough to just get the right inode? no resolve.
	 * Should this be done on the retry as well? */
	if (object) {
		revalidateinode (fs, object);
	}

	if (IA_ISDIR (object->inode->ia_type)) {
		ret = -1;
		errno = EISDIR;
		goto out;
	}

	if (!IA_ISREG (object->inode->ia_type)) {
		ret = -1;
		errno = EINVAL;
		goto out;
	}

	if (glfd->fd) {
		/* Retry. Safe to touch glfd->fd as we
		 *	   still have not glfs_fd_bind() yet.
		 */
		fd_unref (glfd->fd);
		glfd->fd = NULL;
	}

	glfd->fd = fd_create (object->inode, getpid());
	if (!glfd->fd) {
		ret = -1;
		errno = ENOMEM;
		goto out;
	}

	loc.inode = inode_ref (object->inode);
	ret = glfs_loc_touchup (&loc);
	if (ret != 0) {
		errno = EINVAL;
		goto out;
	}

	ret = syncop_open (subvol, &loc, flags, glfd->fd);

	ESTALE_RETRY (ret, errno, reval, &loc, retry);

out:
	loc_wipe (&loc);

	if (ret && glfd) {
		glfs_fd_destroy (glfd);
		glfd = NULL;
	} else {
		fd_bind (glfd->fd);
		glfs_fd_bind (glfd);
	}

	glfs_subvol_done (fs, subvol);

#ifdef DEBUG1
	if (object)
		printf ("%s:%d: Object ref count OUT = %d\n", __FUNCTION__, 
			__LINE__, object->inode->ref);
#endif
	return glfd;
}

struct glfs_object *
glfs_h_creat(struct glfs *fs, struct glfs_object *parent, const char *path, 
	     int flags, mode_t mode, struct stat *sb)
{
	int              ret = -1;
	struct glfs_fd  *glfd = NULL;
	xlator_t        *subvol = NULL;
	loc_t            loc = {0, };
	struct iatt      iatt = {0, };
	uuid_t           gfid;
	dict_t          *xattr_req = NULL;
	struct glfs_object *object = NULL;

	__glfs_entry_fs (fs);

	subvol = glfs_active_subvol (fs);
	if (!subvol) {
		ret = -1;
		errno = EIO;
		goto out;
	}
	
	xattr_req = dict_new ();
	if (!xattr_req) {
		ret = -1;
		errno = ENOMEM;
		goto out;
	}

	uuid_generate (gfid);
	ret = dict_set_static_bin (xattr_req, "gfid-req", gfid, 16);
	if (ret) {
		ret = -1;
		errno = ENOMEM;
		goto out;
	}
	
	glfd = glfs_fd_new (fs);
	if (!glfd)
		goto out;

	if (parent != NULL) {
		revalidateinode (fs, parent);
	}

	loc.inode = inode_new (parent->inode->table);
	if (!loc.inode) {
		ret = -1;
		errno = ENOMEM;
		goto out;
	}
	loc.parent = inode_ref (parent->inode);
	loc.name = path;
	ret = glfs_loc_touchup (&loc);
	if (ret != 0) {
		errno = EINVAL;
		goto out;
	}

	glfd->fd = fd_create (loc.inode, getpid());
	if (!glfd->fd) {
		ret = -1;
		errno = ENOMEM;
		goto out;
	}

/*
	uid = ((uid_t *)pthread_getspecific( *uid_key ));
	gid = ((gid_t *)pthread_getspecific( *gid_key ));
	printf( "from glfs creat : uid = %d, gid = %d\n", *uid, *gid );
*/

	ret = syncop_create (subvol, &loc, flags, mode, glfd->fd,
			     xattr_req, &iatt);
	if (ret == 0) {
		ret = glfs_loc_link (&loc, &iatt);
		if (ret != 0) {
			goto out;
		}
		
		glfs_iatt_to_stat (fs, &iatt, sb);

		if (object == NULL) {
			object = calloc(1, sizeof(struct glfs_object));
			if (object == NULL) {
				errno = ENOMEM;
				ret = -1;
				goto out;
			}
			
			object->inode = loc.inode;
			uuid_copy (object->gfid, object->inode->gfid);
			loc.inode = NULL;
		}
	}
	
out:
	if (ret && object != NULL) {
		glfs_h_close(object);
		object = NULL;
	}

	loc_wipe(&loc);
	
	if (xattr_req)
		dict_unref (xattr_req);

	if (glfd) {
		glfs_fd_destroy (glfd);
		glfd = NULL;
	}

	glfs_subvol_done (fs, subvol);

#ifdef DEBUG1
	if (parent)
		printf ("%s:%d: Parent ref count OUT = %d\n", __FUNCTION__, 
			__LINE__, parent->inode->ref);
	if (object)
		printf ("%s:%d: Object ref count OUT = %d\n", __FUNCTION__, 
			__LINE__, object->inode->ref);
#endif

	return object;
}

struct glfs_object *
glfs_h_mkdir(struct glfs *fs, struct glfs_object *parent, const char *path, 
	     mode_t mode, struct stat *sb)
{
	int              ret = -1;
	xlator_t        *subvol = NULL;
	loc_t            loc = {0, };
	struct iatt      iatt = {0, };
	uuid_t           gfid;
	dict_t          *xattr_req = NULL;
	struct glfs_object *object = NULL;

#ifdef DEBUG1
	if (parent)
		printf ("%s:%d: Parent ref count IN = %d\n", __FUNCTION__, 
			__LINE__, parent->inode->ref);
#endif

	__glfs_entry_fs (fs);

	subvol = glfs_active_subvol (fs);
	if (!subvol) {
		ret = -1;
		errno = EIO;
		goto out;
	}

	xattr_req = dict_new ();
	if (!xattr_req) {
		ret = -1;
		errno = ENOMEM;
		goto out;
	}

	uuid_generate (gfid);
	ret = dict_set_static_bin (xattr_req, "gfid-req", gfid, 16);
	if (ret) {
		ret = -1;
		errno = ENOMEM;
		goto out;
	}

	if (parent != NULL) {
		revalidateinode (fs, parent);
	}
	loc.inode = inode_new (parent->inode->table);
	if (!loc.inode) {
		ret = -1;
		errno = ENOMEM;
		goto out;
	}

	loc.parent = inode_ref (parent->inode);
	loc.name = path;
	ret = glfs_loc_touchup (&loc);
	if (ret != 0) {
		errno = EINVAL;
		goto out;
	}

	ret = syncop_mkdir (subvol, &loc, mode, xattr_req, &iatt);
	if ( ret == 0 )  {
		ret = glfs_loc_link (&loc, &iatt);
		if (ret != 0) {
			goto out;
		}
		
		glfs_iatt_to_stat (fs, &iatt, sb);
		
		object = calloc(1, sizeof(struct glfs_object));
		if (object == NULL) {
			errno = ENOMEM;
			ret = -1;
			goto out;
		}

		object->inode = loc.inode;
		uuid_copy (object->gfid, object->inode->gfid);

		loc.inode = NULL;
	}

out:
	if (ret && object != NULL) {
		glfs_h_close(object);
		object = NULL;
	}

	loc_wipe(&loc);

	if (xattr_req)
		dict_unref (xattr_req);

	glfs_subvol_done (fs, subvol);

	return object;
}

struct glfs_object *
glfs_h_mknod(struct glfs *fs, struct glfs_object *parent, const char *path, 
	     mode_t mode, dev_t dev, struct stat *sb)
{
	int                 ret = -1;
	xlator_t           *subvol = NULL;
	loc_t               loc = {0, };
	struct iatt         iatt = {0, };
	uuid_t              gfid;
	dict_t             *xattr_req = NULL;
	int                 reval = 0;
	struct glfs_object *object = NULL;

#ifdef DEBUG1
	if (parent)
		printf ("%s:%d: Parent ref count IN = %d\n", __FUNCTION__, 
			__LINE__, parent->inode->ref);
#endif

	__glfs_entry_fs (fs);

	subvol = glfs_active_subvol (fs);
	if (!subvol) {
		ret = -1;
		errno = EIO;
		goto out;
	}

	xattr_req = dict_new ();
	if (!xattr_req) {
		ret = -1;
		errno = ENOMEM;
		goto out;
	}

	/* TODO: This is not cleaned up on error, is this fine? */
	uuid_generate (gfid);
	ret = dict_set_static_bin (xattr_req, "gfid-req", gfid, 16);
	if (ret) {
		ret = -1;
		errno = ENOMEM;
		goto out;
	}

retry:
	if (object != NULL) {
		/* retry madness */
		glfs_h_close(object);
		object = NULL;
	}

	/* TODO: Is this enough to just get the right inode? no resolve.
	 * Should this be done on the retry as well? */
	if (parent != NULL) {
		revalidateinode (fs, parent);
	}

	object = glfs_h_lookupat (fs, parent, path, sb);

	if (object == NULL && errno != ENOENT)
		/* Any other type of error is fatal */
		goto out;

	if (object != NULL) {
		ret = -1;
		errno = EEXIST;
		goto out;
	}

	/* FIXME: on a retry loop we may leak an inode */
	loc.inode = inode_new (parent->inode->table);
	if (!loc.inode) {
		ret = -1;
		errno = ENOMEM;
		goto out;
	}

	loc.parent = inode_ref (parent->inode);
	loc.name = path;
	ret = glfs_loc_touchup (&loc);
	if (ret != 0) {
		errno = EINVAL;
		goto out;
	}

	ret = syncop_mknod (subvol, &loc, mode, dev, xattr_req, &iatt);

	ESTALE_RETRY (ret, errno, reval, &loc, retry);

	if (ret == 0) {
		/* FIXME: any errors beyond this point may leave the object 
		 * created but the handle not returned */
		ret = glfs_loc_link (&loc, &iatt);
		if (ret != 0) {
			goto out;
		}

		/* populate stat */
		glfs_iatt_to_stat (fs, &iatt, sb);

		object = calloc(1, sizeof(struct glfs_object));
		if (object == NULL) {
			errno = ENOMEM;
			ret = -1;
			goto out;
		}

		/* populate the return object */
		object->inode = loc.inode;
		uuid_copy (object->gfid, object->inode->gfid);

		/* we hold the reference */
		loc.inode = NULL;
	}
out:
	/* FIXME: If syncop_creat retruns an error, then there seems to be 2 
	 * inode ref decrements, one in the close below and the other in the 
	 * fd_destroy, which does not seem to be an issue in glfs_creat, check
	 * and fix the same, also to recreate, try to create an existing file
	 * and pass in flags as 0 */
	if (ret && object != NULL) {
		glfs_h_close(object);
		object = NULL;
	}

	loc_wipe(&loc);

	if (xattr_req)
		dict_unref (xattr_req);

	glfs_subvol_done (fs, subvol);

#ifdef DEBUG1
	if (parent)
		printf ("%s:%d: Parent ref count OUT = %d\n", __FUNCTION__, 
			__LINE__, parent->inode->ref);
	if (object)
		printf ("%s:%d: Object ref count OUT = %d\n", __FUNCTION__, 
		__LINE__, object->inode->ref);
#endif

	return object;
}

int
glfs_h_unlink (struct glfs *fs, struct glfs_object *parent, const char *path)
{
	int                 ret = -1;
	xlator_t           *subvol = NULL;
	loc_t               loc = {0, };
	struct stat         sb;
	struct glfs_object *object = NULL;

#ifdef DEBUG1
	if (parent)
		printf ("%s:%d: Parent ref count IN = %d\n", __FUNCTION__, 
			__LINE__, parent->inode->ref);
#endif

	__glfs_entry_fs( fs );

	subvol = glfs_active_subvol( fs );
	if ( !subvol ) {
		ret = -1;
		errno = EIO;
		goto out;
	}

	loc.parent = inode_ref( parent->inode );
	loc.name = path;

	object = calloc( 1, sizeof(struct glfs_object) );
	if ( NULL == object ) {
		errno = ENOMEM;
		ret = -1;
		goto out;
	}

	object->inode = inode_grep( parent->inode->table,
	                            parent->inode,
				    path );
	if ( NULL == object->inode ) {
		gf_log( subvol->name, GF_LOG_WARNING,
			"%s:%d: inode grep failed : parent: %p, path : %s, errno = %d",
			__FUNCTION__, __LINE__,
			parent->inode, path, errno );

		object = glfs_h_lookupat( fs, parent, path, &sb );
		if ( NULL == object ) {
			ret = -1;
			gf_log( subvol->name, GF_LOG_ERROR,
				"%s:%d: Failed to lookup inode for parent inode:\
					 %p, path : %s, errno = %d",
				__FUNCTION__, __LINE__,
				parent->inode, path, errno );

			goto out;
		}
	}

	loc.inode = object->inode;

	ret = glfs_loc_touchup( &loc );
	if ( ret != 0 ) {
		errno = EINVAL;
		goto out;
	}

	if ( !IA_ISDIR( loc.inode->ia_type ) ) {
		ret = syncop_unlink( subvol, &loc );
		if (ret != 0) {
			gf_log( subvol->name, GF_LOG_ERROR,
				"%s:%d: syncop_unlink error, parent inode: \
					%p, path : %s, errno = %d",
				__FUNCTION__, __LINE__,
				parent->inode, path, errno );
			goto out;
		}
	} else {
		ret = syncop_rmdir( subvol, &loc );
		if ( ret != 0 ) {
			gf_log( subvol->name, GF_LOG_ERROR,
				"%s:%d: syncop_rmdir error, parent inode: \
					%p, path : %s, errno = %d",
				__FUNCTION__, __LINE__,
				parent->inode, path, errno );
			goto out;
		}
	}

	if (ret == 0)
		ret = glfs_loc_unlink( &loc );

out:
	loc_wipe (&loc);

	if ( object != NULL )
		glfs_h_close( object );

	glfs_subvol_done( fs, subvol );

#ifdef DEBUG1
	if (parent)
		printf ("%s:%d: Parent ref count OUT = %d\n", __FUNCTION__, 
			__LINE__, parent->inode->ref);
#endif

	return ret;
}

struct glfs_fd *
glfs_h_opendir (struct glfs *fs, struct glfs_object *object)
{
	int              ret = -1;
	struct glfs_fd  *glfd = NULL;
	xlator_t        *subvol = NULL;
	loc_t            loc = {0, };
	int              reval = 0;

#ifdef DEBUG1
	if (object)
		printf ("%s:%d: Object ref count IN = %d\n", __FUNCTION__, 
			__LINE__, object->inode->ref);
#endif

	__glfs_entry_fs (fs);

	subvol = glfs_active_subvol (fs);
	if (!subvol) {
		ret = -1;
		errno = EIO;
		goto out;
	}

	glfd = glfs_fd_new (fs);
	if (!glfd)
		goto out;

	INIT_LIST_HEAD (&glfd->entries);
retry:
	/* TODO: Is this enough to just get the right inode? no resolve.
	* Should this be done on the retry as well? */
	if (object != NULL) {
		revalidateinode (fs, object);
	}

	if (!IA_ISDIR (object->inode->ia_type)) {
		ret = -1;
		errno = ENOTDIR;
		goto out;
	}

	if (glfd->fd) {
		/* Retry. Safe to touch glfd->fd as we
		 *	   still have not glfs_fd_bind() yet.
		 */
		fd_unref (glfd->fd);
		glfd->fd = NULL;
	}

	glfd->fd = fd_create (object->inode, getpid());
	if (!glfd->fd) {
		ret = -1;
		errno = ENOMEM;
		goto out;
	}

	loc.inode = inode_ref (object->inode);
	ret = glfs_loc_touchup (&loc);
	if (ret != 0) {
		errno = EINVAL;
		goto out;
	}

	ret = syncop_opendir (subvol, &loc, glfd->fd);

	ESTALE_RETRY (ret, errno, reval, &loc, retry);
out:
	loc_wipe (&loc);

	if (ret && glfd) {
		glfs_fd_destroy (glfd);
		glfd = NULL;
	} else {
		fd_bind (glfd->fd);
		glfs_fd_bind (glfd);
	}

	glfs_subvol_done (fs, subvol);

#ifdef DEBUG1
	if (object)
		printf ("%s:%d: Object ref count OUT = %d\n", __FUNCTION__, 
			__LINE__, object->inode->ref);
#endif

	return glfd;
}

struct glfs_gfid *
glfs_h_extract_gfid(struct glfs_object *object)
{
	struct glfs_gfid *ret = NULL;

	/* FIXME: This is a little stupid at present, need to alloc a single
	 * structure and then proceed, than 2 allocs, and maybe add a release
	 * function in case we start using our mem pools etc. */
	ret = calloc(1, sizeof(struct glfs_gfid));
	if (ret == NULL) {
		errno = ENOMEM;
		goto out;
	}

	ret->len = 16;
	ret->id = calloc(ret->len, sizeof(unsigned char));
	if (ret->id == NULL) {
		errno = ENOMEM;
		free (ret); ret = NULL;
		goto out;
	}

	memcpy(ret->id, object->gfid, 16);
out:
	return ret;
}

struct glfs_object *
glfs_h_create_from_gfid(struct glfs *fs, struct glfs_gfid *id, struct stat *sb)
{
	loc_t               loc = {0, };
	int                 ret = -1;
	struct iatt         iatt = {0, };
	inode_t            *newinode = NULL;
	xlator_t           *subvol = NULL;
	struct glfs_object *object = NULL;

	__glfs_entry_fs (fs);

	subvol = glfs_active_subvol (fs);
	if (!subvol) {
		errno = EIO;
		goto out;
	}

	if (id->len != 16) {
		errno = EINVAL;
		goto out;
	}
	memcpy (loc.gfid, id->id, 16);

	newinode = inode_find (subvol->itable, loc.gfid);
	if (newinode)
		loc.inode = newinode;
	else {
		loc.inode = inode_new (subvol->itable);
		if (!loc.inode) {
			errno = ENOMEM;
			goto out;
		}
	}

	/* TODO: ESTALE retry? */
	ret = syncop_lookup (subvol, &loc, 0, &iatt, 0, 0);
	if (ret) {
		gf_log (subvol->name, GF_LOG_WARNING,
			"inode refresh of %s failed: %s",
			uuid_utoa (loc.gfid), strerror (errno));
		goto out;
	}
	
	newinode = inode_link (loc.inode, 0, 0, &iatt);
	if (newinode)
		inode_lookup (newinode);
	else {
		gf_log (subvol->name, GF_LOG_WARNING,
			"inode linking of %s failed: %s",
			uuid_utoa (loc.gfid), strerror (errno));
		errno = EINVAL;
		goto out;
	}

	/* populate stat */
	glfs_iatt_to_stat (fs, &iatt, sb);

	object = calloc(1, sizeof(struct glfs_object));
	if (object == NULL) {
		errno = ENOMEM;
		ret = -1;
		goto out;
	}

	/* populate the return object */
	object->inode = newinode;
	uuid_copy (object->gfid, object->inode->gfid);

out:
	loc_wipe (&loc);

	glfs_subvol_done (fs, subvol);

#ifdef DEBUG1
	if (object)
		printf ("%s:%d: Object ref count OUT = %d\n", __FUNCTION__, 
			__LINE__, object->inode->ref);
#endif

	return object;
}

int 
glfs_h_close(struct glfs_object *object)
{
#ifdef DEBUG1
	if (object)
		printf ("%s:%d: Object ref count IN = %d\n", __FUNCTION__, 
			__LINE__, object->inode->ref);
#endif
	inode_unref (object->inode);
	free (object);

	return 0;
}

int
glfs_h_truncate( struct glfs *fs, struct glfs_object *object, int offset )
{
	loc_t               loc = {0, };
	int                 ret = -1;
	xlator_t           *subvol = NULL;

	if ((object == NULL) || (fs == NULL) || (offset <= 0)) {
		return -1;
	}
        __glfs_entry_fs (fs);

        subvol = glfs_active_subvol( fs );
        if (!subvol) {
                ret = -1;
                errno = EIO;
                goto out;
        }

	if ( object ) {
		revalidateinode( fs, object );
	}

	/*
 	 * Do an inode_lookup here? or inode_find rather first?
 	 */

	loc.inode = inode_ref (object->inode);
	uuid_copy (loc.gfid, object->inode->gfid);
	ret = glfs_loc_touchup (&loc);
	if (ret != 0) {
		errno = EINVAL;
		goto out;
	}

	ret = syncop_truncate( subvol, &loc, (off_t)offset );
	if ( ret ) {
		gf_log( subvol->name, GF_LOG_ERROR,
			"syncop truncate failed : %s, %d, %s",
			uuid_utoa( loc.gfid ), offset, strerror (errno));
		goto out;
	}
		
	if (ret == 0)
		ret = glfs_loc_unlink( &loc );

out:
	loc_wipe (&loc);

	if ( object != NULL )
		glfs_h_close( object );

	glfs_subvol_done( fs, subvol );

	return ret;
}