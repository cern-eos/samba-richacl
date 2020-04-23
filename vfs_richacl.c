/*
 * Convert NFSv4 acls stored per http://www.suse.de/~agruen/nfs4acl/ to NT acls and vice versa.
 *
 * Copyright (C) Jiri Sasek, 2007
 * based on the foobar.c module which is copyrighted by Volker Lendecke
 * based on pvfs_acl_nfs4.c  Copyright (C) Andrew Tridgell 2006
 *
 * based on vfs_fake_acls:
 * Copyright (C) Tim Potter, 1999-2000
 * Copyright (C) Alexander Bokovoy, 2002
 * Copyright (C) Andrew Bartlett, 2002,2012
 * Copyright (C) Ralph Boehme 2017
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 */

/* Hardcoded for now. This should become a flag in Gitlab CI */
#define RICHACL_NOTSUPP 0

#include "includes.h"
#include "system/filesys.h"
#include "smbd/smbd.h"
#include "libcli/security/security_token.h"
#include "nfs4_acls.h"
#include "librpc/gen_ndr/ndr_nfs4acl.h"
#include "assert.h"

#include "sys/richacl.h"

struct richacl_config {
	char *xattr_name;
	struct richacl_params {
	    int use_root;
	} richacl_params;

	struct smbacl4_vfs_params nfs4_params;
};

#undef DBGC_CLASS
#define DBGC_CLASS DBGC_VFS

#ifdef RICHACL_NOTSUPP

/* apparently SMB_NO_SUPPORT is not defined, despite being one of the official SMB error codes */
#define NT_STATUS_SMB_NO_SUPPORT NT_STATUS_UNSUCCESSFUL

/* This is a dummy implementation that returns SMB_NO_SUPPORT all over the places */

static NTSTATUS richacl_fget_nt_acl(struct vfs_handle_struct *handle,
				   struct files_struct *fsp,
				   uint32_t security_info,
				   TALLOC_CTX *mem_ctx,
				   struct security_descriptor **sd)
{
    return NT_STATUS_SMB_NO_SUPPORT;
}

static NTSTATUS richacl_get_nt_acl(struct vfs_handle_struct *handle,
								   const struct smb_filename *smb_fname,
								   uint32_t security_info,
								   TALLOC_CTX *mem_ctx,
								   struct security_descriptor **sd)
{
    return NT_STATUS_SMB_NO_SUPPORT;
}

static NTSTATUS richacl_fset_nt_acl(vfs_handle_struct *handle,
			 files_struct *fsp,
			 uint32_t security_info_sent,
			 const struct security_descriptor *psd)
{
    return NT_STATUS_SMB_NO_SUPPORT;
}

static int richacl_connect(struct vfs_handle_struct *handle,
			   const char *service,
			   const char *user)
{
	return -1;
}


#else   /* This is the real implementation to support Windows ACLs */


static NTSTATUS richacl_get_racl(struct vfs_handle_struct *handle,
				 files_struct *fsp,
				 const struct smb_filename *smb_fname_in,
				 TALLOC_CTX *mem_ctx,
				 struct richacl **_racl)
{
	struct richacl_config *config = NULL;
	const struct smb_filename *smb_fname = NULL;
	ssize_t length;
	int saved_errno = 0;
	struct richacl *racl;
	struct richace *race;
	DATA_BLOB blob = data_blob_null;

	SMB_VFS_HANDLE_GET_DATA(handle, config,
				struct richacl_config,
				return NT_STATUS_INTERNAL_ERROR);

	if (fsp == NULL && smb_fname_in == NULL) {
		return NT_STATUS_INTERNAL_ERROR;
	}

	smb_fname = smb_fname_in;
	if (smb_fname == NULL) {
		smb_fname = fsp->fsp_name;
	}
	if (smb_fname == NULL) {
		return NT_STATUS_INTERNAL_ERROR;
	}
	DBG_NOTICE("'%s'\n", smb_fname->base_name);

	if (fsp != NULL && fsp->fh->fd != -1)
	    length = SMB_VFS_NEXT_FGETXATTR(handle, fsp, config->xattr_name, NULL, 0);
	else
	    length = SMB_VFS_NEXT_GETXATTR(handle, smb_fname, config->xattr_name, blob.data, blob.length);
	DBG_NOTICE("l=%ld\n", length);

	SMB_STRUCT_STAT sbuf = smb_fname->st;
	if (!VALID_STAT(sbuf)) {
	    DBG_NOTICE("sbuf.st_ex_nlink=%d\n", (int) sbuf.st_ex_nlink);
	    DBG_NOTICE("sbuf not valid\n");
	    int rc;

	    rc = vfs_stat_smb_basename(handle->conn, smb_fname, &sbuf);
	    DBG_NOTICE("vfs_stat_smb_basename rc=%d uid=%d gid=%d\n", rc, sbuf.st_ex_uid, sbuf.st_ex_gid);
	}


