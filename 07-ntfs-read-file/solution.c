#include <solution.h>
#include <unistd.h>
#include <limits.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#define _STRUCT_TIMESPEC 1
#include <stdlib.h>
#include <string.h>
#include <ntfs-3g/volume.h>
#include <ntfs-3g/dir.h>

#include <ntfs-3g/attrib.h>
#include <ntfs-3g/index.h>
#include <ntfs-3g/param.h>
#include <ntfs-3g/types.h>
#include <ntfs-3g/debug.h>
#include <ntfs-3g/attrlist.h>
#include <ntfs-3g/inode.h>
#include <ntfs-3g/compat.h>
#include <ntfs-3g/efs.h>
#include <ntfs-3g/mft.h>
#include <ntfs-3g/index.h>
#include <ntfs-3g/ntfstime.h>
#include <ntfs-3g/lcnalloc.h>
#include <ntfs-3g/logging.h>
#include <ntfs-3g/cache.h>
#include <ntfs-3g/misc.h>
#include <ntfs-3g/security.h>
#include <ntfs-3g/reparse.h>
#include <ntfs-3g/object_id.h>
#include <ntfs-3g/xattrs.h>
#include <ntfs-3g/ea.h>
#include <stddef.h>

#define FILEPATHLEN 256
#define PAGE 4096


static int ntfs_attr_find(const ATTR_TYPES type, const ntfschar *name,
		const u32 name_len, const IGNORE_CASE_BOOL ic,
		const u8 *val, const u32 val_len, ntfs_attr_search_ctx *ctx)
{
	ATTR_RECORD *a;
	ntfs_volume *vol;
	ntfschar *upcase;
	ptrdiff_t offs;
	ptrdiff_t space;
	u32 upcase_len;

	ntfs_log_trace("attribute type 0x%x.\n", le32_to_cpu(type));

	if (ctx->ntfs_ino) {
		vol = ctx->ntfs_ino->vol;
		upcase = vol->upcase;
		upcase_len = vol->upcase_len;
	} else {
		if (name && name != AT_UNNAMED) {
			errno = EINVAL;
			ntfs_log_perror("%s", __FUNCTION__);
			return -1;
		}
		vol = NULL;
		upcase = NULL;
		upcase_len = 0;
	}
	/*
	 * Iterate over attributes in mft record starting at @ctx->attr, or the
	 * attribute following that, if @ctx->is_first is TRUE.
	 */
	if (ctx->is_first) {
		a = ctx->attr;
		ctx->is_first = FALSE;
	} else
		a = (ATTR_RECORD*)((char*)ctx->attr +
				le32_to_cpu(ctx->attr->length));
	for (;;	a = (ATTR_RECORD*)((char*)a + le32_to_cpu(a->length))) {
		/*
		 * Make sure the attribute fully lies within the MFT record
		 * and we can safely access its minimal fields.
		 */
		offs = p2n(a) - p2n(ctx->mrec);
		space = le32_to_cpu(ctx->mrec->bytes_in_use) - offs;
		if ((offs < 0)
		    || (((space < (ptrdiff_t)offsetof(ATTR_RECORD,
						resident_end))
			|| (space < (ptrdiff_t)le32_to_cpu(a->length)))
			    && ((space < 4) || (a->type != AT_END))))
			break;
		ctx->attr = a;
		if (((type != AT_UNUSED) && (le32_to_cpu(a->type) >
				le32_to_cpu(type))) ||
				(a->type == AT_END)) {
			errno = ENOENT;
			return -1;
		}
		if (!a->length)
			break;
		/* If this is an enumeration return this attribute. */
		if (type == AT_UNUSED)
			return 0;
		if (a->type != type)
			continue;
		/*
		 * If @name is AT_UNNAMED we want an unnamed attribute.
		 * If @name is present, compare the two names.
		 * Otherwise, match any attribute.
		 */
		if (name == AT_UNNAMED) {
			/* The search failed if the found attribute is named. */
			if (a->name_length) {
				errno = ENOENT;
				return -1;
			}
		} else {
			register int rc;

			if (a->name_length
			    && ((le16_to_cpu(a->name_offset)
					+ a->name_length * sizeof(ntfschar))
					> le32_to_cpu(a->length))) {
				ntfs_log_error("Corrupt attribute name"
					" in MFT record %lld\n",
					(long long)ctx->ntfs_ino->mft_no);
				break;
			}
			if (name && ((rc = ntfs_names_full_collate(name,
					name_len, (ntfschar*)((char*)a +
						le16_to_cpu(a->name_offset)),
					a->name_length, ic,
					upcase, upcase_len)))) {
				/*
				 * If @name collates before a->name,
				 * there is no matching attribute.
				 */
				if (rc < 0) {
					errno = ENOENT;
					return -1;
				}
			/* If the strings are not equal, continue search. */
			continue;
			}
		}
		/*
		 * The names match or @name not present and attribute is
		 * unnamed. If no @val specified, we have found the attribute
		 * and are done.
		 */
		if (!val)
			return 0;
		/* @val is present; compare values. */
		else {
			register int rc;

			rc = memcmp(val, (char*)a +le16_to_cpu(a->value_offset),
					min(val_len,
					le32_to_cpu(a->value_length)));
			/*
			 * If @val collates before the current attribute's
			 * value, there is no matching attribute.
			 */
			if (!rc) {
				register u32 avl;
				avl = le32_to_cpu(a->value_length);
				if (val_len == avl)
					return 0;
				if (val_len < avl) {
					errno = ENOENT;
					return -1;
				}
			} else if (rc < 0) {
				errno = ENOENT;
				return -1;
			}
		}
	}
	errno = EIO;
	ntfs_log_perror("%s: Corrupt inode (%lld)", __FUNCTION__, 
			ctx->ntfs_ino ? (long long)ctx->ntfs_ino->mft_no : -1);
	return -1;
}