	if (length <= 0) {
	    if (!VALID_STAT(sbuf)) {
		    return map_nt_error_from_unix(errno);
	    }

	    racl = richacl_from_mode(sbuf.st_ex_mode);
	    DBG_NOTICE("richacl_from_mode(%#o) a_count=%d\n", sbuf.st_ex_mode, racl->a_count);
	} else {
	    bool ok = data_blob_realloc(mem_ctx, &blob, length);
	    if (!ok) return NT_STATUS_NO_MEMORY;

	    if (config->richacl_params.use_root) become_root();
	    if (fsp != NULL && fsp->fh->fd != -1) {
		length = SMB_VFS_NEXT_FGETXATTR(handle, fsp, config->xattr_name, blob.data, blob.length);
	    } else {
		length = SMB_VFS_NEXT_GETXATTR(handle, smb_fname, config->xattr_name, blob.data, blob.length);
	    }
	    if (length == -1) {
		saved_errno = errno;
	    }
	    if (config->richacl_params.use_root) unbecome_root();

	    if (saved_errno != 0) {
		errno = saved_errno;
		return map_nt_error_from_unix(errno);
	    }

	    racl = richacl_from_xattr(blob.data, blob.length);
	    DBG_DEBUG("found racl->a_count=%d\n", racl->a_count);
	}

	/* Check for denials and replace RICHACE_OWNER_SPECIAL_ID by real owner - looks like that's what Windows perfers */
	/* need to loop twice since order not guaranteed */
	bool has_deny = false;
	struct richace *owner_ace_allow=NULL, *owner_ace_deny=NULL, *group_ace_allow=NULL, *group_ace_deny=NULL;
	int surplus_owner_deny=0, surplus_owner_allow=0, surplus_group_deny=0, surplus_group_allow=0;
	struct richace *surplus_owner_deny_ace=NULL, *surplus_owner_allow_ace=NULL, *surplus_group_deny_ace=NULL, *surplus_group_allow_ace=NULL;

        richacl_for_each_entry(race, racl) {
	    DBG_DEBUG("richace type [%d] flags [%#x] mask [%#x] who [%d] (file uid %d gid %d)\n",
		  race->e_type, race->e_flags,
		  race->e_mask, race->e_id,
		  sbuf.st_ex_uid, sbuf.st_ex_gid);
	    if (race->e_type == RICHACE_ACCESS_DENIED_ACE_TYPE) has_deny = true;

	    if (race->e_flags & RICHACE_SPECIAL_WHO) {
		char *allowdeny=NULL;
		if (race->e_id == RICHACE_OWNER_SPECIAL_ID) {
		    if (race->e_type == RICHACE_ACCESS_DENIED_ACE_TYPE) {
			owner_ace_deny = race;
			allowdeny = "deny";
		    } else {
			owner_ace_allow = race;
			allowdeny = "allow";
		    }
		    race->e_id = sbuf.st_ex_uid;
		    race->e_flags &= ~RICHACE_SPECIAL_WHO;
		} else if (race->e_id == RICHACE_GROUP_SPECIAL_ID) {
		    if (race->e_type == RICHACE_ACCESS_DENIED_ACE_TYPE) {
			group_ace_deny = race;
			allowdeny = "deny";
		    } else {
			group_ace_allow = race;
			allowdeny = "allow";
		    }
		    race->e_id = sbuf.st_ex_gid;
		    race->e_flags |= RICHACE_IDENTIFIER_GROUP;
		    race->e_flags &= ~RICHACE_SPECIAL_WHO;
		}
		if (allowdeny)
		    DBG_DEBUG("special e_id %d replaced by %d on %s\n", RICHACE_OWNER_SPECIAL_ID,  sbuf.st_ex_uid, allowdeny);
	    } else {	/* specific user or group entry */
		if ( ( (race->e_flags & RICHACE_IDENTIFIER_GROUP) && race->e_id == sbuf.st_ex_gid ) ||
		     ( (race->e_flags & RICHACE_IDENTIFIER_GROUP) == 0 && race->e_id == sbuf.st_ex_uid ) ) {

		    if (race->e_type == RICHACE_ACCESS_DENIED_ACE_TYPE) {
			if (race->e_flags & RICHACE_IDENTIFIER_GROUP) {
			    surplus_group_deny++;
			    surplus_group_deny_ace = race;
			} else {
			    surplus_owner_deny++;
			    surplus_owner_deny_ace = race;
			}
		    } else {	/* allow entry */
			if (race->e_flags & RICHACE_IDENTIFIER_GROUP) {
			    surplus_group_allow++;
			    surplus_group_allow_ace = race;
			} else {
			    surplus_owner_allow++;
			    surplus_owner_allow_ace = race;
			}
		    }

		}
	    }
	}

	/* create a new ACL with denies up front and OWNER and GROUP entries collapsed */
	if (has_deny || owner_ace_allow==NULL ||
		surplus_owner_deny || surplus_group_deny || surplus_owner_allow || surplus_group_allow) {
	    int cnt = racl->a_count - surplus_owner_deny - surplus_group_deny - surplus_owner_allow - surplus_group_allow;
	    if (owner_ace_allow == NULL) cnt++;
	    if (surplus_owner_deny > 0 && owner_ace_deny == NULL) cnt++;
	    if (surplus_group_deny > 0 && group_ace_deny == NULL) cnt++;
	    if (surplus_group_allow > 0 && group_ace_allow == NULL) cnt++;

	    DBG_DEBUG("has_deny %d surplus_owner_deny %d surplus_group_deny %d surplus_owner_allow %d surplus_group_allow %d\n",
		    has_deny, surplus_owner_deny, surplus_group_deny, surplus_owner_allow, surplus_group_allow);
	    DBG_DEBUG("owner_ace_allow %p group_ace_allow %p owner_ace_deny %p group_ace_deny %p\n",
		    owner_ace_allow, group_ace_allow, owner_ace_deny, group_ace_deny);
		    

	    struct richace *owner_ace_allow2 = NULL, *owner_ace_deny2 = NULL, *group_ace_allow2 = NULL, *group_ace_deny2 = NULL;
	    struct richacl *racl2 = richacl_alloc(cnt);
	    struct richace *race2 = &(racl2->a_entries[0]);
	    DBG_DEBUG("realloced ACL with %d entries, from %p\n", cnt, race2);

	    if (has_deny) {
		if (owner_ace_deny || surplus_owner_deny) {
		    owner_ace_deny2 = race2++;
		    richace_copy(owner_ace_deny2, (owner_ace_deny) ? owner_ace_deny : surplus_owner_deny_ace); 
		}


		if (group_ace_deny || surplus_group_deny) {
		    group_ace_deny2 = race2++;
		    richace_copy(group_ace_deny2, (group_ace_deny) ? group_ace_deny : surplus_group_deny_ace); 
		}

		/* copy all deny entries */
		if (has_deny) richacl_for_each_entry(race, racl) {
		    if (race->e_type == RICHACE_ACCESS_DENIED_ACE_TYPE) {
			bool needcopy = false;
			if (race->e_flags & RICHACE_IDENTIFIER_GROUP) {
			    if (race->e_id == sbuf.st_ex_gid) { /* use owner's group entry */
				group_ace_deny2->e_mask |= race->e_mask;
			    } else needcopy = true;
			} else { /* use owner deny entry */
			    if (race->e_id == sbuf.st_ex_uid) { /* use owner's group entry */
				owner_ace_deny2->e_mask |= race->e_mask;
			    } else needcopy = true;
			}
			if (needcopy) {
			    richace_copy(race2, race);
			    race2++;
			    assert ((race2-&(racl2->a_entries[0])) <= cnt);
			}
		    }
		}
	    }

	    /* Make sure an 'owner' ACL granting C & a exists (write_acl & read_attributes), otherwise owner cannot change in Win */
	    owner_ace_allow2 = race2++;
	    assert ((race2-&(racl2->a_entries[0])) <= cnt);
	    if (!owner_ace_allow) owner_ace_allow = surplus_owner_allow_ace;
	    if (owner_ace_allow) richace_copy(owner_ace_allow2, owner_ace_allow); 
	    else {
		owner_ace_allow2->e_type = RICHACE_ACCESS_ALLOWED_ACE_TYPE;
		owner_ace_allow2->e_id = sbuf.st_ex_uid;
	    }
	    owner_ace_allow2->e_mask |= RICHACE_WRITE_ACL|RICHACE_WRITE_ATTRIBUTES;

	    if (group_ace_allow || surplus_group_allow) {
		group_ace_allow2 = race2++;
		assert ((race2-&(racl2->a_entries[0])) <= cnt);
		richace_copy(group_ace_allow2, (group_ace_allow) ? group_ace_allow : surplus_group_allow_ace); 
	    }
	    
	    /* copy all other entries */
	    richacl_for_each_entry(race, racl) {
		if (race->e_type != RICHACE_ACCESS_DENIED_ACE_TYPE) {
		    bool needcopy = false;
		    if (race->e_flags & RICHACE_IDENTIFIER_GROUP) {
			if (race->e_id == sbuf.st_ex_gid) { /* use owner's group entry */
			    group_ace_allow2->e_mask |= race->e_mask;
			} else needcopy = true;
		    } else { /* use owner deny entry */
			if (race->e_id == sbuf.st_ex_uid) { /* use owner's group entry */
			    owner_ace_allow2->e_mask |= race->e_mask;
			} else needcopy = true;
		    }
		    if (needcopy) {
			DBG_DEBUG("copy ace2 %ld e_id %d uid %d gid %d\n", race2-&(racl2->a_entries[0]),
				race->e_id, sbuf.st_ex_uid, sbuf.st_ex_gid);
			richace_copy(race2, race);
			race2++;
			assert ((race2-&(racl2->a_entries[0])) <= cnt);
		    }
		}
	    }

	    racl2->a_count = race2-&(racl2->a_entries[0]);

	    DBG_DEBUG("used %d entries out of %d\n", racl2->a_count, cnt);

	    richacl_free(racl);
	    racl = racl2;
	}

	DBG_DEBUG("resulting acl for %s a_count %d:\n", smb_fname->base_name, racl->a_count);
	richacl_for_each_entry(race, racl) {
	    DBG_DEBUG("richace type [%d] flags [%#x] mask [%#x] who [%d]\n",
		  race->e_type, race->e_flags,
		  race->e_mask, race->e_id);
	}
	*_racl = racl;
	return NT_STATUS_OK;
}