static int ntfs_external_attr_find(ATTR_TYPES type, const ntfschar *name,
		const u32 name_len, const IGNORE_CASE_BOOL ic,
		const VCN lowest_vcn, const u8 *val, const u32 val_len,
		ntfs_attr_search_ctx *ctx)
{
	ntfs_inode *base_ni, *ni;
	ntfs_volume *vol;
	ATTR_LIST_ENTRY *al_entry, *next_al_entry;
	u8 *al_start, *al_end;
	ATTR_RECORD *a;
	ntfschar *al_name;
	ptrdiff_t offs;
	ptrdiff_t space;
	u32 al_name_len;
	BOOL is_first_search = FALSE;

	ni = ctx->ntfs_ino;
	base_ni = ctx->base_ntfs_ino;
	ntfs_log_trace("Entering for inode %lld, attribute type 0x%x.\n",
			(unsigned long long)ni->mft_no, le32_to_cpu(type));
	if (!base_ni) {
		/* First call happens with the base mft record. */
		base_ni = ctx->base_ntfs_ino = ctx->ntfs_ino;
		ctx->base_mrec = ctx->mrec;
	}
	if (ni == base_ni)
		ctx->base_attr = ctx->attr;
	if (type == AT_END)
		goto not_found;
	vol = base_ni->vol;
	al_start = base_ni->attr_list;
	al_end = al_start + base_ni->attr_list_size;
	if (!ctx->al_entry) {
		ctx->al_entry = (ATTR_LIST_ENTRY*)al_start;
		is_first_search = TRUE;
	}
	/*
	 * Iterate over entries in attribute list starting at @ctx->al_entry,
	 * or the entry following that, if @ctx->is_first is TRUE.
	 */
	if (ctx->is_first) {
		al_entry = ctx->al_entry;
		ctx->is_first = FALSE;
		/*
		 * If an enumeration and the first attribute is higher than
		 * the attribute list itself, need to return the attribute list
		 * attribute.
		 */
		if ((type == AT_UNUSED) && is_first_search &&
				le32_to_cpu(al_entry->type) >
				le32_to_cpu(AT_ATTRIBUTE_LIST))
			goto find_attr_list_attr;
	} else {
			/* Check for small entry */
		if (((p2n(al_end) - p2n(ctx->al_entry))
				< (long)offsetof(ATTR_LIST_ENTRY, name))
		    || (le16_to_cpu(ctx->al_entry->length) & 7)
		    || (le16_to_cpu(ctx->al_entry->length)
				< offsetof(ATTR_LIST_ENTRY, name)))
			goto corrupt;

		al_entry = (ATTR_LIST_ENTRY*)((char*)ctx->al_entry +
				le16_to_cpu(ctx->al_entry->length));
		if ((u8*)al_entry == al_end)
			goto not_found;
			/* Preliminary check for small entry */
		if ((p2n(al_end) - p2n(al_entry))
				< (long)offsetof(ATTR_LIST_ENTRY, name))
			goto corrupt;
		/*
		 * If this is an enumeration and the attribute list attribute
		 * is the next one in the enumeration sequence, just return the
		 * attribute list attribute from the base mft record as it is
		 * not listed in the attribute list itself.
		 */
		if ((type == AT_UNUSED) && le32_to_cpu(ctx->al_entry->type) <
				le32_to_cpu(AT_ATTRIBUTE_LIST) &&
				le32_to_cpu(al_entry->type) >
				le32_to_cpu(AT_ATTRIBUTE_LIST)) {
			int rc;
find_attr_list_attr:

			/* Check for bogus calls. */
			if (name || name_len || val || val_len || lowest_vcn) {
				errno = EINVAL;
				ntfs_log_perror("%s", __FUNCTION__);
				return -1;
			}

			/* We want the base record. */
			ctx->ntfs_ino = base_ni;
			ctx->mrec = ctx->base_mrec;
			ctx->is_first = TRUE;
			/* Sanity checks are performed elsewhere. */
			ctx->attr = (ATTR_RECORD*)((u8*)ctx->mrec +
					le16_to_cpu(ctx->mrec->attrs_offset));

			/* Find the attribute list attribute. */
			rc = ntfs_attr_find(AT_ATTRIBUTE_LIST, NULL, 0,
					IGNORE_CASE, NULL, 0, ctx);

			/*
			 * Setup the search context so the correct
			 * attribute is returned next time round.
			 */
			ctx->al_entry = al_entry;
			ctx->is_first = TRUE;

			/* Got it. Done. */
			if (!rc)
				return 0;

			/* Error! If other than not found return it. */
			if (errno != ENOENT)
				return rc;

			/* Not found?!? Absurd! */
			errno = EIO;
			ntfs_log_error("Attribute list wasn't found");
			return -1;
		}
	}
	for (;; al_entry = next_al_entry) {
		/* Out of bounds check. */
		if ((u8*)al_entry < base_ni->attr_list ||
				(u8*)al_entry > al_end)
			break;	/* Inode is corrupt. */
		ctx->al_entry = al_entry;
		/* Catch the end of the attribute list. */
		if ((u8*)al_entry == al_end)
			goto not_found;

		if ((((u8*)al_entry + offsetof(ATTR_LIST_ENTRY, name)) > al_end)
		    || ((u8*)al_entry + le16_to_cpu(al_entry->length) > al_end)
		    || (le16_to_cpu(al_entry->length) & 7)
		    || (le16_to_cpu(al_entry->length)
				< offsetof(ATTR_LIST_ENTRY, name_length))
		    || (al_entry->name_length
			&& ((u8*)al_entry + al_entry->name_offset
				+ al_entry->name_length * sizeof(ntfschar))
				> al_end))
			break; /* corrupt */

		next_al_entry = (ATTR_LIST_ENTRY*)((u8*)al_entry +
				le16_to_cpu(al_entry->length));
		if (type != AT_UNUSED) {
			if (le32_to_cpu(al_entry->type) > le32_to_cpu(type))
				goto not_found;
			if (type != al_entry->type)
				continue;
		}
		al_name_len = al_entry->name_length;
		al_name = (ntfschar*)((u8*)al_entry + al_entry->name_offset);
		/*
		 * If !@type we want the attribute represented by this
		 * attribute list entry.
		 */
		if (type == AT_UNUSED)
			goto is_enumeration;
		/*
		 * If @name is AT_UNNAMED we want an unnamed attribute.
		 * If @name is present, compare the two names.
		 * Otherwise, match any attribute.
		 */
		if (name == AT_UNNAMED) {
			if (al_name_len)
				goto not_found;
		} else {
			int rc;

			if (name && ((rc = ntfs_names_full_collate(name,
					name_len, al_name, al_name_len, ic,
					vol->upcase, vol->upcase_len)))) {

				/*
				 * If @name collates before al_name,
				 * there is no matching attribute.
				 */
				if (rc < 0)
					goto not_found;
				/* If the strings are not equal, continue search. */
				continue;
			}
		}
		/*
		 * The names match or @name not present and attribute is
		 * unnamed. Now check @lowest_vcn. Continue search if the
		 * next attribute list entry still fits @lowest_vcn. Otherwise
		 * we have reached the right one or the search has failed.
		 */
		if (lowest_vcn && (u8*)next_al_entry >= al_start	    &&
				(u8*)next_al_entry + 6 < al_end	    &&
				(u8*)next_al_entry + le16_to_cpu(
					next_al_entry->length) <= al_end    &&
				sle64_to_cpu(next_al_entry->lowest_vcn) <=
					lowest_vcn			    &&
				next_al_entry->type == al_entry->type	    &&
				next_al_entry->name_length == al_name_len   &&
				ntfs_names_are_equal((ntfschar*)((char*)
					next_al_entry +
					next_al_entry->name_offset),
					next_al_entry->name_length,
					al_name, al_name_len, CASE_SENSITIVE,
					vol->upcase, vol->upcase_len))
			continue;
is_enumeration:
		if (MREF_LE(al_entry->mft_reference) == ni->mft_no) {
			if (MSEQNO_LE(al_entry->mft_reference) !=
					le16_to_cpu(
					ni->mrec->sequence_number)) {
				ntfs_log_error("Found stale mft reference in "
						"attribute list!\n");
				break;
			}
		} else { /* Mft references do not match. */
			/* Do we want the base record back? */
			if (MREF_LE(al_entry->mft_reference) ==
					base_ni->mft_no) {
				ni = ctx->ntfs_ino = base_ni;
				ctx->mrec = ctx->base_mrec;
			} else {
				/* We want an extent record. */
				if (!vol->mft_na) {
					ntfs_log_perror("$MFT not ready for "
					    "opening an extent to inode %lld\n",
					    (long long)base_ni->mft_no);
					break;
				}
				ni = ntfs_extent_inode_open(base_ni,
						al_entry->mft_reference);
				if (!ni)
					break;
				ctx->ntfs_ino = ni;
				ctx->mrec = ni->mrec;
			}
		}
		a = ctx->attr = (ATTR_RECORD*)((char*)ctx->mrec +
				le16_to_cpu(ctx->mrec->attrs_offset));
		/*
		 * ctx->ntfs_ino, ctx->mrec, and ctx->attr now point to the
		 * mft record containing the attribute represented by the
		 * current al_entry.
		 *
		 * We could call into ntfs_attr_find() to find the right
		 * attribute in this mft record but this would be less
		 * efficient and not quite accurate as ntfs_attr_find() ignores
		 * the attribute instance numbers for example which become
		 * important when one plays with attribute lists. Also, because
		 * a proper match has been found in the attribute list entry
		 * above, the comparison can now be optimized. So it is worth
		 * re-implementing a simplified ntfs_attr_find() here.
		 *
		 * Use a manual loop so we can still use break and continue
		 * with the same meanings as above.
		 */
do_next_attr_loop:
		/*
		 * Make sure the attribute fully lies within the MFT record
		 * and we can safely access its minimal fields.
		 */
		offs = p2n(a) - p2n(ctx->mrec);
		space = le32_to_cpu(ctx->mrec->bytes_in_use) - offs;
		if (offs < 0)
			break;
		if ((space >= 4) && (a->type == AT_END))
			continue;
		if ((space < (ptrdiff_t)offsetof(ATTR_RECORD, resident_end))
		    || (space < (ptrdiff_t)le32_to_cpu(a->length)))
			break;
		if (al_entry->instance != a->instance)
			goto do_next_attr;
		/*
		 * If the type and/or the name are/is mismatched between the
		 * attribute list entry and the attribute record, there is
		 * corruption so we break and return error EIO.
		 */
		if (al_entry->type != a->type)
			break;
		if (!ntfs_names_are_equal((ntfschar*)((char*)a +
				le16_to_cpu(a->name_offset)),
				a->name_length, al_name,
				al_name_len, CASE_SENSITIVE,
				vol->upcase, vol->upcase_len))
			break;
		ctx->attr = a;
		/*
		 * If no @val specified or @val specified and it matches, we
		 * have found it! Also, if !@type, it is an enumeration, so we
		 * want the current attribute.
		 */
		if ((type == AT_UNUSED) || !val || (!a->non_resident &&
				le32_to_cpu(a->value_length) == val_len &&
				!memcmp((char*)a + le16_to_cpu(a->value_offset),
				val, val_len))) {
			return 0;
		}
do_next_attr:
		/* Proceed to the next attribute in the current mft record. */
		a = (ATTR_RECORD*)((char*)a + le32_to_cpu(a->length));
		goto do_next_attr_loop;
	}
corrupt :
	if (ni != base_ni) {
		ctx->ntfs_ino = base_ni;
		ctx->mrec = ctx->base_mrec;
		ctx->attr = ctx->base_attr;
	}
	errno = EIO;
	ntfs_log_error("Corrupt attribute list entry in MFT record %lld\n",
			(long long)base_ni->mft_no);
	return -1;
not_found:
	/*
	 * If we were looking for AT_END or we were enumerating and reached the
	 * end, we reset the search context @ctx and use ntfs_attr_find() to
	 * seek to the end of the base mft record.
	 */
	if (type == AT_UNUSED || type == AT_END) {
		ntfs_attr_reinit_search_ctx(ctx);
		return ntfs_attr_find(AT_END, name, name_len, ic, val, val_len,
				ctx);
	}
	/*
	 * The attribute wasn't found.  Before we return, we want to ensure
	 * @ctx->mrec and @ctx->attr indicate the position at which the
	 * attribute should be inserted in the base mft record.  Since we also
	 * want to preserve @ctx->al_entry we cannot reinitialize the search
	 * context using ntfs_attr_reinit_search_ctx() as this would set
	 * @ctx->al_entry to NULL.  Thus we do the necessary bits manually (see
	 * ntfs_attr_init_search_ctx() below).  Note, we _only_ preserve
	 * @ctx->al_entry as the remaining fields (base_*) are identical to
	 * their non base_ counterparts and we cannot set @ctx->base_attr
	 * correctly yet as we do not know what @ctx->attr will be set to by
	 * the call to ntfs_attr_find() below.
	 */
	ctx->mrec = ctx->base_mrec;
	ctx->attr = (ATTR_RECORD*)((u8*)ctx->mrec +
			le16_to_cpu(ctx->mrec->attrs_offset));
	ctx->is_first = TRUE;
	ctx->ntfs_ino = ctx->base_ntfs_ino;
	ctx->base_ntfs_ino = NULL;
	ctx->base_mrec = NULL;
	ctx->base_attr = NULL;
	/*
	 * In case there are multiple matches in the base mft record, need to
	 * keep enumerating until we get an attribute not found response (or
	 * another error), otherwise we would keep returning the same attribute
	 * over and over again and all programs using us for enumeration would
	 * lock up in a tight loop.
	 */
	{
		int ret;

		do {
			ret = ntfs_attr_find(type, name, name_len, ic, val,
					val_len, ctx);
		} while (!ret);
		return ret;
	}
}