static NTSTATUS richacl_racl_to_smb4acl(struct vfs_handle_struct *handle,
				   TALLOC_CTX *mem_ctx,
				   const struct smb_filename *smb_fname,
				   struct richacl *racl,
				   struct SMB4ACL_T **_smb4acl)
{
	struct richacl_config *config = NULL;
	struct SMB4ACL_T *smb4acl = NULL;
	struct richace *race;

	DBG_DEBUG("racl->a_count=%d\n", racl->a_count);

	SMB_VFS_HANDLE_GET_DATA(handle, config,
				struct richacl_config,
				return NT_STATUS_INTERNAL_ERROR);

	SMB_STRUCT_STAT sbuf = smb_fname->st;
	if (!VALID_STAT(sbuf)) {
	    DBG_NOTICE("sbuf.st_ex_nlink=%d\n", (int) sbuf.st_ex_nlink);
	    DBG_NOTICE("sbuf not valid\n");
	    int rc;

	    rc = vfs_stat_smb_basename(handle->conn, smb_fname, &sbuf);
	    DBG_NOTICE("vfs_stat_smb_basename rc=%d\n", rc);
	}

	smb4acl = smb_create_smb4acl(mem_ctx);
	if (smb4acl == NULL) {
	    DBG_WARNING("smb4acl=%p\n", smb4acl);
	    return NT_STATUS_INTERNAL_ERROR;
	}

#if 0
	unsigned nfsacl41_flag = 0;
	uint16_t smb4acl_flags = 0;
	if (config->nfs_version > ACL4_XATTR_VERSION_40) {
		nfsacl41_flag = nfs4acl_get_flags(nacl);
		smb4acl_flags = nfs4acl_to_smb4acl_flags(nfsacl41_flag);
		smbacl4_set_controlflags(smb4acl, smb4acl_flags);
	}
#endif

        richacl_for_each_entry(race, racl) {
		SMB_ACE4PROP_T smbace = { 0 };

		DBG_DEBUG("race type [%d] flags [%#x] mask [%#x] who [%d]\n",
			  race->e_type, race->e_flags,
			  race->e_mask, race->e_id);

		smbace.aceType = race->e_type;		/* actually, only 0 & 1 exist in richacl */
		smbace.aceFlags = race->e_flags & 0xff;	/* they mostly match */
		smbace.aceMask = (race->e_mask | SMB_ACE4_READ_ATTRIBUTES | SMB_ACE4_READ_ACL | SMB_ACE4_READ_NAMED_ATTRS /* always allowed on Unix */);	/* so do these */

		/* stolen from vfs_zfsacl.c */
		if (smbace.aceType == SMB_ACE4_ACCESS_ALLOWED_ACE_TYPE) {
			smbace.aceMask |= SMB_ACE4_SYNCHRONIZE;
		}

		if (race->e_flags & RICHACE_SPECIAL_WHO) {
			smbace.flags |= SMB_ACE4_ID_SPECIAL;

			switch (race->e_id) {
			case RICHACE_OWNER_SPECIAL_ID:
				smbace.who.special_id = SMB_ACE4_WHO_OWNER;

			case RICHACE_GROUP_SPECIAL_ID:
				smbace.who.special_id = SMB_ACE4_WHO_GROUP;
				break;

			case RICHACE_EVERYONE_SPECIAL_ID:
				smbace.who.special_id = SMB_ACE4_WHO_EVERYONE;
				break;

			default:
				DBG_ERR("Unknown special id [%d]\n", race->e_id);
				continue;
			}
		} else {
			if (race->e_flags & RICHACE_IDENTIFIER_GROUP) {
				smbace.who.gid = race->e_id;
			} else {
				smbace.who.uid = race->e_id;
			}
		}

		if (race->e_id == sbuf.st_ex_uid ||
			(race->e_id == RICHACE_OWNER_SPECIAL_ID && (race->e_flags & RICHACE_SPECIAL_WHO)))
		{
		    if (smbace.aceType == SMB_ACE4_ACCESS_ALLOWED_ACE_TYPE) {
			smbace.aceMask |= SMB_ACE4_WRITE_ATTRIBUTES |
			    SMB_ACE4_READ_NAMED_ATTRS | SMB_ACE4_WRITE_NAMED_ATTRS |
			    SMB_ACE4_DELETE | SMB_ACE4_READ_ACL | SMB_ACE4_WRITE_ACL |
			    SMB_ACE4_WRITE_OWNER ;	    /* implicitely granted for owner on Unix */
		    }
		}

		/*smbace.aceFlags |= (SEC_ACE_FLAG_OBJECT_INHERIT|SEC_ACE_FLAG_CONTAINER_INHERIT);	  guesswork */


		smb_add_ace4(smb4acl, &smbace);

		DBG_DEBUG("flags [%#x] aceType [%d] aceFlags [%#x] aceMask [%#x] who [%d]\n", smbace.flags,
			  smbace.aceType, smbace.aceFlags,
			  smbace.aceMask, smbace.who.id);
	}

	*_smb4acl = smb4acl;
	return NT_STATUS_OK;
}

struct SMB4ACE_T
{
	SMB_ACE4PROP_T	prop;
	struct SMB4ACE_T *next;
};

struct SMB4ACL_T
{
	uint16_t controlflags;
	uint32_t naces;
	struct SMB4ACE_T	*first;
	struct SMB4ACE_T	*last;
};

static void smbacl4_dump_nfs4acl(int level, struct SMB4ACL_T *acl)
{

	struct SMB4ACE_T *aceint;

	DEBUG(level, ("NFS4ACL: size=%d\n", acl->naces));

	for (aceint = acl->first; aceint != NULL; aceint = aceint->next) {
		SMB_ACE4PROP_T *ace = &aceint->prop;

		DEBUG(level, ("\tACE: type=%d, flags=0x%x, fflags=0x%x, "
			      "mask=0x%x, id=%d\n",
			      ace->aceType,
			      ace->aceFlags, ace->flags,
			      ace->aceMask,
			      ace->who.id));
	}
}

static NTSTATUS richacl_fget_nt_acl(struct vfs_handle_struct *handle,
				   struct files_struct *fsp,
				   uint32_t security_info,
				   TALLOC_CTX *mem_ctx,
				   struct security_descriptor **sd)
{
	struct SMB4ACL_T *smb4acl = NULL;
	struct richacl *racl = NULL;
	NTSTATUS status;
	TALLOC_CTX *frame = talloc_stackframe();

	DBG_NOTICE("\n");

	/* get richacl */
	status = richacl_get_racl(handle, fsp, NULL, frame, &racl);
	if (racl == NULL) {
	    DBG_NOTICE("racl=%p\n", racl);
	    TALLOC_FREE(frame);
	    return NT_STATUS_NOT_FOUND;
	}

	/* convert to nfs4acl */
	status = richacl_racl_to_smb4acl(handle, frame, fsp->fsp_name, racl, &smb4acl);
	DBG_NOTICE("richacl_racl_to_smb4acl status=%d\n", NT_STATUS_V(status));
	richacl_free(racl);
	if (!NT_STATUS_IS_OK(status)) {
		TALLOC_FREE(frame);
		return status;
	}

	status = smb_fget_nt_acl_nfs4(fsp, NULL, security_info, mem_ctx,
				      sd, smb4acl);
	DBG_NOTICE("smb_fget_nt_acl_nfs4 status=%d\n", NT_STATUS_V(status));
	TALLOC_FREE(frame);
	return status;
}

static NTSTATUS richacl_get_nt_acl(struct vfs_handle_struct *handle,
				  const struct smb_filename *smb_fname,
				  uint32_t security_info,
				  TALLOC_CTX *mem_ctx,
				  struct security_descriptor **sd)
{
	struct SMB4ACL_T *smb4acl = NULL;
	TALLOC_CTX *frame = talloc_stackframe();
	struct richacl *racl = NULL;
	NTSTATUS status;

	status = richacl_get_racl(handle, NULL, smb_fname, frame, &racl);

	if (racl == NULL) {
	    DBG_NOTICE("racl=%p\n", racl);
	    TALLOC_FREE(frame);
	    return NT_STATUS_NOT_FOUND;
	}
	DBG_NOTICE("racl->a_count=%d\n", racl->a_count);

	status = richacl_racl_to_smb4acl(handle, frame, smb_fname, racl, &smb4acl);
	richacl_free(racl);
	DBG_NOTICE("richacl_racl_to_smb4acl status=%d\n", NT_STATUS_V(status));
	if (!NT_STATUS_IS_OK(status)) {
		TALLOC_FREE(frame);
		return status;
	}
	smbacl4_dump_nfs4acl(DBGLVL_NOTICE, smb4acl);

	status = smb_get_nt_acl_nfs4(handle->conn, smb_fname, NULL,
				     security_info, mem_ctx, sd,
				     smb4acl);
	DBG_NOTICE("smb_get_nt_acl_nfs4 status=%d\n", NT_STATUS_V(status));


	TALLOC_FREE(frame);
	return status;
}