int attr_lookup(const ATTR_TYPES type, const ntfschar *name,
		const u32 name_len, const IGNORE_CASE_BOOL ic,
		const VCN lowest_vcn, const u8 *val, const u32 val_len,
		ntfs_attr_search_ctx *ctx)
{
	ntfs_volume *vol;
	ntfs_inode *base_ni;
	int ret = -1;

	ntfs_log_enter("Entering for attribute type 0x%x\n", le32_to_cpu(type));
	
	if (!ctx || !ctx->mrec || !ctx->attr || (name && name != AT_UNNAMED &&
			(!ctx->ntfs_ino || !(vol = ctx->ntfs_ino->vol) ||
			!vol->upcase || !vol->upcase_len))) {
		errno = EINVAL;
		ntfs_log_perror("%s", __FUNCTION__);
		goto out;
	}
	
	if (ctx->base_ntfs_ino)
		base_ni = ctx->base_ntfs_ino;
	else
		base_ni = ctx->ntfs_ino;
	if (!base_ni || !NInoAttrList(base_ni) || type == AT_ATTRIBUTE_LIST)
		ret = ntfs_attr_find(type, name, name_len, ic, val, val_len, ctx);
	else
		ret = ntfs_external_attr_find(type, name, name_len, ic, 
					      lowest_vcn, val, val_len, ctx);
out:
	ntfs_log_leave("\n");
	if(type == AT_INDEX_ROOT && ret == ENOENT)
		return ENOTDIR;
	return ret;
}