static NTSTATUS richacl_smb4acl_to_richacl_blob(vfs_handle_struct *handle,
	TALLOC_CTX *mem_ctx,
	struct SMB4ACL_T *smb4acl,
	DATA_BLOB *blob)
{

	struct richacl_config *config = NULL;
	struct richacl *racl;

	struct SMB4ACE_T *smb4ace = NULL;
	size_t smb4naces = 0;
	uint16_t smb4acl_flags = 0;
	DBG_NOTICE("\n");

	SMB_VFS_HANDLE_GET_DATA(handle, config,
				struct richacl_config,
				return NT_STATUS_INTERNAL_ERROR);

	smb4naces = smb_get_naces(smb4acl);
	DBG_NOTICE("smb4naces=%ld\n", smb4naces);

	racl = richacl_alloc(smb4naces);
	memset(racl, 0, sizeof(*racl));	    /* init */

	smb4acl_flags = smbacl4_get_controlflags(smb4acl);
	DBG_NOTICE("smb4acl_flags=%#x\n", smb4acl_flags);
	if (smb4acl_flags & SEC_DESC_DACL_AUTO_INHERITED) {
		racl->a_flags |= RICHACL_AUTO_INHERIT;
	}
	if (smb4acl_flags & SEC_DESC_DACL_PROTECTED) {
		racl->a_flags |= RICHACL_PROTECTED;
	}
	if (smb4acl_flags & SEC_DESC_DACL_DEFAULTED) {
		racl->a_flags |= RICHACL_DEFAULTED;
	}

	smb4ace = smb_first_ace4(smb4acl);
	while (smb4ace != NULL) {
		SMB_ACE4PROP_T *ace4prop = smb_get_ace4(smb4ace);
	        struct richace *race = &(racl->a_entries[racl->a_count]);
	        memset(race, 0, sizeof(*race));                   /* makes the ace an "allowed" type entry in passing */

		race->e_type = ace4prop->aceType;
		race->e_flags = ace4prop->aceFlags;
		race->e_mask = ace4prop->aceMask;

		if (ace4prop->flags & SMB_ACE4_ID_SPECIAL) {
			DBG_DEBUG("special id %d\n", ace4prop->who.special_id);
			race->e_flags |= RICHACE_SPECIAL_WHO;

			switch (ace4prop->who.special_id) {
			case SMB_ACE4_WHO_OWNER:
				race->e_id = RICHACE_OWNER_SPECIAL_ID;
				break;

			case SMB_ACE4_WHO_GROUP:
				race->e_id = RICHACE_GROUP_SPECIAL_ID;
				break;

			case SMB_ACE4_WHO_EVERYONE:
				race->e_id = RICHACE_EVERYONE_SPECIAL_ID;
				break;

			default:
				DBG_ERR("Unsupported special id [%d]\n",
					ace4prop->who.special_id);
				continue;
			}
		} else {
			if (ace4prop->aceFlags & SMB_ACE4_IDENTIFIER_GROUP) {
				race->e_flags|= RICHACE_IDENTIFIER_GROUP;
				race->e_id = ace4prop->who.gid;
			} else {
				race->e_id = ace4prop->who.uid;
			}
		}

		racl->a_count++;
		DBG_DEBUG("racl->a_count=%d\n", racl->a_count);
		smb4ace = smb_next_ace4(smb4ace);
	}

	size_t blobSz = richacl_xattr_size(racl);
	DBG_DEBUG("blobsz=%ld\n", blobSz);
	if (!data_blob_realloc(mem_ctx, blob, blobSz))
		return NT_STATUS_NO_MEMORY;

	richacl_to_xattr(racl, blob->data);
	return NT_STATUS_OK;
}

static bool richacl_smb4acl_set_fn(vfs_handle_struct *handle,
				   files_struct *fsp,
				   struct SMB4ACL_T *smb4acl)
{
	struct richacl_config *config = NULL;
	DATA_BLOB blob = data_blob_null;

	NTSTATUS status;
	int saved_errno = 0;
	int ret;

	SMB_VFS_HANDLE_GET_DATA(handle, config,
				struct richacl_config,
				return false);

	status = richacl_smb4acl_to_richacl_blob(handle, talloc_tos(), smb4acl, &blob);
	if (!NT_STATUS_IS_OK(status)) {
		return false;
	}

	DBG_NOTICE("blob.length=%ld fsp->fh->fd=%d\n", blob.length, fsp->fh->fd);

	if (config->richacl_params.use_root) become_root();
	if (fsp->fh->fd != -1) {
		ret = SMB_VFS_NEXT_FSETXATTR(handle, fsp, config->xattr_name,
					     blob.data, blob.length, 0);
	} else {
		ret = SMB_VFS_NEXT_SETXATTR(handle, fsp->fsp_name, config->xattr_name,
					    blob.data, blob.length, 0);
	}
	if (ret != 0) {
		saved_errno = errno;
	}
	if (config->richacl_params.use_root) unbecome_root();
	data_blob_free(&blob);
	if (saved_errno != 0) {
		errno = saved_errno;
	}
	if (ret != 0) {
		DBG_ERR("can't store acl in xattr %s: %s\n", config->xattr_name, strerror(errno));
		return false;
	}

	return true;
}

static NTSTATUS richacl_fset_nt_acl(vfs_handle_struct *handle,
			 files_struct *fsp,
			 uint32_t security_info_sent,
			 const struct security_descriptor *psd)
{
	struct richacl_config *config = NULL;
	const struct security_token *token = NULL;
	mode_t existing_mode;
	mode_t expected_mode;
	mode_t restored_mode;
	bool chown_needed = false;
	NTSTATUS status;
	int ret = 0;

	DBG_NOTICE("richacl_fset_nt_acl %s\n", fsp->fsp_name->base_name);
	SMB_VFS_HANDLE_GET_DATA(handle, config,
				struct richacl_config,
				return NT_STATUS_INTERNAL_ERROR);

	if (!VALID_STAT(fsp->fsp_name->st)) {
		DBG_ERR("Invalid stat info on [%s]\n", fsp_str_dbg(fsp));
		return NT_STATUS_INTERNAL_ERROR;
	}

	existing_mode = fsp->fsp_name->st.st_ex_mode;
	if (S_ISDIR(existing_mode)) {
		expected_mode = 0777;
	} else {
		expected_mode = 0666;
	}
	if ((existing_mode & expected_mode) != expected_mode) {
		int saved_errno = 0;

		restored_mode = existing_mode | expected_mode;

#if 0
		if (config->richacl_params.use_root) become_root();
		if (fsp->fh->fd != -1) {
			ret = SMB_VFS_NEXT_FCHMOD(handle,
						  fsp,
						  restored_mode);
		} else {
			ret = SMB_VFS_NEXT_CHMOD(handle,
						 fsp->fsp_name,
						 restored_mode);
		}
		if (ret != 0) {
			saved_errno = errno;
		}
		if (config->richacl_params.use_root) unbecome_root();
#endif
		if (saved_errno != 0) {
			errno = saved_errno;
		}
		if (ret != 0) {
			DBG_ERR("Resetting POSIX mode on [%s] from [0%o] to [%#o]: %s\n",
				fsp_str_dbg(fsp), existing_mode, restored_mode,
				strerror(errno));
			return map_nt_error_from_unix(errno);
		}
	}

	status = smb_set_nt_acl_nfs4(handle,
				     fsp,
				     &config->nfs4_params,
				     security_info_sent,
				     psd,
				     richacl_smb4acl_set_fn);
	if (NT_STATUS_IS_OK(status)) {
		return NT_STATUS_OK;
	}

	/*
	 * We got access denied. If we're already root, or we didn't
	 * need to do a chown, or the fsp isn't open with WRITE_OWNER
	 * access, just return.
	 */

	if ((security_info_sent & SECINFO_OWNER) &&
	    (psd->owner_sid != NULL))
	{
		chown_needed = true;
	}
	if ((security_info_sent & SECINFO_GROUP) &&
	    (psd->group_sid != NULL))
	{
		chown_needed = true;
	}

	if (get_current_uid(handle->conn) == 0 ||
	    chown_needed == false ||
	    !(fsp->access_mask & SEC_STD_WRITE_OWNER))
	{
		return NT_STATUS_ACCESS_DENIED;
	}

	/*
	 * Only allow take-ownership, not give-ownership. That's the way Windows
	 * implements SEC_STD_WRITE_OWNER. MS-FSA 2.1.5.16 just states: If
	 * InputBuffer.OwnerSid is not a valid owner SID for a file in the
	 * objectstore, as determined in an implementation specific manner, the
	 * object store MUST return STATUS_INVALID_OWNER.
	 */
	token = get_current_nttok(fsp->conn);
	if (!security_token_is_sid(token, psd->owner_sid)) {
		return NT_STATUS_INVALID_OWNER;
	}

	DBG_DEBUG("overriding chown on file %s for sid %s\n",
		  fsp_str_dbg(fsp), sid_string_tos(psd->owner_sid));

	if (config->richacl_params.use_root) become_root();
	status = smb_set_nt_acl_nfs4(handle,
				     fsp,
				     &config->nfs4_params,
				     security_info_sent,
				     psd,
				     richacl_smb4acl_set_fn);
	if (config->richacl_params.use_root) unbecome_root();
	return status;
}