u64 ntfs_inode_lookup_by_name(ntfs_inode *dir_ni,
		const ntfschar *uname, const int uname_len)
{
	VCN vcn;
	u64 mref = 0;
	s64 br;
	ntfs_volume *vol = dir_ni->vol;
	ntfs_attr_search_ctx *ctx;
	INDEX_ROOT *ir;
	INDEX_ENTRY *ie;
	INDEX_ALLOCATION *ia;
	IGNORE_CASE_BOOL case_sensitivity;
	u8 *index_end;
	ntfs_attr *ia_na;
	int eo, rc;
	u32 index_block_size;
	u8 index_vcn_size_bits;

	ntfs_log_trace("Entering\n");

	if (!dir_ni || !dir_ni->mrec || !uname || uname_len <= 0) {
		errno = EINVAL;
		return -1;
	}

	ctx = ntfs_attr_get_search_ctx(dir_ni, NULL);
	if (!ctx)
		return -1;

	/* Find the index root attribute in the mft record. */
	if (attr_lookup(AT_INDEX_ROOT, NTFS_INDEX_I30, 4, CASE_SENSITIVE, 0, NULL,
			0, ctx)) {
		ntfs_log_perror("Index root attribute missing in directory inode "
				"%lld", (unsigned long long)dir_ni->mft_no);
		goto put_err_out;
	}
	case_sensitivity = (NVolCaseSensitive(vol) ? CASE_SENSITIVE : IGNORE_CASE);
	/* Get to the index root value. */
	ir = (INDEX_ROOT*)((u8*)ctx->attr +
			le16_to_cpu(ctx->attr->value_offset));
	index_block_size = le32_to_cpu(ir->index_block_size);
	if (index_block_size < NTFS_BLOCK_SIZE ||
			index_block_size & (index_block_size - 1)) {
		ntfs_log_error("Index block size %u is invalid.\n",
				(unsigned)index_block_size);
		goto put_err_out;
	}
		/* Consistency check of ir done while fetching attribute */
	index_end = (u8*)&ir->index + le32_to_cpu(ir->index.index_length);
	/* The first index entry. */
	ie = (INDEX_ENTRY*)((u8*)&ir->index +
			le32_to_cpu(ir->index.entries_offset));
	/*
	 * Loop until we exceed valid memory (corruption case) or until we
	 * reach the last entry.
	 */
	for (;; ie = (INDEX_ENTRY*)((u8*)ie + le16_to_cpu(ie->length))) {
		/* Bounds checks. */
		if ((u8*)ie < (u8*)ctx->mrec || (u8*)ie +
				sizeof(INDEX_ENTRY_HEADER) > index_end ||
				(u8*)ie + le16_to_cpu(ie->length) >
				index_end) {
			ntfs_log_error("Index root entry out of bounds in"
				" inode %lld\n",
				(unsigned long long)dir_ni->mft_no);
			goto put_err_out;
		}
		/*
		 * The last entry cannot contain a name. It can however contain
		 * a pointer to a child node in the B+tree so we just break out.
		 */
		if (ie->ie_flags & INDEX_ENTRY_END)
			break;
		
		/* The file name must not overflow from the entry */
		if (ntfs_index_entry_inconsistent(ie, COLLATION_FILE_NAME,
				dir_ni->mft_no)) {
			errno = EIO;
			goto put_err_out;
		}
		/*
		 * Not a perfect match, need to do full blown collation so we
		 * know which way in the B+tree we have to go.
		 */
		rc = ntfs_names_full_collate(uname, uname_len,
				(ntfschar*)&ie->key.file_name.file_name,
				ie->key.file_name.file_name_length,
				case_sensitivity, vol->upcase, vol->upcase_len);
		/*
		 * If uname collates before the name of the current entry, there
		 * is definitely no such name in this index but we might need to
		 * descend into the B+tree so we just break out of the loop.
		 */
		if (rc == -1)
			break;
		/* The names are not equal, continue the search. */
		if (rc)
			continue;
		/*
		 * Perfect match, this will never happen as the
		 * ntfs_are_names_equal() call will have gotten a match but we
		 * still treat it correctly.
		 */
		mref = le64_to_cpu(ie->indexed_file);
		ntfs_attr_put_search_ctx(ctx);
		return mref;
	}
	/*
	 * We have finished with this index without success. Check for the
	 * presence of a child node and if not present return error code
	 * ENOENT, unless we have got the mft reference of a matching name
	 * cached in mref in which case return mref.
	 */
	if (!(ie->ie_flags & INDEX_ENTRY_NODE)) {
		ntfs_attr_put_search_ctx(ctx);
		if (mref)
			return mref;
		ntfs_log_debug("Entry not found - between root entries.\n");
		errno = ENOENT;
		return -1;
	} /* Child node present, descend into it. */

	/* Open the index allocation attribute. */
	ia_na = ntfs_attr_open(dir_ni, AT_INDEX_ALLOCATION, NTFS_INDEX_I30, 4);
	if (!ia_na) {
		ntfs_log_perror("Failed to open index allocation (inode %lld)",
				(unsigned long long)dir_ni->mft_no);
		goto put_err_out;
	}

	/* Allocate a buffer for the current index block. */
	ia = ntfs_malloc(index_block_size);
	if (!ia) {
		ntfs_attr_close(ia_na);
		goto put_err_out;
	}

	/* Determine the size of a vcn in the directory index. */
	if (vol->cluster_size <= index_block_size) {
		index_vcn_size_bits = vol->cluster_size_bits;
	} else {
		index_vcn_size_bits = NTFS_BLOCK_SIZE_BITS;
	}

	/* Get the starting vcn of the index_block holding the child node. */
	vcn = sle64_to_cpup((sle64*)((u8*)ie + le16_to_cpu(ie->length) - 8));

descend_into_child_node:

	/* Read the index block starting at vcn. */
	br = ntfs_attr_mst_pread(ia_na, vcn << index_vcn_size_bits, 1,
			index_block_size, ia);
	if (br != 1) {
		if (br != -1)
			errno = EIO;
		ntfs_log_perror("Failed to read vcn 0x%llx from inode %lld",
			       	(unsigned long long)vcn,
				(unsigned long long)ia_na->ni->mft_no);
		goto close_err_out;
	}

	if (ntfs_index_block_inconsistent((INDEX_BLOCK*)ia, index_block_size,
			ia_na->ni->mft_no, vcn)) {
		errno = EIO;
		goto close_err_out;
	}
	index_end = (u8*)&ia->index + le32_to_cpu(ia->index.index_length);

	/* The first index entry. */
	ie = (INDEX_ENTRY*)((u8*)&ia->index +
			le32_to_cpu(ia->index.entries_offset));
	/*
	 * Iterate similar to above big loop but applied to index buffer, thus
	 * loop until we exceed valid memory (corruption case) or until we
	 * reach the last entry.
	 */
	for (;; ie = (INDEX_ENTRY*)((u8*)ie + le16_to_cpu(ie->length))) {
		/* Bounds check. */
		if ((u8*)ie < (u8*)ia || (u8*)ie +
				sizeof(INDEX_ENTRY_HEADER) > index_end ||
				(u8*)ie + le16_to_cpu(ie->length) >
				index_end) {
			ntfs_log_error("Index entry out of bounds in directory "
				       "inode %lld.\n", 
				       (unsigned long long)dir_ni->mft_no);
			errno = EIO;
			goto close_err_out;
		}
		/*
		 * The last entry cannot contain a name. It can however contain
		 * a pointer to a child node in the B+tree so we just break out.
		 */
		if (ie->ie_flags & INDEX_ENTRY_END)
			break;
		
		/* The file name must not overflow from the entry */
		if (ntfs_index_entry_inconsistent(ie, COLLATION_FILE_NAME,
				dir_ni->mft_no)) {
			errno = EIO;
			goto close_err_out;
		}
		/*
		 * Not a perfect match, need to do full blown collation so we
		 * know which way in the B+tree we have to go.
		 */
		rc = ntfs_names_full_collate(uname, uname_len,
				(ntfschar*)&ie->key.file_name.file_name,
				ie->key.file_name.file_name_length,
				case_sensitivity, vol->upcase, vol->upcase_len);
		/*
		 * If uname collates before the name of the current entry, there
		 * is definitely no such name in this index but we might need to
		 * descend into the B+tree so we just break out of the loop.
		 */
		if (rc == -1)
			break;
		/* The names are not equal, continue the search. */
		if (rc)
			continue;
		mref = le64_to_cpu(ie->indexed_file);
		free(ia);
		ntfs_attr_close(ia_na);
		ntfs_attr_put_search_ctx(ctx);
		return mref;
	}
	/*
	 * We have finished with this index buffer without success. Check for
	 * the presence of a child node.
	 */
	if (ie->ie_flags & INDEX_ENTRY_NODE) {
		if ((ia->index.ih_flags & NODE_MASK) == LEAF_NODE) {
			ntfs_log_error("Index entry with child node found in a leaf "
					"node in directory inode %lld.\n",
					(unsigned long long)dir_ni->mft_no);
			errno = EIO;
			goto close_err_out;
		}
		/* Child node present, descend into it. */
		vcn = sle64_to_cpup((sle64*)((u8*)ie + le16_to_cpu(ie->length) - 8));
		if (vcn >= 0)
			goto descend_into_child_node;
		ntfs_log_error("Negative child node vcn in directory inode "
			       "0x%llx.\n", (unsigned long long)dir_ni->mft_no);
		errno = EIO;
		goto close_err_out;
	}
	free(ia);
	ntfs_attr_close(ia_na);
	ntfs_attr_put_search_ctx(ctx);
	/*
	 * No child node present, return error code ENOENT, unless we have got
	 * the mft reference of a matching name cached in mref in which case
	 * return mref.
	 */
	if (mref)
		return mref;
	ntfs_log_debug("Entry not found.\n");
	errno = ENOENT;
	return -1;
put_err_out:
	eo = EIO;
	ntfs_log_debug("Corrupt directory. Aborting lookup.\n");
eo_put_err_out:
	ntfs_attr_put_search_ctx(ctx);
	errno = eo;
	return -1;
close_err_out:
	eo = errno;
	free(ia);
	ntfs_attr_close(ia_na);
	goto eo_put_err_out;
}



ntfs_inode* pathname_to_inode(ntfs_volume *vol, ntfs_inode *parent,
		const char *pathname)
{
	u64 inum;
	int len, err = 0;
	char *p, *q;
	ntfs_inode *ni;
	ntfs_inode *result = NULL;
	ntfschar *unicode = NULL;
	char *ascii = NULL;

	if (!vol || !pathname) {
		errno = EINVAL;
		return NULL;
	}
	
	ntfs_log_trace("path: '%s'\n", pathname);
	
	ascii = strdup(pathname);
	if (!ascii) {
		ntfs_log_error("Out of memory.\n");
		err = ENOMEM;
		goto out;
	}

	p = ascii;
	/* Remove leading /'s. */
	while (p && *p && *p == PATH_SEP)
		p++;

	if (parent) {
		ni = parent;
	} else {

		ni = ntfs_inode_open(vol, FILE_root);
		if (!ni) {
			ntfs_log_debug("Couldn't open the inode of the root "
					"directory.\n");
			err = EIO;
			result = (ntfs_inode*)NULL;
			goto out;
		}
	}

	while (p && *p) {
		/* Find the end of the first token. */
		q = strchr(p, PATH_SEP);
		if (q != NULL) {
			*q = '\0';
		}

		len = ntfs_mbstoucs(p, &unicode);
		if (len < 0) {
			ntfs_log_perror("Could not convert filename to Unicode:"
					" '%s'", p);
			err = errno;
			goto close;
		} else if (len > NTFS_MAX_NAME_LEN) {
			err = ENAMETOOLONG;
			goto close;
		}
		inum = ntfs_inode_lookup_by_name(ni, unicode, len);

		if (inum == (u64) -1) {
			err = errno;
			ntfs_log_debug("Couldn't find name '%s' in pathname "
					"'%s'.\n", p, pathname);
			goto close;
		}

		if (ni != parent)
			if (ntfs_inode_close(ni)) {
				err = errno;
				goto out;
			}

		inum = MREF(inum);
		ni = ntfs_inode_open(vol, inum);
		if (!ni) {
			ntfs_log_debug("Cannot open inode %llu: %s.\n",
					(unsigned long long)inum, p);
			err = EIO;
			goto close;
		}
	
		free(unicode);
		unicode = NULL;

		if (q) *q++ = PATH_SEP; /* JPA */
		p = q;
		while (p && *p && *p == PATH_SEP)
			p++;
	}

	result = ni;
	ni = NULL;
close:
	if (ni && (ni != parent))
		if (ntfs_inode_close(ni) && !err)
			err = errno;
out:
	free(ascii);
	free(unicode);
	if (err)
		errno = err;
	return result;
}