static int richacl_connect(struct vfs_handle_struct *handle,
			   const char *service,
			   const char *user)
{
	struct richacl_config *config = NULL;
	const char *default_xattr_name = "system.richacl";
	int ret;

	DBG_NOTICE("\n");

	config = talloc_zero(handle->conn, struct richacl_config);
	if (config == NULL) {
		DBG_ERR("talloc_zero() failed\n");
		return -1;
	}

	ret = SMB_VFS_NEXT_CONNECT(handle, service, user);
	if (ret < 0) {
		TALLOC_FREE(config);
		return ret;
	}

	ret = smbacl4_get_vfs_params(handle->conn, &config->nfs4_params);
	if (ret < 0) {
		TALLOC_FREE(config);
		return ret;
	}

#if 0
	int enumval;
	const struct enum_list *default_acl_style_list = NULL;
	default_acl_style_list = get_default_acl_style_list();

	enumval = lp_parm_enum(SNUM(handle->conn),
			       "nfs4acl_xattr",
			       "encoding",
			       nfs4acl_encoding,
			       NFS4ACL_ENCODING_NDR);
	if (enumval == -1) {
		DBG_ERR("Invalid \"nfs4acl_xattr:encoding\" parameter\n");
		return -1;
	}
	config->encoding = (enum nfs4acl_encoding)enumval;

	switch (config->encoding) {
	case NFS4ACL_ENCODING_XDR:
		default_xattr_name = NFS4ACL_XDR_XATTR_NAME;
		break;
	case NFS4ACL_ENCODING_NDR:
	default:
		default_xattr_name = NFS4ACL_NDR_XATTR_NAME;
		break;
	}

	nfs_version = (unsigned)lp_parm_int(SNUM(handle->conn),
					    "nfs4acl_xattr",
					    "version",
					    41);
	switch (nfs_version) {
	case 40:
		config->nfs_version = ACL4_XATTR_VERSION_40;
		break;
	case 41:
		config->nfs_version = ACL4_XATTR_VERSION_41;
		break;
	default:
		config->nfs_version = ACL4_XATTR_VERSION_DEFAULT;
		break;
	}

	config->default_acl_style = lp_parm_enum(SNUM(handle->conn),
						 "nfs4acl_xattr",
						 "default acl style",
						 default_acl_style_list,
						 DEFAULT_ACL_EVERYONE);
#endif

	config->xattr_name = lp_parm_talloc_string(config,
						   SNUM(handle->conn),
						   default_xattr_name,
						   "xattr_name",
						   default_xattr_name);

	DBG_NOTICE("richacl xattr name %s\n", config->xattr_name);

	config->richacl_params.use_root = (int) lp_parm_int(SNUM(handle->conn), "richacl", "use_root", 0);
	DBG_NOTICE("use_root %d\n", config->richacl_params.use_root);

	SMB_VFS_HANDLE_SET_DATA(handle, config, NULL, struct nfs4acl_config,
				return -1);

	/*
	 * Ensure we have the parameters correct if we're using this module.
	DBG_NOTICE("Setting 'inherit acls = true', "
		   "'dos filemode = true', "
		   "'force unknown acl user = true', "
		   "'create mask = 0666', "
		   "'directory mask = 0777' and "
		   "'store dos attributes = yes' "
		   "for service [%s]\n", service);
	 */

	/*
	lp_do_parameter(SNUM(handle->conn), "inherit acls", "true");
	lp_do_parameter(SNUM(handle->conn), "dos filemode", "true");
	lp_do_parameter(SNUM(handle->conn), "force unknown acl user", "true");
	lp_do_parameter(SNUM(handle->conn), "create mask", "0666");
	lp_do_parameter(SNUM(handle->conn), "directory mask", "0777");
	lp_do_parameter(SNUM(handle->conn), "store dos attributes", "yes");
	*/

	return 0;
}


#endif  /* #ifdef RICHACL_NOTSUPP */

/* --- Common part --- */

/*
   As long as Samba does not support an exiplicit method for a module
   to define conflicting vfs methods, we should override all conflicting
   methods here.  That way, we know we are using the NFSv4 storage

   Function declarations taken from vfs_solarisacl
*/

static SMB_ACL_T richacl_fail__sys_acl_get_file(vfs_handle_struct *handle,
					const struct smb_filename *smb_fname,
					SMB_ACL_TYPE_T type,
					TALLOC_CTX *mem_ctx)
{
	return (SMB_ACL_T)NULL;
}

static SMB_ACL_T richacl_fail__sys_acl_get_fd(vfs_handle_struct *handle,
						    files_struct *fsp,
						    TALLOC_CTX *mem_ctx)
{
	return (SMB_ACL_T)NULL;
}

static int richacl_fail__sys_acl_set_file(vfs_handle_struct *handle,
					 const struct smb_filename *smb_fname,
					 SMB_ACL_TYPE_T type,
					 SMB_ACL_T theacl)
{
	return -1;
}

static int richacl_fail__sys_acl_set_fd(vfs_handle_struct *handle,
				       files_struct *fsp,
				       SMB_ACL_T theacl)
{
	return -1;
}

static int richacl_fail__sys_acl_delete_def_file(vfs_handle_struct *handle,
			const struct smb_filename *smb_fname)
{
	return -1;
}

static int richacl_fail__sys_acl_blob_get_file(vfs_handle_struct *handle,
			const struct smb_filename *smb_fname,
			TALLOC_CTX *mem_ctx,
			char **blob_description,
			DATA_BLOB *blob)
{
	return -1;
}

static int richacl_fail__sys_acl_blob_get_fd(vfs_handle_struct *handle, files_struct *fsp, TALLOC_CTX *mem_ctx, char **blob_description, DATA_BLOB *blob)
{
	return -1;
}

/* VFS operations structure */

static struct vfs_fn_pointers richacl_fns = {
	.connect_fn = richacl_connect,
	.fget_nt_acl_fn = richacl_fget_nt_acl,
	.get_nt_acl_fn = richacl_get_nt_acl,
	.fset_nt_acl_fn = richacl_fset_nt_acl,

	.sys_acl_get_file_fn = richacl_fail__sys_acl_get_file,
	.sys_acl_get_fd_fn = richacl_fail__sys_acl_get_fd,
	.sys_acl_blob_get_file_fn = richacl_fail__sys_acl_blob_get_file,
	.sys_acl_blob_get_fd_fn = richacl_fail__sys_acl_blob_get_fd,
	.sys_acl_set_file_fn = richacl_fail__sys_acl_set_file,
	.sys_acl_set_fd_fn = richacl_fail__sys_acl_set_fd,
	.sys_acl_delete_def_file_fn = richacl_fail__sys_acl_delete_def_file,
};

static_decl_vfs;
NTSTATUS vfs_richacl_init(TALLOC_CTX *ctx)
{
	return smb_register_vfs(SMB_VFS_INTERFACE_VERSION, "richacl",
				&richacl_fns);
}