int dump_file(int img, const char *path, int out)
{

	char img_path[FILEPATHLEN] = "";
	char img_name[FILEPATHLEN] = "";
	sprintf(img_path, "/proc/self/fd/%d", img);
	int sz = readlink(img_path, img_name, FILEPATHLEN);
	if (sz < 0)
		return sz;
	img_name[sz] = '\0';
	
	
	ntfs_volume* volume = ntfs_mount(img_name, NTFS_MNT_RDONLY);
	if (!volume)
		return -errno;

	ntfs_inode *inode = pathname_to_inode(volume, NULL, path);
	if (!inode){
		int tmp = -errno;
		ntfs_umount(volume, TRUE);
		return tmp;
	}

	ntfs_attr *attribute = ntfs_attr_open(inode, AT_DATA, NULL, 0);
	if (!attribute){
		int tmp = -errno;
		ntfs_inode_close(inode);
		ntfs_umount(volume, TRUE);
		return tmp;
	}


	s64 read = 0, wrote = 0, already_read = 0; 
	char buf[PAGE] = "";

	for(;;){
		read = ntfs_attr_pread(attribute, already_read, PAGE, buf);
		if (!read)
			break;
		if (read < 0)
			return -errno;
		
		wrote = write(out, buf, read);
		if (read - wrote)
			return -errno;

		already_read += read;
	}

	ntfs_attr_close(attribute);
	ntfs_inode_close(inode);
	ntfs_umount(volume, TRUE);

	
	return 0;
}