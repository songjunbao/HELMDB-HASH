/* -------------------------------------------------------------------------
 *
 * tuptoaster.cpp
 *	  Support routines for external and compressed storage of
 *	  variable size attributes.
 *
 * Portions Copyright (c) 2020 Huawei Technologies Co.,Ltd.
 * Copyright (c) 2000-2012, PostgreSQL Global Development Group
 *
 *
 * IDENTIFICATION
 *	  src/gausskernel/storage/access/heap/tuptoaster.cpp
 *
 *
 * INTERFACE ROUTINES
 *		toast_insert_or_update -
 *			Try to make a given tuple fit into one page by compressing
 *			or moving off attributes
 *
 *		toast_delete -
 *			Reclaim toast storage when a tuple is deleted
 *
 *		heap_tuple_untoast_attr -
 *			Fetch back a given value from the "secondary" relation
 *
 * -------------------------------------------------------------------------
 */
#include "postgres.h"
#include "knl/knl_variable.h"

#include <fcntl.h>

#include "access/genam.h"
#include "access/heapam.h"
#include "access/tuptoaster.h"
#include "access/xact.h"
#include "access/ustore/knl_utuptoaster.h"
#include "catalog/catalog.h"
#include "utils/fmgroids.h"
#include "utils/pg_lzcompress.h"
#include "utils/rel.h"
#include "utils/rel_gs.h"
#include "utils/typcache.h"
#include "commands/vacuum.h"
#include "utils/snapmgr.h"
#include "mb/pg_wchar.h"
#include "access/tableam.h"

#undef TOAST_DEBUG

static bool toastid_valueid_exists(Oid toastrelid, Oid valueid, int2 bucketid);
static struct varlena *toast_fetch_datum_slice(struct varlena *attr, int64 sliceoffset, int32 length);
static struct varlena* toast_huge_fetch_datum_slice(struct varlena* attr, int64 sliceoffset, int32 length);
varlena* toast_huge_write_datum_slice(struct varlena* attr1, struct varlena* attr2, int64 sliceoffset, int32 length);
void toast_huge_fetch_and_copy(Relation srctoastrel, Relation srctoastidx, Relation destoastrel,
    Relation destoastidx, varatt_lob_external large_toast_pointer, int32 *chunk_seq, Oid *firstchunkid,
    Oid realtoastOid);

/* ----------
 * heap_tuple_fetch_attr -
 *
 *	Public entry point to get back a toasted value from
 *	external source (possibly still in compressed format).
 *
 * This will return a datum that contains all the data internally, ie, not
 * relying on external storage or memory, but it can still be compressed or
 * have a short header.
 ----------
 */
struct varlena *heap_tuple_fetch_attr(struct varlena *attr)
{
    struct varlena *result = NULL;
    errno_t rc = EOK;

    if (VARATT_IS_EXTERNAL_ONDISK_B(attr)) {
        /*
         * This is an external stored plain value
         */
        result = toast_fetch_datum(attr);
    } else if (VARATT_IS_EXTERNAL_INDIRECT(attr)) {
        /*
         * This is an indirect pointer --- dereference it
         */
        struct varatt_indirect redirect;

        VARATT_EXTERNAL_GET_POINTER(redirect, attr);
        attr = (struct varlena *)redirect.pointer;

        /* nested indirect Datums aren't allowed */
        Assert(!VARATT_IS_EXTERNAL_INDIRECT(attr));

        /* recurse if value is still external in some other way */
        if (VARATT_IS_EXTERNAL(attr))
            return heap_tuple_fetch_attr(attr);

        /*
         * Copy into the caller's memory context, in case caller tries to
         * pfree the result.
         */
        result = (struct varlena *)palloc(VARSIZE_ANY(attr));
        rc = memcpy_s(result, VARSIZE_ANY(attr), attr, VARSIZE_ANY(attr));
        securec_check(rc, "", "");
    } else {
        /*
         * This is a plain value inside of the main tuple - why am I called?
         */
        result = attr;
    }

    return result;
}

/* ----------
 * heap_tuple_untoast_attr -
 *
 *	Public entry point to get back a toasted value from compression
 *	or external storage.
 * ----------
 */
struct varlena *heap_tuple_untoast_attr(struct varlena *attr, ScalarVector *arr)
{
    if (VARATT_IS_EXTERNAL_ONDISK_B(attr)) {
        /*
         * This is an externally stored datum --- fetch it back from there
         */
        attr = toast_fetch_datum(attr);
        /* If it's compressed, decompress it */
        if (VARATT_IS_COMPRESSED(attr)) {
            PGLZ_Header *tmp = (PGLZ_Header *)attr;

            if (arr == NULL) {
                attr = (struct varlena *)palloc(PGLZ_RAW_SIZE(tmp) + VARHDRSZ);
            } else {
                attr = (struct varlena *)arr->m_buf->Allocate(tmp->rawsize + VARHDRSZ);
            }
            SET_VARSIZE(attr, PGLZ_RAW_SIZE(tmp) + VARHDRSZ);
            pglz_decompress(tmp, VARDATA(attr));
            pfree(tmp);
        }
    } else if (VARATT_IS_EXTERNAL_INDIRECT(attr)) {
        struct varatt_indirect redirect;
        VARATT_EXTERNAL_GET_POINTER(redirect, attr);
        attr = (struct varlena *)redirect.pointer;

        /* nested indirect Datums aren't allowed */
        Assert(!VARATT_IS_EXTERNAL_INDIRECT(attr));

        attr = heap_tuple_untoast_attr(attr, arr);
    } else if (VARATT_IS_COMPRESSED(attr)) {
        /*
         * This is a compressed value inside of the main tuple
         */
        PGLZ_Header *tmp = (PGLZ_Header *)attr;

        if (arr == NULL) {
            attr = (struct varlena *)palloc(PGLZ_RAW_SIZE(tmp) + VARHDRSZ);
        } else {
            attr = (struct varlena *)arr->m_buf->Allocate(tmp->rawsize + VARHDRSZ);
        }
        SET_VARSIZE(attr, PGLZ_RAW_SIZE(tmp) + VARHDRSZ);
        pglz_decompress(tmp, VARDATA(attr));
    } else if (VARATT_IS_SHORT(attr) && !VARATT_IS_HUGE_TOAST_POINTER(attr)) {
        /*
         * This is a short-header varlena --- convert to 4-byte header format
         */
        Size data_size = VARSIZE_SHORT(attr) - VARHDRSZ_SHORT;
        Size new_size = data_size + VARHDRSZ;
        struct varlena *new_attr;
        errno_t rc = EOK;

        if (arr == NULL) {
            new_attr = (struct varlena *)palloc(new_size);
        } else {
            new_attr = (struct varlena *)arr->m_buf->Allocate(new_size);
        }
        SET_VARSIZE(new_attr, new_size);
        rc = memcpy_s(VARDATA(new_attr), new_size, VARDATA_SHORT(attr), data_size);
        securec_check(rc, "", "");
        attr = new_attr;
    }

    return attr;
}

/* ----------
 * heap_tuple_untoast_attr_slice -
 *
 *		Public entry point to get back part of a toasted value
 *		from compression or external storage.
 * ----------
 */
struct varlena *heap_tuple_untoast_attr_slice(struct varlena *attr, int64 slice_offset, int32 slice_length)
{
    struct varlena *preslice = NULL;
    struct varlena *result = NULL;
    char *attrdata = NULL;
    int32 attrsize;
    errno_t rc = EOK;

    if (VARATT_IS_EXTERNAL_ONDISK_B(attr)) {
        struct varatt_external toast_pointer;

        VARATT_EXTERNAL_GET_POINTER(toast_pointer, attr);

        /* fast path for non-compressed external datums */
        if (!VARATT_EXTERNAL_IS_COMPRESSED(toast_pointer))
            return toast_fetch_datum_slice(attr, slice_offset, slice_length);

        /* fetch it back (compressed marker will get set automatically) */
        preslice = toast_fetch_datum(attr);
    } else if (VARATT_IS_EXTERNAL_INDIRECT(attr)) {
        struct varatt_indirect redirect;
        VARATT_EXTERNAL_GET_POINTER(redirect, attr);

        /* nested indirect Datums aren't allowed */
        Assert(!VARATT_IS_EXTERNAL_INDIRECT(redirect.pointer));

        return heap_tuple_untoast_attr_slice(redirect.pointer, slice_offset, slice_length);
    } else if (VARATT_IS_HUGE_TOAST_POINTER(attr)) {
        return toast_huge_fetch_datum_slice(attr, slice_offset, slice_length);
    } else {
        preslice = attr;
    }

    if (VARATT_IS_COMPRESSED(preslice)) {
        PGLZ_Header *tmp = (PGLZ_Header *)preslice;
        Size size = PGLZ_RAW_SIZE(tmp) + VARHDRSZ;

        preslice = (struct varlena *)palloc(size);
        SET_VARSIZE(preslice, size);
        pglz_decompress(tmp, VARDATA(preslice));

        if (tmp != (PGLZ_Header *)attr)
            pfree(tmp);
    }

    if (VARATT_IS_SHORT(preslice)) {
        attrdata = VARDATA_SHORT(preslice);
        attrsize = VARSIZE_SHORT(preslice) - VARHDRSZ_SHORT;
    } else {
        attrdata = VARDATA(preslice);
        attrsize = VARSIZE(preslice) - VARHDRSZ;
    }

    /* slicing of datum for compressed cases and plain value */
    if (slice_offset >= attrsize) {
        slice_offset = 0;
        slice_length = 0;
    }

    if (((slice_offset + slice_length) > attrsize) || slice_length < 0)
        slice_length = attrsize - slice_offset;

    result = (struct varlena *)palloc(slice_length + VARHDRSZ);
    SET_VARSIZE(result, slice_length + VARHDRSZ);

    rc = memcpy_s(VARDATA(result), slice_length + VARHDRSZ, attrdata + slice_offset, slice_length);
    securec_check(rc, "", "");
    if (preslice != attr)
        pfree(preslice);

    return result;
}

/* ----------
 * toast_raw_datum_size -
 *
 *	Return the raw (detoasted) size of a varlena datum
 *	(including the VARHDRSZ header)
 * ----------
 */
Size toast_raw_datum_size(Datum value)
{
    struct varlena *attr = (struct varlena *)DatumGetPointer(value);
    Size result;

    if (VARATT_IS_EXTERNAL_ONDISK_B(attr)) {
        /* va_rawsize is the size of the original datum -- including header */
        struct varatt_external toast_pointer;

        VARATT_EXTERNAL_GET_POINTER(toast_pointer, attr);
        result = toast_pointer.va_rawsize;
    } else if (VARATT_IS_EXTERNAL_INDIRECT(attr)) {
        struct varatt_indirect toast_pointer;
        VARATT_EXTERNAL_GET_POINTER(toast_pointer, attr);

        /* nested indirect Datums aren't allowed */
        Assert(!VARATT_IS_EXTERNAL_INDIRECT(toast_pointer.pointer));

        return toast_raw_datum_size(PointerGetDatum(toast_pointer.pointer));
    } else if (VARATT_IS_COMPRESSED(attr)) {
        /* here, va_rawsize is just the payload size */
        result = VARRAWSIZE_4B_C(attr) + VARHDRSZ;
    } else if (VARATT_IS_HUGE_TOAST_POINTER(attr)) {
        struct varatt_lob_external large_toast_pointer;

        VARATT_EXTERNAL_GET_HUGE_POINTER(large_toast_pointer, attr);
        result = large_toast_pointer.va_rawsize;
    } else if (VARATT_IS_SHORT(attr)) {
        /*
         * we have to normalize the header length to VARHDRSZ or else the
         * callers of this function will be confused.
         */
        result = VARSIZE_SHORT(attr) - VARHDRSZ_SHORT + VARHDRSZ;
    } else {
        /* plain untoasted datum */
        result = VARSIZE(attr);
    }
    return result;
}

/* ----------
 * toast_datum_size
 *
 *	Return the physical storage size (possibly compressed) of a varlena datum
 * ----------
 */
Size toast_datum_size(Datum value)
{
    struct varlena *attr = (struct varlena *)DatumGetPointer(value);
    Size result;

    if (VARATT_IS_EXTERNAL_ONDISK_B(attr)) {
        /*
         * Attribute is stored externally - return the extsize whether
         * compressed or not.  We do not count the size of the toast pointer
         * ... should we?
         */
        struct varatt_external toast_pointer;

        VARATT_EXTERNAL_GET_POINTER(toast_pointer, attr);
        result = toast_pointer.va_extsize;
    } else if (VARATT_IS_EXTERNAL_INDIRECT(attr)) {
        struct varatt_indirect toast_pointer;

        VARATT_EXTERNAL_GET_POINTER(toast_pointer, attr);

        /* nested indirect Datums aren't allowed */
        Assert(!VARATT_IS_EXTERNAL_INDIRECT(attr));

        return toast_datum_size(PointerGetDatum(toast_pointer.pointer));
    } else if (VARATT_IS_HUGE_TOAST_POINTER(attr)) {
        struct varatt_lob_external large_toast_pointer;

        VARATT_EXTERNAL_GET_HUGE_POINTER(large_toast_pointer, attr);
        result = large_toast_pointer.va_rawsize;
    } else if (VARATT_IS_SHORT(attr)) {
        result = VARSIZE_SHORT(attr);
    } else {
        /*
         * Attribute is stored inline either compressed or not, just calculate
         * the size of the datum in either case.
         */
        result = VARSIZE(attr);
    }
    return result;
}

int64 calculate_huge_length(text *t)
{
    ScanKeyData toastkey;
    SysScanDesc toastscan;
    HeapTuple ttup;
    bool isnull;
    int64 len = 0;
    int offset = 0;
    MemoryContext fetchLobContext = AllocSetContextCreate(CurrentMemoryContext, "lob calculate length context",
        ALLOCSET_DEFAULT_MINSIZE, ALLOCSET_DEFAULT_INITSIZE, ALLOCSET_DEFAULT_MAXSIZE);
    struct varatt_lob_external large_toast_pointer;
    VARATT_EXTERNAL_GET_HUGE_POINTER(large_toast_pointer, t);
    Relation toastrel = heap_open(large_toast_pointer.va_toastrelid, AccessShareLock);
    Relation toastidx = index_open(toastrel->rd_rel->reltoastidxid, AccessShareLock);
    TupleDesc toast_tup_desc = toastrel->rd_att;
    ScanKeyInit(&toastkey, (AttrNumber)1, BTEqualStrategyNumber, F_OIDEQ,
        ObjectIdGetDatum(large_toast_pointer.va_valueid));
    toastscan = systable_beginscan_ordered(toastrel, toastidx, SnapshotToast, 1, &toastkey);
    while ((ttup = systable_getnext_ordered(toastscan, ForwardScanDirection)) != NULL) {
        Pointer chunk = DatumGetPointer(fastgetattr(ttup, CHUNK_DATA_ATTR, toast_tup_desc, &isnull));
        MemoryContext oldCxt = MemoryContextSwitchTo(fetchLobContext);
        text *result = heap_tuple_untoast_attr((varlena *)chunk);
        const char* str = VARDATA_ANY(result);
        int limit = VARSIZE_ANY_EXHDR(result);
        /* including character encoding, avoiding truncate */
        if (offset > 0) {
            if (limit <= offset) {
                offset -= limit;
                continue;
            }
            str += offset;
            limit -= offset;
            offset = 0;
        }

        len += (int64)pg_mbstrlen_with_len_toast(str, &limit);
        if (limit != 0) {
            Assert(limit < 0);
            offset = -limit;
        }
        MemoryContextSwitchTo(oldCxt);
        MemoryContextReset(fetchLobContext);
    }
    Assert(offset == 0);
    systable_endscan_ordered(toastscan);
    index_close(toastidx, AccessShareLock);
    heap_close(toastrel, AccessShareLock);
    MemoryContextDelete(fetchLobContext);
    return len;
}

/* ----------
 * toast_delete -
 *
 *	Cascaded delete toast-entries on DELETE
 * ----------
 */
void toast_delete(Relation rel, HeapTuple oldtup, int options)
{
    TupleDesc tuple_desc;
    FormData_pg_attribute *att = NULL;
    int num_attrs;
    int i;
    Datum toast_values[MaxHeapAttributeNumber];
    bool toast_isnull[MaxHeapAttributeNumber];

    /*
     * We should only ever be called for tuples of plain relations or
     * materialized views --- recursing on a toast rel is bad news.
     */
    Assert(rel->rd_rel->relkind == RELKIND_RELATION || rel->rd_rel->relkind == RELKIND_MATVIEW);

    /*
     * Get the tuple descriptor and break down the tuple into fields.
     *
     * NOTE: it's debatable whether to use heap_deform_tuple() here or just
     * heap_getattr() only the varlena columns.  The latter could win if there
     * are few varlena columns and many non-varlena ones. However,
     * heap_deform_tuple costs only O(N) while the heap_getattr way would cost
     * O(N^2) if there are many varlena columns, so it seems better to err on
     * the side of linear cost.  (We won't even be here unless there's at
     * least one varlena column, by the way.)
     */
    tuple_desc = rel->rd_att;
    att = tuple_desc->attrs;
    num_attrs = tuple_desc->natts;

    Assert(num_attrs <= MaxHeapAttributeNumber);
    if (num_attrs > MaxHeapAttributeNumber)
        ereport(ERROR, (errcode(ERRCODE_TOO_MANY_COLUMNS),
                        errmsg("number of columns (%d) exceeds limit (%d), AM type (%d), type id (%u)", num_attrs,
                               MaxHeapAttributeNumber, GetTableAmType(tuple_desc->td_tam_ops), tuple_desc->tdtypeid)));

    heap_deform_tuple(oldtup, tuple_desc, toast_values, toast_isnull);

    /*
     * Check for external stored attributes and delete them from the secondary
     * relation.
     */
    for (i = 0; i < num_attrs; i++) {
        if (att[i].attlen == -1) {
            Datum value = toast_values[i];

            if (toast_isnull[i])
                continue;
            else if (VARATT_IS_EXTERNAL_ONDISK_B(PointerGetDatum(value)))
                toast_delete_datum(rel, value, options);
            else if (VARATT_IS_HUGE_TOAST_POINTER(PointerGetDatum(value)))
                toast_huge_delete_datum(rel, value, options);
            else if (VARATT_IS_EXTERNAL_INDIRECT(PointerGetDatum(value)))
                ereport(ERROR, (errcode(ERRCODE_FETCH_DATA_FAILED),
                                errmsg("attempt to delete tuple containing indirect datums")));
        }
    }
}

struct varlena *heap_tuple_fetch_and_copy(Relation rel, struct varlena *attr, bool needcheck)
{
    Relation srctoastrel;
    Relation srctoastidx;
    Relation destoastrel;
    Relation destoastidx;
    int32 chunk_seq = 0;
    errno_t rc;
    struct varlena *result;
    Oid firstchunkid = InvalidOid;
    struct varatt_lob_external large_toast_pointer;

    VARATT_EXTERNAL_GET_HUGE_POINTER(large_toast_pointer, attr);

    if (needcheck && large_toast_pointer.va_toastrelid == rel->rd_rel->reltoastrelid) {
        return NULL;
    }

    srctoastrel = heap_open(large_toast_pointer.va_toastrelid, AccessShareLock);
    srctoastidx = index_open(srctoastrel->rd_rel->reltoastidxid, AccessShareLock);
    destoastrel = heap_open(rel->rd_rel->reltoastrelid, RowExclusiveLock);
    destoastidx = index_open(destoastrel->rd_rel->reltoastidxid, RowExclusiveLock);

    if (OidIsValid(rel->rd_toastoid) && toastrel_valueid_exists(destoastrel, large_toast_pointer.va_valueid)) {
        index_close(srctoastidx, AccessShareLock);
        heap_close(srctoastrel, AccessShareLock);
        index_close(destoastidx, RowExclusiveLock);
        heap_close(destoastrel, RowExclusiveLock);
        return NULL;
    }

    toast_huge_fetch_and_copy(srctoastrel, srctoastidx, destoastrel, destoastidx, large_toast_pointer, &chunk_seq,
        &firstchunkid, rel->rd_toastoid);

    result = (struct varlena *)palloc(LARGE_TOAST_POINTER_SIZE);
    SET_HUGE_TOAST_POINTER_TAG(result, VARTAG_ONDISK);
    if (OidIsValid(rel->rd_toastoid)) {
        large_toast_pointer.va_toastrelid = rel->rd_toastoid;
    } else {
        large_toast_pointer.va_toastrelid = rel->rd_rel->reltoastrelid;
    }
    large_toast_pointer.va_valueid = firstchunkid;
    rc =
        memcpy_s(VARDATA_EXTERNAL(result), LARGE_TOAST_POINTER_SIZE, &large_toast_pointer, sizeof(large_toast_pointer));
    securec_check(rc, "", "");

    index_close(srctoastidx, AccessShareLock);
    heap_close(srctoastrel, AccessShareLock);
    index_close(destoastidx, RowExclusiveLock);
    heap_close(destoastrel, RowExclusiveLock);

    return result;
}

void delete_old_tuple_toast(Relation rel, Datum toast_oldvalue, int options, bool allow_update_self)
{
    if (VARATT_IS_HUGE_TOAST_POINTER(DatumGetPointer(toast_oldvalue))) {
        toast_huge_delete_datum(rel, toast_oldvalue, options, allow_update_self);
    } else {
        toast_delete_datum(rel, toast_oldvalue, options, allow_update_self);
    }
}

/* ----------
 * toast_insert_or_update -
 *
 *	Delete no-longer-used toast-entries and create new ones to
 *	make the new tuple fit on INSERT or UPDATE
 *
 * Inputs:
 *	newtup: the candidate new tuple to be inserted
 *	oldtup: the old row version for UPDATE, or NULL for INSERT
 *	options: options to be passed to heap_insert() for toast rows
 * Result:
 *	either newtup if no toasting is needed, or a palloc'd modified tuple
 *	that is what should actually get stored
 *
 * NOTE: neither newtup nor oldtup will be modified.  This is a change
 * from the pre-8.1 API of this routine.
 * ----------
 */
HeapTuple toast_insert_or_update(Relation rel, HeapTuple newtup, HeapTuple oldtup,
                                 int options, Page pageForOldTup, bool allow_update_self)
{
    HeapTuple result_tuple;
    TupleDesc tuple_desc;
    FormData_pg_attribute *att = NULL;
    int num_attrs;
    int i;

    bool need_change = false;
    bool need_free = false;
    bool need_delold = false;
    bool has_nulls = false;

    Size max_data_len;
    Size hoff;

    char toast_action[MaxHeapAttributeNumber];
    bool toast_isnull[MaxHeapAttributeNumber];
    bool toast_oldisnull[MaxHeapAttributeNumber];
    Datum toast_values[MaxHeapAttributeNumber];
    Datum toast_oldvalues[MaxHeapAttributeNumber];
    struct varlena *toast_oldexternal[MaxHeapAttributeNumber];
    int32 toast_sizes[MaxHeapAttributeNumber];
    bool toast_free[MaxHeapAttributeNumber];
    bool toast_delold[MaxHeapAttributeNumber];
    errno_t rc = EOK;

    /*
     * We should only ever be called for tuples of plain relations ---
     * recursing on a toast rel is bad news.
     */
    Assert(rel->rd_rel->relkind == RELKIND_RELATION || rel->rd_rel->relkind == RELKIND_MATVIEW);

    /*
     * Get the tuple descriptor and break down the tuple(s) into fields.
     */
    tuple_desc = rel->rd_att;
    att = tuple_desc->attrs;
    num_attrs = tuple_desc->natts;

    Assert(num_attrs <= MaxHeapAttributeNumber);
    if (num_attrs > MaxHeapAttributeNumber) {
        ereport(ERROR, (errcode(ERRCODE_TOO_MANY_COLUMNS),
                        errmsg("number of columns (%d) exceeds limit (%d), AM type (%d), type id (%u)", num_attrs,
                               MaxHeapAttributeNumber, GetTableAmType(tuple_desc->td_tam_ops), tuple_desc->tdtypeid)));
    }
    heap_deform_tuple(newtup, tuple_desc, toast_values, toast_isnull);
    if (oldtup != NULL) {
        heap_deform_tuple3(oldtup, tuple_desc, toast_oldvalues, toast_oldisnull, pageForOldTup);
    }

    /* ----------
     * Then collect information about the values given
     *
     * NOTE: toast_action[i] can have these values:
     *		' '		default handling
     *		'p'		already processed --- don't touch it
     *		'x'		incompressible, but OK to move off
     *
     * NOTE: toast_sizes[i] is only made valid for varlena attributes with
     *		toast_action[i] different from 'p'.
     * ----------
     */
    rc = memset_s(toast_action, sizeof(char) * MaxHeapAttributeNumber, ' ', num_attrs * sizeof(char));
    securec_check(rc, "", "");
    rc = memset_s(toast_oldexternal, sizeof(struct varlena *) * MaxHeapAttributeNumber, 0,
                  num_attrs * sizeof(struct varlena *));
    securec_check(rc, "", "");
    rc = memset_s(toast_free, sizeof(bool) * MaxHeapAttributeNumber, 0, num_attrs * sizeof(bool));
    securec_check(rc, "", "");
    rc = memset_s(toast_delold, sizeof(bool) * MaxHeapAttributeNumber, 0, num_attrs * sizeof(bool));
    securec_check(rc, "", "");

    for (i = 0; i < num_attrs; i++) {
        struct varlena *old_value = NULL;
        struct varlena *new_value = NULL;

        if (oldtup != NULL) {
            /*
             * For UPDATE get the old and new values of this attribute
             */
            old_value = (struct varlena *)DatumGetPointer(toast_oldvalues[i]);
            new_value = (struct varlena *)DatumGetPointer(toast_values[i]);

            /*
             * If the old value is stored on disk, check if it has changed so
             * we have to delete it later.
             */
            if (att[i].attlen == -1 && !toast_oldisnull[i] &&
                (VARATT_IS_EXTERNAL_ONDISK_B(old_value) || VARATT_IS_HUGE_TOAST_POINTER(old_value))) {
                if (toast_isnull[i] || (RelationIsLogicallyLogged(rel) && !VARATT_IS_HUGE_TOAST_POINTER(new_value)) ||
                    !(VARATT_IS_EXTERNAL_ONDISK_B(new_value) || VARATT_IS_HUGE_TOAST_POINTER(new_value)) ||
                    VARTAG_EXTERNAL(new_value) != VARTAG_EXTERNAL(old_value) ||
                    memcmp((char *)old_value, (char *)new_value, VARSIZE_EXTERNAL(old_value)) != 0) {
                    /*
                     * The old external stored value isn't needed any more
                     * after the update
                     */
                    toast_delold[i] = true;
                    need_delold = true;
                } else {
                    /*
                     * This attribute isn't changed by this update so we reuse
                     * the original reference to the old value in the new
                     * tuple.
                     */
                    toast_action[i] = 'p';
                    continue;
                }
            }
        } else {
            /*
             * For INSERT simply get the new value
             */
            new_value = (struct varlena *)DatumGetPointer(toast_values[i]);
        }

        /*
         * Handle NULL attributes
         */
        if (toast_isnull[i]) {
            toast_action[i] = 'p';
            has_nulls = true;
            continue;
        }

        /*
         * Now look at varlena attributes
         */
        if (att[i].attlen == -1) {
            /*
             * If the table's attribute says PLAIN always, force it so.
             */
            if (att[i].attstorage == 'p') {
                toast_action[i] = 'p';
            }

            /*
             * We took care of UPDATE above, so any external value we find
             * still in the tuple must be someone else's we cannot reuse.
             * Fetch it back (without decompression, unless we are forcing
             * PLAIN storage).	If necessary, we'll push it out as a new
             * external value below.
             */
            if (VARATT_IS_EXTERNAL(new_value) && !VARATT_IS_HUGE_TOAST_POINTER(new_value)) {
                toast_oldexternal[i] = new_value;
                if (att[i].attstorage == 'p') {
                    new_value = heap_tuple_untoast_attr(new_value);
                } else {
                    new_value = heap_tuple_fetch_attr(new_value);
                }
                toast_values[i] = PointerGetDatum(new_value);
                toast_free[i] = true;
                need_change = true;
                need_free = true;
            } else if (VARATT_IS_HUGE_TOAST_POINTER(new_value)) {
                toast_oldexternal[i] = heap_tuple_fetch_and_copy(rel, new_value, oldtup != NULL);
                if (toast_oldexternal[i] != NULL) {
                    new_value = toast_oldexternal[i];
                    toast_values[i] = PointerGetDatum(new_value);
                    need_change = true;
                    need_free = true;
                }
            }

            /*
             * Remember the size of this attribute
             */
            toast_sizes[i] = VARSIZE_ANY(new_value);
        } else {
            /*
             * Not a varlena attribute, plain storage always
             */
            toast_action[i] = 'p';
        }
    }

    /*
     * Compress and/or save external until data fits into target length
     *
     *	1: Inline compress attributes with attstorage 'x', and store very
     *	   large attributes with attstorage 'x' or 'e' external immediately
     *	2: Store attributes with attstorage 'x' or 'e' external
     *	3: Inline compress attributes with attstorage 'm'
     *	4: Store attributes with attstorage 'm' external
     *
     * compute header overhead --- this should match heap_form_tuple()
     */
    hoff = offsetof(HeapTupleHeaderData, t_bits);
    if (has_nulls) {
        hoff += BITMAPLEN(num_attrs);
    }
    if (newtup->t_data->t_infomask & HEAP_HASOID) {
        hoff += sizeof(Oid);
    }
    hoff = MAXALIGN(hoff);
    /* now convert to a limit on the tuple data size */
    max_data_len = TOAST_TUPLE_TARGET - hoff;

    /*
     * Look for attributes with attstorage 'x' to compress.  Also find large
     * attributes with attstorage 'x' or 'e', and store them external.
     */
    while (heap_compute_data_size(tuple_desc, toast_values, toast_isnull) > max_data_len) {
        int biggest_attno = -1;
        int32 biggest_size = MAXALIGN(TOAST_POINTER_SIZE);
        Datum old_value;
        Datum new_value;

        /*
         * Search for the biggest yet unprocessed internal attribute
         */
        for (i = 0; i < num_attrs; i++) {
            if (toast_action[i] != ' ') {
                continue;
            }
            if (VARATT_IS_EXTERNAL(DatumGetPointer(toast_values[i]))) {
                continue; /* can't happen, toast_action would be 'p' */
            }
            if (VARATT_IS_COMPRESSED(DatumGetPointer(toast_values[i]))) {
                continue;
            }
            if (att[i].attstorage != 'x' && att[i].attstorage != 'e') {
                continue;
            }
            if (toast_sizes[i] > biggest_size) {
                biggest_attno = i;
                biggest_size = toast_sizes[i];
            }
        }

        if (biggest_attno < 0) {
            break;
        }

        /*
         * Attempt to compress it inline, if it has attstorage 'x'
         */
        i = biggest_attno;
        if (att[i].attstorage == 'x') {
            old_value = toast_values[i];
            new_value = toast_compress_datum(old_value);
            if (DatumGetPointer(new_value) != NULL) {
                /* successful compression */
                if (toast_free[i]) {
                    pfree(DatumGetPointer(old_value));
                }
                toast_values[i] = new_value;
                toast_free[i] = true;
                toast_sizes[i] = VARSIZE(DatumGetPointer(toast_values[i]));
                need_change = true;
                need_free = true;
            } else {
                /* incompressible, ignore on subsequent compression passes */
                toast_action[i] = 'x';
            }
        } else {
            /* has attstorage 'e', ignore on subsequent compression passes */
            toast_action[i] = 'x';
        }

        /*
         * If this value is by itself more than maxDataLen (after compression
         * if any), push it out to the toast table immediately, if possible.
         * This avoids uselessly compressing other fields in the common case
         * where we have one long field and several short ones.
         *
         * XXX maybe the threshold should be less than maxDataLen?
         */
        if ((uint32)toast_sizes[i] > max_data_len && rel->rd_rel->reltoastrelid != InvalidOid) {
            old_value = toast_values[i];
            toast_action[i] = 'p';
            toast_values[i] = toast_save_datum(rel, toast_values[i], toast_oldexternal[i], options);
            if (toast_free[i]) {
                pfree(DatumGetPointer(old_value));
            }
            toast_free[i] = true;
            need_change = true;
            need_free = true;
        }
    }

    /*
     * Second we look for attributes of attstorage 'x' or 'e' that are still
     * inline.	But skip this if there's no toast table to push them to.
     */
    while (heap_compute_data_size(tuple_desc, toast_values, toast_isnull) > max_data_len &&
           rel->rd_rel->reltoastrelid != InvalidOid) {
        int biggest_attno = -1;
        int32 biggest_size = MAXALIGN(TOAST_POINTER_SIZE);
        Datum old_value;

        /* ------
         * Search for the biggest yet inlined attribute with
         * attstorage equals 'x' or 'e'
         * ------
         */
        for (i = 0; i < num_attrs; i++) {
            if (toast_action[i] == 'p') {
                continue;
            }
            if (VARATT_IS_EXTERNAL(DatumGetPointer(toast_values[i]))) {
                continue; /* can't happen, toast_action would be 'p' */
            }

            if (att[i].attstorage != 'x' && att[i].attstorage != 'e') {
                continue;
            }
            if (toast_sizes[i] > biggest_size) {
                biggest_attno = i;
                biggest_size = toast_sizes[i];
            }
        }

        if (biggest_attno < 0) {
            break;
        }

        /*
         * Store this external
         */
        i = biggest_attno;
        old_value = toast_values[i];
        toast_action[i] = 'p';
        toast_values[i] = toast_save_datum(rel, toast_values[i], toast_oldexternal[i], options);
        if (toast_free[i]) {
            pfree(DatumGetPointer(old_value));
        }
        toast_free[i] = true;

        need_change = true;
        need_free = true;
    }

    /*
     * Round 3 - this time we take attributes with storage 'm' into
     * compression
     */
    while (heap_compute_data_size(tuple_desc, toast_values, toast_isnull) > max_data_len) {
        int biggest_attno = -1;
        int32 biggest_size = MAXALIGN(TOAST_POINTER_SIZE);
        Datum old_value;
        Datum new_value;

        /*
         * Search for the biggest yet uncompressed internal attribute
         */
        for (i = 0; i < num_attrs; i++) {
            if (toast_action[i] != ' ') {
                continue;
            }
            if (VARATT_IS_EXTERNAL(DatumGetPointer(toast_values[i]))) {
                continue; /* can't happen, toast_action would be 'p' */
            }
            if (VARATT_IS_COMPRESSED(DatumGetPointer(toast_values[i]))) {
                continue;
            }
            if (att[i].attstorage != 'm') {
                continue;
            }
            if (toast_sizes[i] > biggest_size) {
                biggest_attno = i;
                biggest_size = toast_sizes[i];
            }
        }

        if (biggest_attno < 0) {
            break;
        }

        /*
         * Attempt to compress it inline
         */
        i = biggest_attno;
        old_value = toast_values[i];
        new_value = toast_compress_datum(old_value);
        if (DatumGetPointer(new_value) != NULL) {
            /* successful compression */
            if (toast_free[i]) {
                pfree(DatumGetPointer(old_value));
            }
            toast_values[i] = new_value;
            toast_free[i] = true;
            toast_sizes[i] = VARSIZE(DatumGetPointer(toast_values[i]));
            need_change = true;
            need_free = true;
        } else {
            /* incompressible, ignore on subsequent compression passes */
            toast_action[i] = 'x';
        }
    }

    /*
     * Finally we store attributes of type 'm' externally.	At this point we
     * increase the target tuple size, so that 'm' attributes aren't stored
     * externally unless really necessary.
     */
    max_data_len = TOAST_TUPLE_TARGET_MAIN - hoff;

    while (heap_compute_data_size(tuple_desc, toast_values, toast_isnull) > max_data_len &&
           rel->rd_rel->reltoastrelid != InvalidOid) {
        int biggest_attno = -1;
        int32 biggest_size = MAXALIGN(TOAST_POINTER_SIZE);
        Datum old_value;

        /* --------
         * Search for the biggest yet inlined attribute with
         * attstorage = 'm'
         * --------
         */
        for (i = 0; i < num_attrs; i++) {
            if (toast_action[i] == 'p') {
                continue;
            }
            if (VARATT_IS_EXTERNAL(DatumGetPointer(toast_values[i]))) {
                continue; /* can't happen, toast_action would be 'p' */
            }
            if (att[i].attstorage != 'm') {
                continue;
            }
            if (toast_sizes[i] > biggest_size) {
                biggest_attno = i;
                biggest_size = toast_sizes[i];
            }
        }

        if (biggest_attno < 0) {
            break;
        }

        /*
         * Store this external
         */
        i = biggest_attno;
        old_value = toast_values[i];
        toast_action[i] = 'p';
        toast_values[i] = toast_save_datum(rel, toast_values[i], toast_oldexternal[i], options);
        if (toast_free[i]) {
            pfree(DatumGetPointer(old_value));
        }
        toast_free[i] = true;

        need_change = true;
        need_free = true;
    }

    /*
     * In the case we toasted any values, we need to build a new heap tuple
     * with the changed values.
     */
    if (need_change) {
        HeapTupleHeader olddata = newtup->t_data;
        HeapTupleHeader new_data;
        int32 new_header_len;
        int32 new_data_len;
        int32 new_tuple_len;

        /*
         * Calculate the new size of the tuple.
         *
         * Note: we used to assume here that the old tuple's t_hoff must equal
         * the new_header_len value, but that was incorrect.  The old tuple
         * might have a smaller-than-current natts, if there's been an ALTER
         * TABLE ADD COLUMN since it was stored; and that would lead to a
         * different conclusion about the size of the null bitmap, or even
         * whether there needs to be one at all.
         */
        new_header_len = offsetof(HeapTupleHeaderData, t_bits);
        if (has_nulls) {
            new_header_len += BITMAPLEN(num_attrs);
        }
        if (olddata->t_infomask & HEAP_HASOID) {
            new_header_len += sizeof(Oid);
        }
        new_header_len = MAXALIGN((uint32)new_header_len);
        new_data_len = heap_compute_data_size(tuple_desc, toast_values, toast_isnull);
        new_tuple_len = new_header_len + new_data_len;

        /*
         * Allocate and zero the space needed, and fill HeapTupleData fields.
         */
        result_tuple = (HeapTuple)heaptup_alloc(HEAPTUPLESIZE + new_tuple_len);
        result_tuple->t_len = new_tuple_len;
        result_tuple->t_self = newtup->t_self;
        result_tuple->t_tableOid = newtup->t_tableOid;
        result_tuple->t_bucketId = newtup->t_bucketId;
        HeapTupleCopyBase(result_tuple, newtup);
#ifdef PGXC
        result_tuple->t_xc_node_id = newtup->t_xc_node_id;
#endif

        new_data = (HeapTupleHeader)((char *)result_tuple + HEAPTUPLESIZE);
        result_tuple->t_data = new_data;

        /*
         * Copy the existing tuple header, but adjust natts and t_hoff.
         */
        rc = memcpy_s(new_data, SizeofHeapTupleHeader, olddata, offsetof(HeapTupleHeaderData, t_bits));
        securec_check(rc, "", "");
        HeapTupleHeaderSetNatts(new_data, num_attrs);
        new_data->t_hoff = new_header_len;
        if (olddata->t_infomask & HEAP_HASOID) {
            HeapTupleHeaderSetOid(new_data, HeapTupleHeaderGetOid(olddata));
        }

        /* Copy over the data, and fill the null bitmap if needed */
        heap_fill_tuple(tuple_desc, toast_values, toast_isnull, (char *)new_data + new_header_len, new_data_len,
                        &(new_data->t_infomask), has_nulls ? new_data->t_bits : NULL);
    } else {
        result_tuple = newtup;
    }

    /*
     * Free allocated temp values
     */
    if (need_free) {
        for (i = 0; i < num_attrs; i++) {
            if (toast_free[i]) {
                pfree(DatumGetPointer(toast_values[i]));
            }
        }
    }

    /*
     * Delete external values from the old tuple
     */
    if (need_delold) {
        for (i = 0; i < num_attrs; i++) {
            if (toast_delold[i]) {
                delete_old_tuple_toast(rel, toast_oldvalues[i], options, allow_update_self);
            }
        }
    }

    return result_tuple;
}

/* ----------
 * toast_flatten_tuple -
 *
 *	"Flatten" a tuple to contain no out-of-line toasted fields.
 *	(This does not eliminate compressed or short-header datums.)
 * ----------
 */
HeapTuple toast_flatten_tuple(HeapTuple tup, TupleDesc tuple_desc)
{
    HeapTuple new_tuple;
    FormData_pg_attribute *att = tuple_desc->attrs;
    int num_attrs = tuple_desc->natts;
    int i;
    Datum toast_values[MaxTupleAttributeNumber];
    bool toast_isnull[MaxTupleAttributeNumber];
    bool toast_free[MaxTupleAttributeNumber];

    /*
     * Break down the tuple into fields.
     */
    Assert(num_attrs <= MaxTupleAttributeNumber);

    heap_deform_tuple(tup, tuple_desc, toast_values, toast_isnull);

    errno_t rc = memset_s(toast_free, MaxTupleAttributeNumber * sizeof(bool), 0, num_attrs * sizeof(bool));
    securec_check(rc, "", "");
    for (i = 0; i < num_attrs; i++) {
        /*
         * Look at non-null varlena attributes
         */
        if (!toast_isnull[i] && att[i].attlen == -1) {
            struct varlena *new_value;

            new_value = (struct varlena *)DatumGetPointer(toast_values[i]);
            checkHugeToastPointer(new_value);
            if (VARATT_IS_EXTERNAL(new_value)) {
                new_value = toast_fetch_datum(new_value);
                toast_values[i] = PointerGetDatum(new_value);
                toast_free[i] = true;
            }
        }
    }

    /*
     * Form the reconfigured tuple.
     */
    new_tuple = heap_form_tuple(tuple_desc, toast_values, toast_isnull);

    /*
     * Be sure to copy the tuple's OID and identity fields.  We also make a
     * point of copying visibility info, just in case anybody looks at those
     * fields in a syscache entry.
     */
    if (tuple_desc->tdhasoid)
        HeapTupleSetOid(new_tuple, HeapTupleGetOid(tup));

    new_tuple->t_self = tup->t_self;
    new_tuple->t_tableOid = tup->t_tableOid;
    new_tuple->t_bucketId = tup->t_bucketId;
    HeapTupleCopyBase(new_tuple, tup);

    new_tuple->t_data->t_choice = tup->t_data->t_choice;
    new_tuple->t_data->t_ctid = tup->t_data->t_ctid;
    new_tuple->t_data->t_infomask &= ~HEAP_XACT_MASK;
    new_tuple->t_data->t_infomask |= tup->t_data->t_infomask & HEAP_XACT_MASK;
    new_tuple->t_data->t_infomask2 &= ~HEAP2_XACT_MASK;
    new_tuple->t_data->t_infomask2 |= tup->t_data->t_infomask2 & HEAP2_XACT_MASK;

    /*
     * Free allocated temp values
     */
    for (i = 0; i < num_attrs; i++)
        if (toast_free[i])
            pfree(DatumGetPointer(toast_values[i]));

    return new_tuple;
}

/* ----------
 * toast_flatten_tuple_attribute -
 *
 *	If a Datum is of composite type, "flatten" it to contain no toasted fields.
 *	This must be invoked on any potentially-composite field that is to be
 *	inserted into a tuple.	Doing this preserves the invariant that toasting
 *	goes only one level deep in a tuple.
 *
 *	Note that flattening does not mean expansion of short-header varlenas,
 *	so in one sense toasting is allowed within composite datums.
 * ----------
 */
Datum toast_flatten_tuple_attribute(Datum value, Oid typeId, int32 typeMod)
{
    TupleDesc tuple_desc;
    HeapTupleHeader olddata;
    HeapTupleHeader new_data;
    int32 new_header_len;
    int32 new_data_len;
    int32 new_tuple_len;
    HeapTupleData tmptup;
    FormData_pg_attribute *att = NULL;
    int num_attrs;
    int i;
    bool need_change = false;
    bool has_nulls = false;
    Datum toast_values[MaxTupleAttributeNumber];
    bool toast_isnull[MaxTupleAttributeNumber];
    bool toast_free[MaxTupleAttributeNumber];
    errno_t rc;
    /*
     * See if it's a composite type, and get the tupdesc if so.
     */
    tuple_desc = lookup_rowtype_tupdesc_noerror(typeId, typeMod, true);
    if (tuple_desc == NULL)
        return value; /* not a composite type */

    att = tuple_desc->attrs;
    num_attrs = tuple_desc->natts;

    /*
     * Break down the tuple into fields.
     */
    olddata = DatumGetHeapTupleHeader(value);
    Assert(typeId == HeapTupleHeaderGetTypeId(olddata));
    Assert(typeMod == HeapTupleHeaderGetTypMod(olddata));
    /* Build a temporary HeapTuple control structure */
    tmptup.t_len = HeapTupleHeaderGetDatumLength(olddata);
    ItemPointerSetInvalid(&(tmptup.t_self));
    tmptup.t_tableOid = InvalidOid;
    tmptup.t_bucketId = InvalidBktId;
    HeapTupleSetZeroBase(&tmptup);
#ifdef PGXC
    tmptup.t_xc_node_id = 0;
#endif
    tmptup.t_data = olddata;

    Assert(num_attrs <= MaxTupleAttributeNumber);
    if (tuple_desc->natts > MaxTupleAttributeNumber)
        ereport(ERROR,
                (errcode(ERRCODE_TOO_MANY_COLUMNS),
                 errmsg("number of columns (%d) exceeds limit (%d), AM type (%d), type id (%u)", tuple_desc->natts,
                        MaxTupleAttributeNumber, GetTableAmType(tuple_desc->td_tam_ops), tuple_desc->tdtypeid)));

    heap_deform_tuple(&tmptup, tuple_desc, toast_values, toast_isnull);

    rc = memset_s(toast_free, num_attrs * sizeof(bool), 0, num_attrs * sizeof(bool));
    securec_check(rc, "", "");
    for (i = 0; i < num_attrs; i++) {
        /*
         * Look at non-null varlena attributes
         */
        if (toast_isnull[i])
            has_nulls = true;
        else if (att[i].attlen == -1) {
            struct varlena *new_value;

            new_value = (struct varlena *)DatumGetPointer(toast_values[i]);
            if (VARATT_IS_EXTERNAL(new_value) || VARATT_IS_COMPRESSED(new_value)) {
                new_value = heap_tuple_untoast_attr(new_value);
                toast_values[i] = PointerGetDatum(new_value);
                toast_free[i] = true;
                need_change = true;
            }
        }
    }

    /*
     * If nothing to untoast, just return the original tuple.
     */
    if (!need_change) {
        ReleaseTupleDesc(tuple_desc);
        return value;
    }

    /*
     * Calculate the new size of the tuple.
     *
     * This should match the reconstruction code in toast_insert_or_update.
     */
    new_header_len = offsetof(HeapTupleHeaderData, t_bits);
    if (has_nulls)
        new_header_len += BITMAPLEN(num_attrs);
    if (olddata->t_infomask & HEAP_HASOID)
        new_header_len += sizeof(Oid);
    new_header_len = MAXALIGN((uint32)new_header_len);
    new_data_len = heap_compute_data_size(tuple_desc, toast_values, toast_isnull);
    new_tuple_len = new_header_len + new_data_len;

    new_data = (HeapTupleHeader)palloc0(new_tuple_len);

    /*
     * Copy the existing tuple header, but adjust natts and t_hoff.
     */
    rc = memcpy_s(new_data, new_tuple_len, olddata, offsetof(HeapTupleHeaderData, t_bits));
    securec_check(rc, "", "");
    HeapTupleHeaderSetNatts(new_data, num_attrs);
    new_data->t_hoff = new_header_len;
    if (olddata->t_infomask & HEAP_HASOID)
        HeapTupleHeaderSetOid(new_data, HeapTupleHeaderGetOid(olddata));

    /* Reset the datum length field, too */
    HeapTupleHeaderSetDatumLength(new_data, new_tuple_len);

    /* Copy over the data, and fill the null bitmap if needed */
    heap_fill_tuple(tuple_desc, toast_values, toast_isnull, (char *)new_data + new_header_len, new_data_len,
                    &(new_data->t_infomask), has_nulls ? new_data->t_bits : NULL);

    /*
     * Free allocated temp values
     */
    for (i = 0; i < num_attrs; i++)
        if (toast_free[i])
            pfree(DatumGetPointer(toast_values[i]));
    ReleaseTupleDesc(tuple_desc);

    return PointerGetDatum(new_data);
}

void toast_huge_fetch_and_copy_level2(Relation srctoastrel, Relation srctoastidx, Relation destoastrel,
    Relation destoastidx, varatt_external toast_pointer, Oid chunk_id)
{
    ScanKeyData toastkey;
    SysScanDesc toastscan;
    TupleDesc src_toast_tup_desc = srctoastrel->rd_att;
    TupleDesc dest_toast_tup_desc = destoastrel->rd_att;
    int32 residx;
    Pointer chunk = NULL;
    HeapTuple ttup;
    HeapTuple toasttup;
    bool isnull = false;
    Datum t_values[3];
    bool t_isnull[3];
    int32 totalsize = 0;
    errno_t rc;
    CommandId mycid = GetCurrentCommandId(true);
    struct varlena *result = NULL;

    ScanKeyInit(&toastkey, (AttrNumber)1, BTEqualStrategyNumber, F_OIDEQ, ObjectIdGetDatum(toast_pointer.va_valueid));
    toastscan = systable_beginscan_ordered(srctoastrel, srctoastidx, SnapshotToast, 1, &toastkey);
    while ((ttup = systable_getnext_ordered(toastscan, ForwardScanDirection)) != NULL) {
        residx = DatumGetInt32(fastgetattr(ttup, CHUNK_ID_ATTR, src_toast_tup_desc, &isnull));
        chunk = DatumGetPointer(fastgetattr(ttup, CHUNK_DATA_ATTR, src_toast_tup_desc, &isnull));
        t_values[0] = ObjectIdGetDatum(chunk_id);
        t_values[1] = Int32GetDatum(residx);
        if (!VARATT_IS_EXTENDED(chunk)) {
            totalsize = VARSIZE(chunk);
        } else if (VARATT_IS_SHORT(chunk)) {
            totalsize = VARSIZE_SHORT(chunk);
        }
        result = (varlena *)palloc(totalsize);
        rc = memcpy_s(result, totalsize, chunk, totalsize);
        securec_check(rc, "\0", "\0");
        t_values[2] = PointerGetDatum(result);
        t_isnull[0] = false;
        t_isnull[1] = false;
        t_isnull[2] = false;
        toasttup = heap_form_tuple(dest_toast_tup_desc, t_values, t_isnull);
        (void)heap_insert(destoastrel, toasttup, mycid, 0, NULL);
        (void)index_insert(destoastidx, t_values, t_isnull, &(toasttup->t_self), destoastrel,
            destoastidx->rd_index->indisunique ? UNIQUE_CHECK_YES : UNIQUE_CHECK_NO);
        heap_freetuple(toasttup);
        pfree_ext(result);
    }
    systable_endscan_ordered(toastscan);
}

void toast_huge_fetch_and_copy_level1(Relation srctoastrel, Relation srctoastidx, Relation destoastrel,
    Relation destoastidx, HeapTuple ttup, Oid firstchunkid, int32 *chunk_seq, Oid realtoastOid)
{
    Oid chunk_id = InvalidOid;
    int2 bucketid;
    Datum t_values[3];
    bool t_isnull[3];
    errno_t rc;
    bool isnull;
    TupleDesc dest_toast_tup_desc = destoastrel->rd_att;
    TupleDesc src_toast_tup_desc = srctoastrel->rd_att;
    CommandId mycid = GetCurrentCommandId(true);
    Pointer chunk = DatumGetPointer(fastgetattr(ttup, CHUNK_DATA_ATTR, src_toast_tup_desc, &isnull));
    struct varatt_external toast_pointer;
    VARATT_EXTERNAL_GET_POINTER_B(toast_pointer, chunk, bucketid);
    if (OidIsValid(realtoastOid)) {
        chunk_id = toast_pointer.va_valueid;
    } else {
        chunk_id = GetNewOidWithIndex(destoastrel, RelationGetRelid(destoastidx), (AttrNumber)1);
    }
    toast_huge_fetch_and_copy_level2(srctoastrel, srctoastidx, destoastrel, destoastidx, toast_pointer, chunk_id);
    toast_pointer.va_valueid = chunk_id;
    if (OidIsValid(realtoastOid)) {
        toast_pointer.va_toastrelid = realtoastOid;
    } else {
        toast_pointer.va_toastrelid = destoastrel->rd_id;
    }
    struct varlena *tmp = (struct varlena *)palloc(TOAST_POINTER_SIZE);
    SET_VARTAG_EXTERNAL(tmp, VARTAG_ONDISK);
    rc = memcpy_s(VARDATA_EXTERNAL(tmp), TOAST_POINTER_SIZE, &toast_pointer, sizeof(toast_pointer));
    securec_check(rc, "", "");

    t_values[0] = ObjectIdGetDatum(firstchunkid);
    t_values[1] = Int32GetDatum((*chunk_seq)++);
    t_values[2] = PointerGetDatum(tmp);
    t_isnull[0] = false;
    t_isnull[1] = false;
    t_isnull[2] = false;
    HeapTuple toasttup = heap_form_tuple(dest_toast_tup_desc, t_values, t_isnull);
    toasttup->t_data->t_infomask &= (~HEAP_HASEXTERNAL);
    (void)heap_insert(destoastrel, toasttup, mycid, 0, NULL);
    (void)index_insert(destoastidx, t_values, t_isnull, &(toasttup->t_self), destoastrel,
        destoastidx->rd_index->indisunique ? UNIQUE_CHECK_YES : UNIQUE_CHECK_NO);
    heap_freetuple(toasttup);
    pfree_ext(tmp);
}

void toast_huge_fetch_and_copy(Relation srctoastrel, Relation srctoastidx, Relation destoastrel,
    Relation destoastidx, varatt_lob_external large_toast_pointer, int32 *chunk_seq, Oid *firstchunkid,
    Oid realtoastOid)
{
    ScanKeyData toastkey;
    SysScanDesc toastscan;
    HeapTuple ttup;
    struct varatt_external toast_pointer;
    Pointer chunk;
    bool isnull = false;
    int2 bucketid;

    if (*firstchunkid == InvalidOid) {
        if (OidIsValid(realtoastOid)) {
            *firstchunkid = large_toast_pointer.va_valueid;
        } else {
            *firstchunkid = GetNewOidWithIndex(destoastrel, RelationGetRelid(destoastidx), (AttrNumber)1);
        }
    }

    ScanKeyInit(&toastkey, (AttrNumber)1, BTEqualStrategyNumber, F_OIDEQ,
        ObjectIdGetDatum(large_toast_pointer.va_valueid));
    toastscan = systable_beginscan_ordered(srctoastrel, srctoastidx, SnapshotToast, 1, &toastkey);
    while ((ttup = systable_getnext_ordered(toastscan, ForwardScanDirection)) != NULL) {
        bool isSameTransAndQuery = TransactionIdIsCurrentTransactionId(HeapTupleGetRawXmin(ttup));
        if (isSameTransAndQuery) {
            isSameTransAndQuery = (GetCurrentCommandId(true) == HeapTupleGetCmin(ttup));
        }

        if (isSameTransAndQuery && srctoastrel->rd_id == destoastrel->rd_id) {
            TupleDesc src_toast_tup_desc = srctoastrel->rd_att;
            chunk = DatumGetPointer(fastgetattr(ttup, CHUNK_DATA_ATTR, src_toast_tup_desc, &isnull));
            VARATT_EXTERNAL_GET_POINTER_B(toast_pointer, chunk, bucketid);
        }
        toast_huge_fetch_and_copy_level1(srctoastrel, srctoastidx, destoastrel, destoastidx, ttup, *firstchunkid,
            chunk_seq, realtoastOid);
        if (isSameTransAndQuery && srctoastrel->rd_id == destoastrel->rd_id) {
            toast_delete_datum_internal(toast_pointer, 0, true);
            simple_heap_delete(destoastrel, &ttup->t_self, 0, true);
        }
    }
    systable_endscan_ordered(toastscan);
}

void toast_huge_concat_hugepointers(text* t1, text* t2, Oid *firstchunkid, Oid toastOid)
{
    Relation srctoastrel;
    Relation srctoastidx;
    Relation destoastrel;
    Relation destoastidx;
    int32 firstchunkseq = 0;
    struct varatt_lob_external large_toast_pointer1;
    struct varatt_lob_external large_toast_pointer2;

    VARATT_EXTERNAL_GET_HUGE_POINTER(large_toast_pointer1, t1);
    VARATT_EXTERNAL_GET_HUGE_POINTER(large_toast_pointer2, t2);
    destoastrel = heap_open(toastOid, RowExclusiveLock);
    destoastidx = index_open(destoastrel->rd_rel->reltoastidxid, RowExclusiveLock);
    srctoastrel = heap_open(large_toast_pointer1.va_toastrelid, AccessShareLock);
    srctoastidx = index_open(srctoastrel->rd_rel->reltoastidxid, AccessShareLock);
    toast_huge_fetch_and_copy(srctoastrel, srctoastidx, destoastrel, destoastidx, large_toast_pointer1,
        &firstchunkseq, firstchunkid, InvalidOid);
    index_close(srctoastidx, AccessShareLock);
    heap_close(srctoastrel, AccessShareLock);

    srctoastrel = heap_open(large_toast_pointer2.va_toastrelid, AccessShareLock);
    srctoastidx = index_open(srctoastrel->rd_rel->reltoastidxid, AccessShareLock);
    toast_huge_fetch_and_copy(srctoastrel, srctoastidx, destoastrel, destoastidx, large_toast_pointer2,
        &firstchunkseq, firstchunkid, InvalidOid);
    index_close(srctoastidx, AccessShareLock);
    heap_close(srctoastrel, AccessShareLock);

    index_close(destoastidx, RowExclusiveLock);
    heap_close(destoastrel, RowExclusiveLock);
}

void toast_huge_concat_varlenas_internal(Relation toastrel, Relation toastidx, text *t1, text *t2, Oid *firstchunkid,
    int32 *chunkseq)
{
    text *tmp = NULL;
    char *ptr = NULL;
    errno_t rc;
    HeapTuple toasttup;
    Datum values[3];
    bool isnull[3];
    struct varatt_external first_toast_pointer;
    char *data = NULL;
    int32 data_all;
    union {
        struct varlena hdr;
        char data[TOAST_MAX_CHUNK_SIZE + sizeof(struct varlena) + sizeof(int32)]; /* make struct big enough */
        int32 align_it;                  /* ensure struct is aligned well enough */
    } chunk_data;
    int32 chunk_size;
    int32 chunk_seq = 0;
    CommandId mycid = GetCurrentCommandId(true);
    int64 len1 = VARSIZE_ANY_EXHDR(t1);
    int64 len2 = VARSIZE_ANY_EXHDR(t2);
    int64 len = len1 + len2;
    TupleDesc toast_tup_desc = toastrel->rd_att;
    bool needfree = false;
    Assert(*firstchunkid == InvalidOid);
    *firstchunkid = GetNewOidWithIndex(toastrel, RelationGetRelid(toastidx), (AttrNumber)1);

    while (len > 0) {
        chunk_seq = 0;
        int l1 = len > MAX_TOAST_CHUNK_SIZE ? MAX_TOAST_CHUNK_SIZE : len;
        tmp = (struct varlena *)palloc(l1 + VARHDRSZ);
        SET_VARSIZE(tmp, l1 + VARHDRSZ);
        ptr = VARDATA(tmp);
        if (l1 == len) {
            rc = memcpy_s(ptr, l1, VARDATA_ANY(t2) + (MAX_TOAST_CHUNK_SIZE - len1), l1);
            securec_check(rc, "\0", "\0");
        } else {
            rc = memcpy_s(ptr, len1, VARDATA_ANY(t1), len1);
            securec_check(rc, "\0", "\0");
            rc = memcpy_s(ptr + len1, l1 - len1, VARDATA_ANY(t2), l1 - len1);
            securec_check(rc, "\0", "\0");
        }
        len -= MAX_TOAST_CHUNK_SIZE;
        Pointer dval = DatumGetPointer(toast_compress_datum(PointerGetDatum(tmp)));
        if (PointerIsValid(dval) && VARATT_IS_COMPRESSED(dval)) {
            data = VARDATA(dval);
            data_all = VARSIZE(dval) - VARHDRSZ;
            /* rawsize in a compressed datum is just the size of the payload */
            first_toast_pointer.va_rawsize = VARRAWSIZE_4B_C(dval) + VARHDRSZ;
            first_toast_pointer.va_extsize = data_all;
            needfree = true;
            /* Assert that the numbers look like it's compressed */
            Assert(VARATT_EXTERNAL_IS_COMPRESSED(first_toast_pointer));
        } else {
            data = VARDATA(tmp);
            data_all = VARSIZE(tmp) - VARHDRSZ;
            first_toast_pointer.va_rawsize = VARSIZE(tmp);
            first_toast_pointer.va_extsize = data_all;
        }

        first_toast_pointer.va_toastrelid = toastrel->rd_id;
        first_toast_pointer.va_valueid = GetNewOidWithIndex(toastrel, RelationGetRelid(toastidx), (AttrNumber)1);
        rc = memset_s(&chunk_data, sizeof(chunk_data), 0, sizeof(chunk_data));
        securec_check(rc, "", "");
        values[0] = ObjectIdGetDatum(first_toast_pointer.va_valueid);
        values[2] = PointerGetDatum(&chunk_data);
        isnull[0] = false;
        isnull[1] = false;
        isnull[2] = false;
        while (data_all > 0) {
            chunk_size = Min(TOAST_MAX_CHUNK_SIZE, (uint32)data_all);
            values[1] = Int32GetDatum(chunk_seq++);
            SET_VARSIZE(&chunk_data, chunk_size + VARHDRSZ);
            rc = memcpy_s(VARDATA(&chunk_data), TOAST_MAX_CHUNK_SIZE, data, chunk_size);
            securec_check(rc, "", "");
            toasttup = heap_form_tuple(toast_tup_desc, values, isnull);
            (void)heap_insert(toastrel, toasttup, mycid, 0, NULL);
            (void)index_insert(toastidx, values, isnull, &(toasttup->t_self), toastrel,
                toastidx->rd_index->indisunique ? UNIQUE_CHECK_YES : UNIQUE_CHECK_NO);
            heap_freetuple(toasttup);
            data_all -= chunk_size;
            data += chunk_size;
        }

        struct varlena *result = (struct varlena *)palloc(TOAST_POINTER_SIZE);
        SET_VARTAG_EXTERNAL(result, VARTAG_ONDISK);
        rc = memcpy_s(VARDATA_EXTERNAL(result), TOAST_POINTER_SIZE, &first_toast_pointer, sizeof(first_toast_pointer));
        securec_check(rc, "", "");
        values[0] = ObjectIdGetDatum(*firstchunkid);
        values[1] = Int32GetDatum((*chunkseq)++);
        values[2] = PointerGetDatum(result);
        isnull[0] = false;
        isnull[1] = false;
        isnull[2] = false;
        toasttup = heap_form_tuple(toast_tup_desc, values, isnull);
        toasttup->t_data->t_infomask &= (~HEAP_HASEXTERNAL);
        (void)heap_insert(toastrel, toasttup, mycid, 0, NULL);
        (void)index_insert(toastidx, values, isnull, &(toasttup->t_self), toastrel,
            toastidx->rd_index->indisunique ? UNIQUE_CHECK_YES : UNIQUE_CHECK_NO);
        heap_freetuple(toasttup);
        pfree_ext(tmp);
        pfree_ext(result);
        if (needfree) {
            pfree_ext(dval);
        }
    }
}

void toast_huge_concat_varlenas(text* t1, text* t2, Oid *firstchunkid, int32 *chunkseq, Oid toastOid)
{
    Relation toastrel = heap_open(toastOid, RowExclusiveLock);
    Relation toastidx = index_open(toastrel->rd_rel->reltoastidxid, RowExclusiveLock);
    toast_huge_concat_varlenas_internal(toastrel, toastidx, t1, t2, firstchunkid, chunkseq);
    index_close(toastidx, RowExclusiveLock);
    heap_close(toastrel, RowExclusiveLock);
}

void toast_huge_fetch_and_append_datum(Relation toastrel, Relation toastidx, text *t, Oid *firstchunkid, int32 chunkseq)
{
    TupleDesc toast_tup_desc = toastrel->rd_att;
    struct varatt_external toast_pointer1;
    Datum toast_values[3];
    bool toast_isnull[3];
    char *data_all = NULL;
    int32 data_size;
    union {
        struct varlena hdr;
        char data[TOAST_MAX_CHUNK_SIZE + sizeof(struct varlena) + sizeof(int32)]; /* make struct big enough */
        int32 align_it;                  /* ensure struct is aligned well enough */
    } chunk_data;
    int32 chunk_size;
    int32 chunk_seq = 0;
    errno_t rc;
    HeapTuple toasttup;
    struct varlena *result = NULL;
    CommandId mycid = GetCurrentCommandId(true);
    bool needfree = false;
    if (*firstchunkid == InvalidOid) {
        *firstchunkid = GetNewOidWithIndex(toastrel, RelationGetRelid(toastidx), (AttrNumber)1);
    }
    Pointer dval = DatumGetPointer(toast_compress_datum(PointerGetDatum(t)));
    if (PointerIsValid(dval) && VARATT_IS_COMPRESSED(dval)) {
        data_all = VARDATA(dval);
        data_size = VARSIZE(dval) - VARHDRSZ;
        /* rawsize in a compressed datum is just the size of the payload */
        toast_pointer1.va_rawsize = VARRAWSIZE_4B_C(dval) + VARHDRSZ;
        toast_pointer1.va_extsize = data_size;
        needfree = true;
        /* Assert that the numbers look like it's compressed */
        Assert(VARATT_EXTERNAL_IS_COMPRESSED(toast_pointer1));
    } else {
        dval = (Pointer)t;
        data_all = VARDATA(dval);
        data_size = VARSIZE(dval) - VARHDRSZ;
        toast_pointer1.va_rawsize = VARSIZE(dval);
        toast_pointer1.va_extsize = data_size;
    }

    toast_pointer1.va_toastrelid = toastrel->rd_id;
    toast_pointer1.va_valueid = GetNewOidWithIndex(toastrel, RelationGetRelid(toastidx), (AttrNumber)1);
    rc = memset_s(&chunk_data, sizeof(chunk_data), 0, sizeof(chunk_data));
    securec_check(rc, "", "");
    toast_values[0] = ObjectIdGetDatum(toast_pointer1.va_valueid);
    toast_values[2] = PointerGetDatum(&chunk_data);
    toast_isnull[0] = false;
    toast_isnull[1] = false;
    toast_isnull[2] = false;
    while (data_size > 0) {
        chunk_size = Min(TOAST_MAX_CHUNK_SIZE, (uint32)data_size);
        toast_values[1] = Int32GetDatum(chunk_seq++);
        SET_VARSIZE(&chunk_data, chunk_size + VARHDRSZ);
        rc = memcpy_s(VARDATA(&chunk_data), TOAST_MAX_CHUNK_SIZE, data_all, chunk_size);
        securec_check(rc, "", "");
        toasttup = heap_form_tuple(toast_tup_desc, toast_values, toast_isnull);
        (void)heap_insert(toastrel, toasttup, mycid, 0, NULL);
        (void)index_insert(toastidx, toast_values, toast_isnull, &(toasttup->t_self), toastrel,
            toastidx->rd_index->indisunique ? UNIQUE_CHECK_YES : UNIQUE_CHECK_NO);
        heap_freetuple(toasttup);
        data_size -= chunk_size;
        data_all += chunk_size;
    }

    result = (struct varlena *)palloc(TOAST_POINTER_SIZE);
    SET_VARTAG_EXTERNAL(result, VARTAG_ONDISK);
    rc = memcpy_s(VARDATA_EXTERNAL(result), TOAST_POINTER_SIZE, &toast_pointer1, sizeof(toast_pointer1));
    securec_check(rc, "", "");
    toast_values[0] = ObjectIdGetDatum(*firstchunkid);
    toast_values[1] = Int32GetDatum(chunkseq);
    toast_values[2] = PointerGetDatum(result);
    toast_isnull[0] = false;
    toast_isnull[1] = false;
    toast_isnull[2] = false;
    toasttup = heap_form_tuple(toast_tup_desc, toast_values, toast_isnull);
    toasttup->t_data->t_infomask &= (~HEAP_HASEXTERNAL);
    (void)heap_insert(toastrel, toasttup, mycid, 0, NULL);
    (void)index_insert(toastidx, toast_values, toast_isnull, &(toasttup->t_self), toastrel,
        toastidx->rd_index->indisunique ? UNIQUE_CHECK_YES : UNIQUE_CHECK_NO);
    heap_freetuple(toasttup);
    pfree_ext(result);
    if (needfree) {
        pfree_ext(dval);
    }
}

void toast_huge_concat_varlena_after(text *t1, text *t2, Oid *firstchunkid, Oid toastOid)
{
    ScanKeyData toastkey;
    SysScanDesc toastscan;
    HeapTuple ttup;
    Pointer chunk = NULL;
    int32 residx;
    bool isnull;
    errno_t rc;
    char *ptr = NULL;
    struct varatt_lob_external large_toast_pointer;
    struct varatt_external toast_pointer;
    struct varlena *result = NULL;
    struct varlena *tmp = NULL;
    bool newChunk = false;
    int2 bucketid;
    bool islast = true;

    VARATT_EXTERNAL_GET_HUGE_POINTER(large_toast_pointer, t1);

    Relation srctoastrel = heap_open(large_toast_pointer.va_toastrelid, AccessShareLock);
    Relation srctoastidx = index_open(srctoastrel->rd_rel->reltoastidxid, AccessShareLock);
    Relation destoastrel = heap_open(toastOid, RowExclusiveLock);
    Relation destoastidx = index_open(destoastrel->rd_rel->reltoastidxid, RowExclusiveLock);
    TupleDesc src_toast_tup_desc = srctoastrel->rd_att;

    ScanKeyInit(&toastkey, (AttrNumber)1, BTEqualStrategyNumber, F_OIDEQ,
        ObjectIdGetDatum(large_toast_pointer.va_valueid));
    toastscan = systable_beginscan_ordered(srctoastrel, srctoastidx, SnapshotToast, 1, &toastkey);
    while ((ttup = systable_getnext_ordered(toastscan, BackwardScanDirection)) != NULL) {
        bool isSameTransAndQuery = TransactionIdIsCurrentTransactionId(HeapTupleGetRawXmin(ttup));
        if (isSameTransAndQuery && large_toast_pointer.va_toastrelid == toastOid) {
            isSameTransAndQuery = (GetCurrentCommandId(true) == HeapTupleGetCmin(ttup));
        }
        residx = DatumGetInt32(fastgetattr(ttup, CHUNK_ID_ATTR, src_toast_tup_desc, &isnull));
        chunk = DatumGetPointer(fastgetattr(ttup, CHUNK_DATA_ATTR, src_toast_tup_desc, &isnull));
        VARATT_EXTERNAL_GET_POINTER_B(toast_pointer, chunk, bucketid);
        if (islast) {
            if (toast_pointer.va_rawsize == MAX_TOAST_CHUNK_SIZE + VARHDRSZ) {
                newChunk = true;
            } else {
                result = heap_tuple_untoast_attr((varlena *)chunk);
                Assert(VARSIZE_ANY_EXHDR(result) <= MAX_TOAST_CHUNK_SIZE);
            }
            int64 len1 = VARSIZE_ANY_EXHDR(result);
            int64 len2 = VARSIZE_ANY_EXHDR(t2);
            if (!newChunk && len1 + len2 > MAX_TOAST_CHUNK_SIZE) {
                toast_huge_concat_varlenas_internal(destoastrel, destoastidx, result, t2, firstchunkid, &residx);
            } else if (newChunk) {
                toast_huge_fetch_and_append_datum(destoastrel, destoastidx, t2, firstchunkid, residx + 1);
                toast_huge_fetch_and_copy_level1(srctoastrel, srctoastidx, destoastrel, destoastidx, ttup,
                    *firstchunkid, &residx, InvalidOid);
            } else {
                tmp = (varlena *)palloc(len1 + len2 + VARHDRSZ);
                SET_VARSIZE(tmp, len1 + len2 + VARHDRSZ);
                ptr = VARDATA(tmp);
                rc = memcpy_s(ptr, len1, VARDATA_ANY(result), len1);
                securec_check(rc, "\0", "\0");
                rc = memcpy_s(ptr + len1, len2, VARDATA_ANY(t2), len2);
                securec_check(rc, "\0", "\0");
                toast_huge_fetch_and_append_datum(destoastrel, destoastidx, tmp, firstchunkid, residx);
                pfree_ext(tmp);
            }
            islast = false;
            pfree_ext(result);
            if (isSameTransAndQuery && large_toast_pointer.va_toastrelid == toastOid) {
                toast_delete_datum_internal(toast_pointer, 0, true);
                simple_heap_delete(destoastrel, &ttup->t_self, 0, true);
            }
            continue;
        }
        toast_huge_fetch_and_copy_level1(srctoastrel, srctoastidx, destoastrel, destoastidx, ttup, *firstchunkid,
            &residx, InvalidOid);
        if (isSameTransAndQuery && large_toast_pointer.va_toastrelid == toastOid) {
            toast_delete_datum_internal(toast_pointer, 0, true);
            simple_heap_delete(destoastrel, &ttup->t_self, 0, true);
        }
    }
    systable_endscan_ordered(toastscan);

    index_close(srctoastidx, AccessShareLock);
    heap_close(srctoastrel, AccessShareLock);
    index_close(destoastidx, RowExclusiveLock);
    heap_close(destoastrel, RowExclusiveLock);
}

void toast_huge_concat_varlena_before(text *t1, text *t2, Oid *firstchunkid, Oid toastOid)
{
    ScanKeyData toastkey;
    SysScanDesc toastscan;
    HeapTuple ttup;
    int32 residx;
    bool isnull = false;
    struct varatt_lob_external large_toast_pointer;
    VARATT_EXTERNAL_GET_HUGE_POINTER(large_toast_pointer, t2);

    Relation srctoastrel = heap_open(large_toast_pointer.va_toastrelid, AccessShareLock);
    Relation srctoastidx = index_open(srctoastrel->rd_rel->reltoastidxid, AccessShareLock);
    Relation destoastrel = heap_open(toastOid, RowExclusiveLock);
    Relation destoastidx = index_open(destoastrel->rd_rel->reltoastidxid, RowExclusiveLock);
    TupleDesc src_toast_tup_desc = srctoastrel->rd_att;

    toast_huge_fetch_and_append_datum(destoastrel, destoastidx, t1, firstchunkid, 0);
    ScanKeyInit(&toastkey, (AttrNumber)1, BTEqualStrategyNumber, F_OIDEQ,
        ObjectIdGetDatum(large_toast_pointer.va_valueid));
    toastscan = systable_beginscan_ordered(srctoastrel, srctoastidx, SnapshotToast, 1, &toastkey);
    while ((ttup = systable_getnext_ordered(toastscan, BackwardScanDirection)) != NULL) {
        residx = DatumGetInt32(fastgetattr(ttup, CHUNK_ID_ATTR, src_toast_tup_desc, &isnull));
        residx += 1;
        toast_huge_fetch_and_copy_level1(srctoastrel, srctoastidx, destoastrel, destoastidx, ttup, *firstchunkid,
            &residx, InvalidOid);
    }
    systable_endscan_ordered(toastscan);

    index_close(srctoastidx, AccessShareLock);
    heap_close(srctoastrel, AccessShareLock);
    index_close(destoastidx, RowExclusiveLock);
    heap_close(destoastrel, RowExclusiveLock);
}

Oid get_toast_oid()
{
    Oid toastOid = InvalidOid;
    if (OidIsValid(t_thrd.xact_cxt.ActiveLobRelid)) {
        Relation rel = heap_open(t_thrd.xact_cxt.ActiveLobRelid, AccessShareLock);
        if (RelationIsUstoreFormat(rel)) {
            ereport(ERROR, (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
                errmsg("Un-support clob/blob type more than 1GB of Ustore")));
        }
        toastOid = rel->rd_rel->reltoastrelid;
        heap_close(rel, AccessShareLock);
    }
    if (!OidIsValid(toastOid)) {
        create_toast_by_sid(&toastOid);
    }

    return toastOid;
}

static text* text_catenate_convert_vartype(text *t)
{
    text *result = t;
    if (!VARATT_IS_HUGE_TOAST_POINTER(t) && VARATT_IS_1B(t)) {
        int len = VARSIZE_ANY_EXHDR(t);
        result = (text*)palloc(len + VARHDRSZ);
        SET_VARSIZE(result, len + VARHDRSZ);
        errno_t rc = memcpy_s(VARDATA(result), len, VARDATA_ANY(t), len);
        securec_check(rc, "\0", "\0");
    }
    return result;
}

text *text_catenate_huge(text *t1, text *t2, Oid toastOid)
{
    text *result = NULL;
    struct varatt_lob_external large_toast_pointer;
    Oid firstchunkid = InvalidOid;
    int32 chunkseq = 0;
    int64 len1 = VARSIZE_ANY_EXHDR(t1);
    int64 len2 = VARSIZE_ANY_EXHDR(t2);
    errno_t rc;

    t1 = text_catenate_convert_vartype(t1);
    t2 = text_catenate_convert_vartype(t2);

    if (VARATT_IS_HUGE_TOAST_POINTER(t1)) {
        struct varatt_lob_external large_toast_pointer;

        VARATT_EXTERNAL_GET_HUGE_POINTER(large_toast_pointer, t1);
        len1 = large_toast_pointer.va_rawsize;
        if (VARATT_IS_HUGE_TOAST_POINTER(t2)) {
            struct varatt_lob_external large_toast_pointer;

            VARATT_EXTERNAL_GET_HUGE_POINTER(large_toast_pointer, t2);
            len2 = large_toast_pointer.va_rawsize;
            toast_huge_concat_hugepointers(t1, t2, &firstchunkid, toastOid);
        } else if (VARATT_IS_4B(t2)) {
            toast_huge_concat_varlena_after(t1, t2, &firstchunkid, toastOid);
        } else {
            Assert(0);
        }
    } else if (VARATT_IS_4B(t1)) {
        if (VARATT_IS_HUGE_TOAST_POINTER(t2)) {
            struct varatt_lob_external large_toast_pointer;

            VARATT_EXTERNAL_GET_HUGE_POINTER(large_toast_pointer, t2);
            len2 = large_toast_pointer.va_rawsize;
            toast_huge_concat_varlena_before(t1, t2, &firstchunkid, toastOid);
        } else if (VARATT_IS_4B(t2)) {
            toast_huge_concat_varlenas(t1, t2, &firstchunkid, &chunkseq, toastOid);
        } else {
            Assert(0);
        }
    } else {
        Assert(0);
    }

    int64 len = len1 + len2;
    result = (struct varlena *)palloc(LARGE_TOAST_POINTER_SIZE);
    SET_HUGE_TOAST_POINTER_TAG(result, VARTAG_ONDISK);
    large_toast_pointer.va_rawsize = len;
    large_toast_pointer.va_toastrelid = toastOid;
    large_toast_pointer.va_valueid = firstchunkid;
    rc =
        memcpy_s(VARDATA_EXTERNAL(result), LARGE_TOAST_POINTER_SIZE, &large_toast_pointer, sizeof(large_toast_pointer));
    securec_check(rc, "", "");

    return result;
}

/* ----------
 * toast_compress_datum -
 *
 *	Create a compressed version of a varlena datum
 *
 *	If we fail (ie, compressed result is actually bigger than original)
 *	then return NULL.  We must not use compressed data if it'd expand
 *	the tuple!
 *
 *	We use VAR{SIZE,DATA}_ANY so we can handle short varlenas here without
 *	copying them.  But we can't handle external or compressed datums.
 * ----------
 */
Datum toast_compress_datum(Datum value)
{
    struct varlena *tmp = NULL;
    int32 valsize = VARSIZE_ANY_EXHDR(DatumGetPointer(value));

    Assert(!VARATT_IS_EXTERNAL(DatumGetPointer(value)));
    Assert(!VARATT_IS_COMPRESSED(DatumGetPointer(value)));

    /*
     * No point in wasting a palloc cycle if value size is out of the allowed
     * range for compression
     */
    if (valsize < PGLZ_strategy_default->min_input_size || valsize > PGLZ_strategy_default->max_input_size)
        return PointerGetDatum(NULL);

    tmp = (struct varlena *)palloc(PGLZ_MAX_OUTPUT(valsize));
    /*
     * We recheck the actual size even if pglz_compress() reports success,
     * because it might be satisfied with having saved as little as one byte
     * in the compressed data --- which could turn into a net loss once you
     * consider header and alignment padding.  Worst case, the compressed
     * format might require three padding bytes (plus header, which is
     * included in VARSIZE(tmp)), whereas the uncompressed format would take
     * only one header byte and no padding if the value is short enough.  So
     * we insist on a savings of more than 2 bytes to ensure we have a gain.
     */
    if (pglz_compress(VARDATA_ANY(DatumGetPointer(value)), valsize, (PGLZ_Header *)tmp, PGLZ_strategy_default) &&
        VARSIZE(tmp) < (uint32)(valsize - 2)) {
        /* successful compression */
        return PointerGetDatum(tmp);
    } else {
        /* incompressible data */
        pfree(tmp);
        return PointerGetDatum(NULL);
    }
}

/* ----------
 * toast_save_datum -
 *
 *	Save one single datum into the secondary relation and return
 *	a Datum reference for it.
 *
 * rel: the main relation we're working with (not the toast rel!)
 * value: datum to be pushed to toast storage
 * oldexternal: if not NULL, toast pointer previously representing the datum
 * options: options to be passed to heap_insert() for toast rows
 * ----------
 */
Datum toast_save_datum(Relation rel, Datum value, struct varlena *oldexternal, int options)
{
    Relation toastrel;
    Relation toastidx;
    HeapTuple toasttup;
    TupleDesc toast_tup_desc;
    Datum t_values[3];
    bool t_isnull[3];
    CommandId mycid = GetCurrentCommandId(true);
    struct varlena *result = NULL;
    struct varatt_external toast_pointer;
    union {
        struct varlena hdr;
        char data[TOAST_MAX_CHUNK_SIZE + sizeof(struct varlena) + sizeof(int32)]; /* make struct big enough */
        int32 align_it;                  /* ensure struct is aligned well enough */
    } chunk_data;
    int32 chunk_size;
    int32 chunk_seq = 0;
    char *data_p = NULL;
    int32 data_todo;
    Pointer dval = DatumGetPointer(value);
    errno_t rc;
    int2 bucketid = InvalidBktId;
    Assert(!VARATT_IS_EXTERNAL(value));
    rc = memset_s(&chunk_data, sizeof(chunk_data), 0, sizeof(chunk_data));
    securec_check(rc, "", "");

    /*
     * Open the toast relation and its index.  We can use the index to check
     * uniqueness of the OID we assign to the toasted item, even though it has
     * additional columns besides OID.
     */
    if (RelationIsBucket(rel)) {
        bucketid = rel->rd_node.bucketNode;
    }
    toastrel = heap_open(rel->rd_rel->reltoastrelid, RowExclusiveLock, bucketid);
    toast_tup_desc = toastrel->rd_att;
    toastidx = index_open(toastrel->rd_rel->reltoastidxid, RowExclusiveLock, bucketid);

    /*
     * Get the data pointer and length, and compute va_rawsize and va_extsize.
     *
     * va_rawsize is the size of the equivalent fully uncompressed datum, so
     * we have to adjust for short headers.
     *
     * va_extsize is the actual size of the data payload in the toast records.
     */
    if (VARATT_IS_SHORT(dval)) {
        data_p = VARDATA_SHORT(dval);
        data_todo = VARSIZE_SHORT(dval) - VARHDRSZ_SHORT;
        toast_pointer.va_rawsize = data_todo + VARHDRSZ; /* as if not short */
        toast_pointer.va_extsize = data_todo;
    } else if (VARATT_IS_COMPRESSED(dval)) {
        data_p = VARDATA(dval);
        data_todo = VARSIZE(dval) - VARHDRSZ;
        /* rawsize in a compressed datum is just the size of the payload */
        toast_pointer.va_rawsize = VARRAWSIZE_4B_C(dval) + VARHDRSZ;
        toast_pointer.va_extsize = data_todo;
        /* Assert that the numbers look like it's compressed */
        Assert(VARATT_EXTERNAL_IS_COMPRESSED(toast_pointer));
    } else {
        data_p = VARDATA(dval);
        data_todo = VARSIZE(dval) - VARHDRSZ;
        toast_pointer.va_rawsize = VARSIZE(dval);
        toast_pointer.va_extsize = data_todo;
    }

    /*
     * Insert the correct table OID into the result TOAST pointer.
     *
     * Normally this is the actual OID of the target toast table, but during
     * table-rewriting operations such as CLUSTER, we have to insert the OID
     * of the table's real permanent toast table instead.  rd_toastoid is set
     * if we have to substitute such an OID.
     */
    if (OidIsValid(rel->rd_toastoid))
        toast_pointer.va_toastrelid = rel->rd_toastoid;
    else
        toast_pointer.va_toastrelid = RelationGetRelid(toastrel);

    /*
     * Choose an OID to use as the value ID for this toast value.
     *
     * Normally we just choose an unused OID within the toast table.  But
     * during table-rewriting operations where we are preserving an existing
     * toast table OID, we want to preserve toast value OIDs too.  So, if
     * rd_toastoid is set and we had a prior external value from that same
     * toast table, re-use its value ID.  If we didn't have a prior external
     * value (which is a corner case, but possible if the table's attstorage
     * options have been changed), we have to pick a value ID that doesn't
     * conflict with either new or existing toast value OIDs.
     */
    if (!OidIsValid(rel->rd_toastoid)) {
        /* normal case: just choose an unused OID */
        toast_pointer.va_valueid = GetNewOidWithIndex(toastrel, RelationGetRelid(toastidx), (AttrNumber)1);
    } else {
        /* rewrite case: check to see if value was in old toast table */
        toast_pointer.va_valueid = InvalidOid;
        if (oldexternal != NULL) {
            struct varatt_external old_toast_pointer;
            int2 toastbid;

            Assert(VARATT_IS_EXTERNAL_ONDISK_B(oldexternal));
            /* Must copy to access aligned fields */
            VARATT_EXTERNAL_GET_POINTER_B(old_toast_pointer, oldexternal, toastbid);
            if (old_toast_pointer.va_toastrelid == rel->rd_toastoid) {
                Assert(bucketid == toastbid);
                /* This value came from the old toast table; reuse its OID */
                toast_pointer.va_valueid = old_toast_pointer.va_valueid;

                /*
                 * There is a corner case here: the table rewrite might have
                 * to copy both live and recently-dead versions of a row, and
                 * those versions could easily reference the same toast value.
                 * When we copy the second or later version of such a row,
                 * reusing the OID will mean we select an OID that's already
                 * in the new toast table.	Check for that, and if so, just
                 * fall through without writing the data again.
                 *
                 * While annoying and ugly-looking, this is a good thing
                 * because it ensures that we wind up with only one copy of
                 * the toast value when there is only one copy in the old
                 * toast table.  Before we detected this case, we'd have made
                 * multiple copies, wasting space; and what's worse, the
                 * copies belonging to already-deleted heap tuples would not
                 * be reclaimed by VACUUM.
                 */
                if (toastrel_valueid_exists(toastrel, toast_pointer.va_valueid)) {
                    /* Match, so short-circuit the data storage loop below */
                    data_todo = 0;
                }
            }
        }
        if (toast_pointer.va_valueid == InvalidOid) {
            /*
             * new value; must choose an OID that doesn't conflict in either
             * old or new toast table
             */
            do {
                toast_pointer.va_valueid = GetNewOidWithIndex(toastrel, RelationGetRelid(toastidx), (AttrNumber)1);
            } while (toastid_valueid_exists(rel->rd_toastoid, toast_pointer.va_valueid, bucketid));
        }
    }

    /*
     * Initialize constant parts of the tuple data
     */
    t_values[0] = ObjectIdGetDatum(toast_pointer.va_valueid);
    t_values[2] = PointerGetDatum(&chunk_data);
    t_isnull[0] = false;
    t_isnull[1] = false;
    t_isnull[2] = false;

    /*
     * Split up the item into chunks
     */
    while (data_todo > 0) {
        /*
         * Calculate the size of this chunk
         */
        chunk_size = Min(TOAST_MAX_CHUNK_SIZE, (uint32)data_todo);

        /*
         * Build a tuple and store it
         */
        t_values[1] = Int32GetDatum(chunk_seq++);
        SET_VARSIZE(&chunk_data, chunk_size + VARHDRSZ);
        rc = memcpy_s(VARDATA(&chunk_data), TOAST_MAX_CHUNK_SIZE, data_p, chunk_size);
        securec_check(rc, "", "");
        toasttup = heap_form_tuple(toast_tup_desc, t_values, t_isnull);

        (void)heap_insert(toastrel, toasttup, mycid, options, NULL, true);

        /*
         * Create the index entry.	We cheat a little here by not using
         * FormIndexDatum: this relies on the knowledge that the index columns
         * are the same as the initial columns of the table.
         *
         * Note also that there had better not be any user-created index on
         * the TOAST table, since we don't bother to update anything else.
         */
        (void)index_insert(toastidx, t_values, t_isnull, &(toasttup->t_self), toastrel,
                           toastidx->rd_index->indisunique ? UNIQUE_CHECK_YES : UNIQUE_CHECK_NO);

        /*
         * Free memory
         */
        heap_freetuple(toasttup);

        /*
         * Move on to next chunk
         */
        data_todo -= chunk_size;
        data_p += chunk_size;
    }

    /*
     * Done - close toast relation
     */
    index_close(toastidx, RowExclusiveLock);
    heap_close(toastrel, RowExclusiveLock);

    /*
     * Create the TOAST pointer value that we'll return
     */
    bool is_bucket_relation = RelationIsBucket(rel);
    Size result_size = TOAST_POINTER_SIZE;
    if (is_bucket_relation) {
        result_size += sizeof(int2);
    }

    result = (struct varlena *)palloc(result_size);

    if (is_bucket_relation) {
        SET_VARTAG_EXTERNAL(result, VARTAG_BUCKET);
    } else {
        SET_VARTAG_EXTERNAL(result, VARTAG_ONDISK);
    }

    rc = memcpy_s(VARDATA_EXTERNAL(result), TOAST_POINTER_SIZE, &toast_pointer, sizeof(toast_pointer));
    securec_check(rc, "", "");

    if (is_bucket_relation) {
        rc = memcpy_s((char *)result + TOAST_POINTER_SIZE, sizeof(int2), &bucketid, sizeof(int2));
        securec_check(rc, "", "");
    }

    return PointerGetDatum(result);
}

void toast_delete_datum_internal(varatt_external toast_pointer, int options, bool allow_update_self, int2 bucketid)
{
    Relation toastrel;
    Relation toastidx;
    ScanKeyData toastkey;
    SysScanDesc toastscan;
    HeapTuple toasttup;

    toastrel = heap_open(toast_pointer.va_toastrelid, RowExclusiveLock, bucketid);
    toastidx = index_open(toastrel->rd_rel->reltoastidxid, RowExclusiveLock, bucketid);

    /*
     * Setup a scan key to find chunks with matching va_valueid
     */
    ScanKeyInit(&toastkey, (AttrNumber)1, BTEqualStrategyNumber, F_OIDEQ, ObjectIdGetDatum(toast_pointer.va_valueid));

    /*
     * Find all the chunks.  (We don't actually care whether we see them in
     * sequence or not, but since we've already locked the index we might as
     * well use systable_beginscan_ordered.)
     */
    toastscan = systable_beginscan_ordered(toastrel, toastidx, SnapshotToast, 1, &toastkey);
    while ((toasttup = systable_getnext_ordered(toastscan, ForwardScanDirection)) != NULL) {
        /*
         * Have a chunk, delete it
         */
        simple_heap_delete(toastrel, &toasttup->t_self, options, allow_update_self);
    }

    /*
     * End scan and close relations
     */
    systable_endscan_ordered(toastscan);
    index_close(toastidx, RowExclusiveLock);
    heap_close(toastrel, RowExclusiveLock);
}

/* ----------
 * toast_delete_datum -
 *
 *	Delete a single external stored value.
 * ----------
 */
void toast_delete_datum(Relation rel, Datum value, int options, bool allow_update_self)
{
    struct varlena *attr = (struct varlena *)DatumGetPointer(value);
    struct varatt_external toast_pointer;
    int2 bucketid;
    if (!VARATT_IS_EXTERNAL_ONDISK_B(attr))
        return;
    /* Must copy to access aligned fields */
    VARATT_EXTERNAL_GET_POINTER_B(toast_pointer, attr, bucketid);

    /*
     * Open the toast relation and its index
     */
    Assert(bucketid == InvalidBktId || (bucketid == rel->rd_node.bucketNode));
    toast_delete_datum_internal(toast_pointer, options, allow_update_self, bucketid);
}

void toast_huge_delete_datum(Relation rel, Datum value, int options, bool allow_update_self)
{
    struct varlena *attr = (struct varlena *)DatumGetPointer(value);
    ScanKeyData toastkey;
    SysScanDesc toastscan;
    HeapTuple ttup;

    Pointer chunk = NULL;
    bool isnull = false;
    struct varatt_external toast_pointer;
    int2 bucketid;
    struct varatt_lob_external large_toast_pointer;
    VARATT_EXTERNAL_GET_HUGE_POINTER(large_toast_pointer, attr);

    Relation toastrel = heap_open(large_toast_pointer.va_toastrelid, RowExclusiveLock);
    TupleDesc toast_tup_desc = toastrel->rd_att;
    Relation toastidx = index_open(toastrel->rd_rel->reltoastidxid, RowExclusiveLock);
    ScanKeyInit(&toastkey, (AttrNumber)1, BTEqualStrategyNumber, F_OIDEQ,
        ObjectIdGetDatum(large_toast_pointer.va_valueid));
    toastscan = systable_beginscan_ordered(toastrel, toastidx, SnapshotToast, 1, &toastkey);
    while ((ttup = systable_getnext_ordered(toastscan, ForwardScanDirection)) != NULL) {
        chunk = DatumGetPointer(fastgetattr(ttup, CHUNK_DATA_ATTR, toast_tup_desc, &isnull));
        VARATT_EXTERNAL_GET_POINTER_B(toast_pointer, chunk, bucketid);
        toast_delete_datum(rel, PointerGetDatum(chunk), options, allow_update_self);
        simple_heap_delete(toastrel, &ttup->t_self, options, allow_update_self);
    }
    systable_endscan_ordered(toastscan);
    index_close(toastidx, RowExclusiveLock);
    heap_close(toastrel, RowExclusiveLock);
}

/* ----------
 * toastrel_valueid_exists -
 *
 *	Test whether a toast value with the given ID exists in the toast relation.
 *  For safety, we consider a value to exist if there are either live or dead
 *  toast rows with that ID; see notes for GetNewOid().
 * ----------
 */
bool toastrel_valueid_exists(Relation toastrel, Oid valueid)
{
    bool result = false;
    ScanKeyData toastkey;
    SysScanDesc toastscan;

    /*
     * Setup a scan key to find chunks with matching va_valueid
     */
    ScanKeyInit(&toastkey, (AttrNumber)1, BTEqualStrategyNumber, F_OIDEQ, ObjectIdGetDatum(valueid));

    /*
     * Is there any such chunk?
     */
    toastscan = systable_beginscan(toastrel, toastrel->rd_rel->reltoastidxid, true, SnapshotAny, 1, &toastkey);
    if (systable_getnext(toastscan) != NULL)
        result = true;

    systable_endscan(toastscan);

    return result;
}

/* ----------
 * toastid_valueid_exists -
 *
 *	As above, but work from toast rel's OID not an open relation
 * ----------
 */
static bool toastid_valueid_exists(Oid toastrelid, Oid valueid, int2 bucketid)
{
    bool result = false;
    Relation toastrel;

    toastrel = heap_open(toastrelid, AccessShareLock, bucketid);

    result = toastrel_valueid_exists(toastrel, valueid);

    heap_close(toastrel, AccessShareLock);

    return result;
}

/* ----------
 * toast_fetch_datum -
 *
 *	Reconstruct an in memory Datum from the chunks saved
 *	in the toast relation
 * ----------
 */
struct varlena* heap_internal_toast_fetch_datum(struct varatt_external toast_pointer,
    Relation toastrel, Relation toastidx)
{
    ScanKeyData toastkey;
    SysScanDesc toastscan;
    HeapTuple ttup;
    TupleDesc toast_tup_desc;
    struct varlena *result = NULL;
    int32 ressize;
    int32 residx, nextidx;
    int32 numchunks;
    Pointer chunk = NULL;
    bool isnull = false;
    char *chunkdata = NULL;
    int32 chunksize;

    ressize = toast_pointer.va_extsize;
    numchunks = ((ressize - 1) / TOAST_MAX_CHUNK_SIZE) + 1;

    result = (struct varlena *)palloc(ressize + VARHDRSZ);

    if (VARATT_EXTERNAL_IS_COMPRESSED(toast_pointer))
        SET_VARSIZE_COMPRESSED(result, ressize + VARHDRSZ);
    else
        SET_VARSIZE(result, ressize + VARHDRSZ);

    /*
     * Open the toast relation and its index
     */
    toast_tup_desc = toastrel->rd_att;

    /*
     * Setup a scan key to fetch from the index by va_valueid
     */
    ScanKeyInit(&toastkey, (AttrNumber)1, BTEqualStrategyNumber, F_OIDEQ, ObjectIdGetDatum(toast_pointer.va_valueid));

    /*
     * Read the chunks by index
     *
     * Note that because the index is actually on (valueid, chunkidx) we will
     * see the chunks in chunkidx order, even though we didn't explicitly ask
     * for it.
     */
    nextidx = 0;

    toastscan = systable_beginscan_ordered(toastrel, toastidx, get_toast_snapshot(), 1, &toastkey);
    while ((ttup = systable_getnext_ordered(toastscan, ForwardScanDirection)) != NULL) {
        /*
         * Have a chunk, extract the sequence number and the data
         */
        residx = DatumGetInt32(fastgetattr(ttup, CHUNK_ID_ATTR, toast_tup_desc, &isnull));
        Assert(!isnull);
        chunk = DatumGetPointer(fastgetattr(ttup, CHUNK_DATA_ATTR, toast_tup_desc, &isnull));
        Assert(!isnull);
        if (!VARATT_IS_EXTENDED(chunk)) {
            chunksize = VARSIZE(chunk) - VARHDRSZ;
            chunkdata = VARDATA(chunk);
        } else if (VARATT_IS_SHORT(chunk)) {
            /* could happen due to heap_form_tuple doing its thing */
            chunksize = VARSIZE_SHORT(chunk) - VARHDRSZ_SHORT;
            chunkdata = VARDATA_SHORT(chunk);
        } else {
            /* should never happen */
            Assert(0);
            ereport(ERROR, (errcode(ERRCODE_UNEXPECTED_CHUNK_VALUE),
                            errmsg("found toasted toast chunk for toast value %u in %s", toast_pointer.va_valueid,
                                   RelationGetRelationName(toastrel))));
            chunksize = 0; /* keep compiler quiet */
            chunkdata = NULL;
        }

        /*
         * Some checks on the data we've found
         */
        if (residx != nextidx) {
            Assert(0);
            ereport(ERROR, (errcode(ERRCODE_UNEXPECTED_CHUNK_VALUE),
                            errmsg("unexpected chunk number %d (expected %d) for toast value %u in %s", residx, nextidx,
                                   toast_pointer.va_valueid, RelationGetRelationName(toastrel))));
        }
        if (residx < numchunks - 1) {
            if (chunksize != TOAST_MAX_CHUNK_SIZE) {
                Assert(0);
                ereport(ERROR,
                        (errcode(ERRCODE_UNEXPECTED_CHUNK_VALUE),
                         errmsg("unexpected chunk size %d (expected %d) in chunk %d of %d for toast value %u in %s",
                                chunksize, (int)TOAST_MAX_CHUNK_SIZE, residx, numchunks, toast_pointer.va_valueid,
                                RelationGetRelationName(toastrel))));
            }
        } else if (residx == numchunks - 1) {
            if ((residx * TOAST_MAX_CHUNK_SIZE + chunksize) != (uint32)ressize) {
                Assert(0);
                ereport(ERROR,
                        (errcode(ERRCODE_UNEXPECTED_CHUNK_VALUE),
                         errmsg("unexpected chunk size %d (expected %d) in final chunk %d for toast value %u in %s",
                                chunksize, (int)(ressize - residx * TOAST_MAX_CHUNK_SIZE), residx,
                                toast_pointer.va_valueid, RelationGetRelationName(toastrel))));
            }
        } else {
            Assert(0);
            ereport(ERROR, (errcode(ERRCODE_UNEXPECTED_CHUNK_VALUE),
                            errmsg("unexpected chunk number %d (out of range %d..%d) for toast value %u in %s", residx,
                                   0, numchunks - 1, toast_pointer.va_valueid, RelationGetRelationName(toastrel))));
        }
        /*
         * Copy the data into proper place in our result
         */
        errno_t rc = memcpy_s(VARDATA(result) + residx * TOAST_MAX_CHUNK_SIZE,
                              ressize - residx * TOAST_MAX_CHUNK_SIZE, chunkdata, chunksize);
        securec_check(rc, "", "");

        nextidx++;
    }

    /*
     * Final checks that we successfully fetched the datum
     */
    if (nextidx != numchunks) {
        ereport(ERROR,
            (errcode(ERRCODE_UNEXPECTED_CHUNK_VALUE),
                errmsg("missing chunk number %d for toast value %u in %s",
                    nextidx,
                    toast_pointer.va_valueid,
                    RelationGetRelationName(toastrel))));
    }

    /*
     * End scan and close relations
     */
    systable_endscan_ordered(toastscan);
    return result;
}

struct varlena* HeapInternalToastFetchDatumSlice(struct varatt_external toastPointer,
    Relation toastrel, Relation toastidx, int64 sliceoffset, int32 length)
{
    int32 attrsize;
    int32 residx;
    int32 nextidx;
    int numchunks;
    int startchunk;
    int endchunk;
    int32 startoffset;
    int32 endoffset;
    int totalchunks;
    Pointer chunk = NULL;
    bool isnull = false;
    char* chunkdata = NULL;
    int32 chunksize;
    int32 chcpystrt;
    int32 chcpyend;
    errno_t rc = EOK;
    ScanKeyData toastkey[3];
    int nscankeys;
    SysScanDesc toastscan;
    HeapTuple ttup;
    TupleDesc toast_tup_desc;
    struct varlena* result = NULL;
 
   /*
     * It's nonsense to fetch slices of a compressed datum -- this isn't lo_*
     * we can't return a compressed datum which is meaningful to toast later
   */
    Assert(!VARATT_EXTERNAL_IS_COMPRESSED(toastPointer));

    attrsize = toastPointer.va_extsize;
    totalchunks = ((attrsize - 1) / TOAST_MAX_CHUNK_SIZE) + 1;

    if (sliceoffset >= attrsize) {
        sliceoffset = 0;
        length = 0;
    }

    if (((sliceoffset + length) > attrsize) || length < 0)
        length = attrsize - sliceoffset;

    result = (struct varlena*)palloc(length + VARHDRSZ);

    if (VARATT_EXTERNAL_IS_COMPRESSED(toastPointer))
        SET_VARSIZE_COMPRESSED(result, length + VARHDRSZ);
    else
        SET_VARSIZE(result, length + VARHDRSZ);

    if (length == 0)
        return result; /* Can save a lot of work at this point! */

    startchunk = sliceoffset / TOAST_MAX_CHUNK_SIZE;
    endchunk = (sliceoffset + length - 1) / TOAST_MAX_CHUNK_SIZE;
    numchunks = (endchunk - startchunk) + 1;

    startoffset = sliceoffset % TOAST_MAX_CHUNK_SIZE;
    endoffset = (sliceoffset + length - 1) % TOAST_MAX_CHUNK_SIZE;

    toast_tup_desc = toastrel->rd_att;
    /*
     * Setup a scan key to fetch from the index. This is either two keys or
     * three depending on the number of chunks.
     */
    ScanKeyInit(
        &toastkey[0], (AttrNumber)ATTR_FIRST, 
        BTEqualStrategyNumber, F_OIDEQ, ObjectIdGetDatum(toastPointer.va_valueid));

    /*
     * Use equality condition for one chunk, a range condition otherwise:
     */
    if (numchunks == 1) {
        ScanKeyInit(&toastkey[1], (AttrNumber)ATTR_SECOND, 
                    BTEqualStrategyNumber, F_INT4EQ, Int32GetDatum(startchunk));
        
        nscankeys = ATTR_SECOND;
    } else {
        ScanKeyInit(&toastkey[1], (AttrNumber)ATTR_SECOND, 
                    BTGreaterEqualStrategyNumber, F_INT4GE, Int32GetDatum(startchunk));
        
        ScanKeyInit(&toastkey[2], (AttrNumber)ATTR_SECOND, 
                    BTLessEqualStrategyNumber, F_INT4LE, Int32GetDatum(endchunk));
        nscankeys = ATTR_THIRD;
    }

    /*
     * Read the chunks by index
     * The index is on (valueid, chunkidx) so they will come in order
     */
    nextidx = startchunk;
    toastscan = systable_beginscan_ordered(toastrel, toastidx, get_toast_snapshot(), nscankeys, toastkey);
    while ((ttup = systable_getnext_ordered(toastscan, ForwardScanDirection)) != NULL) {
        /*
         * Have a chunk, extract the sequence number and the data
         */
        residx = DatumGetInt32(fastgetattr(ttup, 2, toast_tup_desc, &isnull));
        Assert(!isnull);
        chunk = DatumGetPointer(fastgetattr(ttup, 3, toast_tup_desc, &isnull));
        Assert(!isnull);
        if (!VARATT_IS_EXTENDED(chunk)) {
            chunksize = VARSIZE(chunk) - VARHDRSZ;
            chunkdata = VARDATA(chunk);
        } else if (VARATT_IS_SHORT(chunk)) {
            /* could happen due to heap_form_tuple doing its thing */
            chunksize = VARSIZE_SHORT(chunk) - VARHDRSZ_SHORT;
            chunkdata = VARDATA_SHORT(chunk);
        } else {
            /* should never happen */
            Assert(0);
            ereport(ERROR,
                (errcode(ERRCODE_UNEXPECTED_CHUNK_VALUE),
                    errmsg("found toasted toast chunk for toast value %u in %s",
                        toastPointer.va_valueid,
                        RelationGetRelationName(toastrel))));
            chunksize = 0; /* keep compiler quiet */
            chunkdata = NULL;
        }

        /*
         * Some checks on the data we've found
         */
        if ((residx != nextidx) || (residx > endchunk) || (residx < startchunk)) {
            Assert(0);
            ereport(ERROR,
                (errcode(ERRCODE_UNEXPECTED_CHUNK_VALUE),
                    errmsg("unexpected chunk number %d (expected %d) for toast value %u in %s",
                        residx,
                        nextidx,
                        toastPointer.va_valueid,
                        RelationGetRelationName(toastrel))));
        }
        if (residx < totalchunks - 1) {
            if (chunksize != TOAST_MAX_CHUNK_SIZE) {
                Assert(0);
                ereport(ERROR,
                    (errcode(ERRCODE_UNEXPECTED_CHUNK_VALUE),
                        errmsg("unexpected chunk size %d (expected %d) in chunk %d of %d for toast value %u in %s when "
                               "fetching slice",
                            chunksize,
                            (int)TOAST_MAX_CHUNK_SIZE,
                            residx,
                            totalchunks,
                            toastPointer.va_valueid,
                            RelationGetRelationName(toastrel))));
            }
        } else if (residx == totalchunks - 1) {
            if ((residx * TOAST_MAX_CHUNK_SIZE + chunksize) != (uint32)attrsize) {
                Assert(0);
                ereport(ERROR,
                    (errcode(ERRCODE_UNEXPECTED_CHUNK_VALUE),
                        errmsg("unexpected chunk size %d (expected %d) in final chunk %d for toast value %u in %s when "
                               "fetching slice",
                            chunksize,
                            (int)(attrsize - residx * TOAST_MAX_CHUNK_SIZE),
                            residx,
                            toastPointer.va_valueid,
                            RelationGetRelationName(toastrel))));
            }
        } else {
            Assert(0);
            ereport(ERROR,
                (errcode(ERRCODE_UNEXPECTED_CHUNK_VALUE),
                    errmsg("unexpected chunk number %d (out of range %d..%d) for toast value %u in %s",
                        residx,
                        0,
                        totalchunks - 1,
                        toastPointer.va_valueid,
                        RelationGetRelationName(toastrel))));
        }
        /*
         * Copy the data into proper place in our result
         */
        chcpystrt = 0;
        chcpyend = chunksize - 1;
        if (residx == startchunk)
            chcpystrt = startoffset;
        if (residx == endchunk)
            chcpyend = endoffset;

        rc = memcpy_s(VARDATA(result) + (residx * TOAST_MAX_CHUNK_SIZE - sliceoffset) + chcpystrt,
            length + VARHDRSZ - (residx * TOAST_MAX_CHUNK_SIZE - sliceoffset + chcpystrt),
            chunkdata + chcpystrt,
            (chcpyend - chcpystrt) + 1);
        securec_check(rc, "", "");
        nextidx++;
    }

    /*
     * Final checks that we successfully fetched the datum
     */
    if (nextidx != (endchunk + 1)) {
        Assert(0);
        ereport(ERROR,
            (errcode(ERRCODE_UNEXPECTED_CHUNK_VALUE),
                errmsg("missing chunk number %d for toast value %u in %s",
                    nextidx,
                    toastPointer.va_valueid,
                    RelationGetRelationName(toastrel))));
    }
    systable_endscan_ordered(toastscan);
    return result;
}

/* ----------
 * toast_fetch_datum -
 *
 *  Reconstruct an in memory Datum from the chunks saved
 *  in the toast relation
 * ----------
 */
struct varlena* toast_fetch_datum(struct varlena* attr)
{
    Relation toastrel;
    Relation toastidx;
    struct varlena* result = NULL;
    struct varatt_external toast_pointer;
    int2 bucketid;
    if (VARATT_IS_EXTERNAL_INDIRECT(attr))
        ereport(ERROR, (errcode(ERRCODE_FETCH_DATA_FAILED), errmsg("shouldn't be called for indirect tuples")));
    /* Must copy to access aligned fields */
    VARATT_EXTERNAL_GET_POINTER_B(toast_pointer, attr, bucketid);

    /*
     * Open the toast relation and its index
     */
    toastrel = heap_open(toast_pointer.va_toastrelid, AccessShareLock, bucketid);
    toastidx = index_open(toastrel->rd_rel->reltoastidxid, AccessShareLock, bucketid);
    
    if (RelationIsUstoreFormat(toastrel)) {
        result = UHeapInternalToastFetchDatum(toast_pointer, toastrel, toastidx);
    } else {
        result = heap_internal_toast_fetch_datum(toast_pointer, toastrel, toastidx);
    }

    index_close(toastidx, AccessShareLock);
    heap_close(toastrel, AccessShareLock);

    return result;
}

/* ----------
 * toast_fetch_datum_slice -
 *
 *	Reconstruct a segment of a Datum from the chunks saved
 *	in the toast relation
 * ----------
 */
static struct varlena* toast_fetch_datum_slice(struct varlena* attr, int64 sliceoffset, int32 length)
{
    Relation toastrel;
    Relation toastidx;
    struct varlena* result = NULL;
    struct varatt_external toastPointer;

    int2 bucketid;
    Assert(VARATT_IS_EXTERNAL_ONDISK_B(attr));

    /* Must copy to access aligned fields */
    VARATT_EXTERNAL_GET_POINTER_B(toastPointer, attr, bucketid);

    toastrel = heap_open(toastPointer.va_toastrelid, AccessShareLock, bucketid);
    toastidx = index_open(toastrel->rd_rel->reltoastidxid, AccessShareLock, bucketid);
    if (RelationIsUstoreFormat(toastrel)) {
        result = UHeapInternalToastFetchDatumSlice(toastPointer, toastrel, toastidx, sliceoffset, length);
    } else {
        result = HeapInternalToastFetchDatumSlice(toastPointer, toastrel, toastidx, sliceoffset, length);
    }
    index_close(toastidx, AccessShareLock);
    heap_close(toastrel, AccessShareLock);

    return result;
}

/* ----------
 * toast_huge_fetch_datum_slice -
 *
 * return a Datum from the chunks saved in the toast relation
 *    attr: large_toast_pointer in user table
 *    sliceoffset: byte offset, from 0
 *    length: byte length
 *
 * ----------
 */
static struct varlena *toast_huge_fetch_datum_slice(struct varlena *attr, int64 sliceoffset, int32 length)
{
    ScanKeyData toastkey;
    SysScanDesc toastscan;
    HeapTuple ttup;
    errno_t rc;
    struct varlena *result = NULL;
    struct varlena *first_chunk = NULL;
    int32 curlength;
    int32 totallength = 0;
    Pointer chunk;
    bool isnull;
    struct varatt_lob_external large_toast_pointer;
    VARATT_EXTERNAL_GET_HUGE_POINTER(large_toast_pointer, attr);

    result = (struct varlena *)palloc(length + VARHDRSZ);
    SET_VARSIZE(result, length + VARHDRSZ);

    Relation toastrel = heap_open(large_toast_pointer.va_toastrelid, AccessShareLock);
    TupleDesc toast_tup_desc = toastrel->rd_att;
    Relation toastidx = index_open(toastrel->rd_rel->reltoastidxid, AccessShareLock);
    ScanKeyInit(&toastkey, (AttrNumber)1, BTEqualStrategyNumber, F_OIDEQ,
        ObjectIdGetDatum(large_toast_pointer.va_valueid));
    toastscan = systable_beginscan_ordered(toastrel, toastidx, get_toast_snapshot(), 1, &toastkey);
    while ((ttup = systable_getnext_ordered(toastscan, ForwardScanDirection)) != NULL) {
        chunk = DatumGetPointer(fastgetattr(ttup, CHUNK_DATA_ATTR, toast_tup_desc, &isnull));
        struct varatt_external toast_pointer;
        VARATT_EXTERNAL_GET_POINTER(toast_pointer, chunk);

        if (sliceoffset > toast_pointer.va_rawsize - VARHDRSZ) {
            sliceoffset -= (toast_pointer.va_rawsize - VARHDRSZ);
            continue;
        } else {
            if (length <= ((toast_pointer.va_rawsize - VARHDRSZ) - sliceoffset)) {
                curlength = length;
            } else {
                curlength = (toast_pointer.va_rawsize - VARHDRSZ) - sliceoffset;
            }
            first_chunk = heap_tuple_untoast_attr_slice((varlena *)chunk, sliceoffset, curlength);
            rc = memcpy_s(VARDATA(result) + totallength, length, VARDATA(first_chunk), curlength);
            securec_check(rc, "", "");
            length -= curlength;
            totallength += curlength;
            sliceoffset = 0;
        }
        if (length == 0) {
            break;
        }
    }
    systable_endscan_ordered(toastscan);
    index_close(toastidx, AccessShareLock);
    heap_close(toastrel, AccessShareLock);

    return result;
}

/* ----------
 * toast_huge_write_datum_slice -
 *
 * return a Datum from the chunks saved in the toast relation
 *    attr1: large_toast_pointer in user table
 *    attr2: less than 1G data
 *    sliceoffset: byte offset, from 0
 *    length: byte length
 *
 * ----------
 */
struct varlena *toast_huge_write_datum_slice(struct varlena *attr1, struct varlena *attr2, int64 sliceoffset,
    int32 length)
{
    ScanKeyData toastkey;
    HeapTuple ttup;
    errno_t rc;
    struct varlena *first_chunk = NULL;
    int32 curlength;
    int32 totallength = 0;
    int32 residx;
    Pointer chunk;
    bool isnull;
    Assert(length >= 1 && length <= MAX_TOAST_CHUNK_SIZE);
    struct varatt_lob_external large_toast_pointer;
    VARATT_EXTERNAL_GET_HUGE_POINTER(large_toast_pointer, attr1);
    Oid toastOid = InvalidOid;
    create_toast_by_sid(&toastOid);
    Assert(OidIsValid(toastOid));

    if (sliceoffset < 0 || sliceoffset > large_toast_pointer.va_rawsize) {
        ereport(ERROR, (errcode(ERRCODE_UNEXPECTED_CHUNK_VALUE), errmsg("offset(%lu) is invalid.", sliceoffset)));
    }

    Relation srctoastrel = heap_open(large_toast_pointer.va_toastrelid, AccessShareLock);
    TupleDesc toast_tup_desc = srctoastrel->rd_att;
    Relation srctoastidx = index_open(srctoastrel->rd_rel->reltoastidxid, AccessShareLock);
    Relation destoastrel = heap_open(toastOid, RowExclusiveLock);
    Relation destoastidx = index_open(destoastrel->rd_rel->reltoastidxid, RowExclusiveLock);
    Oid firstchunkid = GetNewOidWithIndex(destoastrel, RelationGetRelid(destoastidx), (AttrNumber)1);
    ScanKeyInit(&toastkey, (AttrNumber)1, BTEqualStrategyNumber, F_OIDEQ,
        ObjectIdGetDatum(large_toast_pointer.va_valueid));
    SysScanDesc toastscan = systable_beginscan_ordered(srctoastrel, srctoastidx, SnapshotToast, 1, &toastkey);
    while ((ttup = systable_getnext_ordered(toastscan, ForwardScanDirection)) != NULL) {
        residx = DatumGetInt32(fastgetattr(ttup, CHUNK_ID_ATTR, toast_tup_desc, &isnull));
        chunk = DatumGetPointer(fastgetattr(ttup, CHUNK_DATA_ATTR, toast_tup_desc, &isnull));
        struct varatt_external toast_pointer;
        VARATT_EXTERNAL_GET_POINTER(toast_pointer, chunk);

        if (sliceoffset > toast_pointer.va_rawsize - VARHDRSZ) {
            toast_huge_fetch_and_copy_level1(srctoastrel, srctoastidx, destoastrel, destoastidx, ttup, firstchunkid,
                &residx, InvalidOid);
            sliceoffset -= (toast_pointer.va_rawsize - VARHDRSZ);
            continue;
        }
        if (length == 0) {
            toast_huge_fetch_and_copy_level1(srctoastrel, srctoastidx, destoastrel, destoastidx, ttup, firstchunkid,
                &residx, InvalidOid);
            continue;
        }
        first_chunk = heap_tuple_untoast_attr((varlena *)chunk);
        if (length <= (toast_pointer.va_rawsize - VARHDRSZ - sliceoffset)) {
            curlength = length;
        } else {
            curlength = toast_pointer.va_rawsize - VARHDRSZ - sliceoffset;
        }
        rc = memcpy_s(VARDATA(first_chunk) + sliceoffset, curlength, VARDATA(attr2) + totallength, curlength);
        securec_check(rc, "", "");
        length -= curlength;
        totallength += curlength;
        sliceoffset = 0;
        toast_huge_fetch_and_append_datum(destoastrel, destoastidx, first_chunk, &firstchunkid, residx);
    }
    systable_endscan_ordered(toastscan);
    index_close(srctoastidx, AccessShareLock);
    heap_close(srctoastrel, AccessShareLock);
    index_close(destoastidx, RowExclusiveLock);
    heap_close(destoastrel, RowExclusiveLock);

    struct varlena *result = (struct varlena *)palloc(LARGE_TOAST_POINTER_SIZE);
    SET_HUGE_TOAST_POINTER_TAG(result, VARTAG_ONDISK);
    large_toast_pointer.va_valueid = firstchunkid;
    large_toast_pointer.va_toastrelid = toastOid;
    rc =
        memcpy_s(VARDATA_EXTERNAL(result), LARGE_TOAST_POINTER_SIZE, &large_toast_pointer, sizeof(large_toast_pointer));
    securec_check(rc, "", "");

    return result;
}

void checkHugeToastPointer(struct varlena *value)
{
    if (VARATT_IS_HUGE_TOAST_POINTER(value)) {
        ereport(ERROR,
            (errcode(ERRCODE_FEATURE_NOT_SUPPORTED), errmsg("not support feature for more than 1G clob/blob")));
    }
}

struct varlena * toast_pointer_fetch_data(TupleTableSlot* varSlot, Form_pg_attribute attr, int varNumber)
{
    MemoryContext oldcontext;
    oldcontext = MemoryContextSwitchTo(varSlot->tts_mcxt);

    struct varlena *toast_pointer_lob = (struct varlena *)palloc(LOB_POINTER_SIZE);
    HeapTuple tuple = (HeapTuple)(varSlot->tts_tuple);
    Assert(toast_pointer_lob != NULL);
    if (tuple != NULL) {
        struct varatt_lob_pointer lob_pointer;
        SET_VARTAG_1B_E(toast_pointer_lob, VARTAG_LOB);

        lob_pointer.relid = tuple->t_tableOid;
        lob_pointer.columid = attr->attnum;
        lob_pointer.bucketid = tuple->t_bucketId;
        lob_pointer.bi_hi = tuple->t_self.ip_blkid.bi_hi;
        lob_pointer.bi_lo = tuple->t_self.ip_blkid.bi_lo;
        lob_pointer.ip_posid = tuple->t_self.ip_posid;

        errno_t rc = memcpy_s(VARDATA_EXTERNAL(toast_pointer_lob), LOB_POINTER_SIZE, &lob_pointer,
            sizeof(varatt_lob_pointer));
        securec_check(rc, "", "");
    } else {
        toast_pointer_lob = (varlena *)DatumGetPointer(varSlot->tts_lobPointers[varNumber]);
    }
    MemoryContextSwitchTo(oldcontext);

    return toast_pointer_lob;
}

HeapTuple ctid_get_tuple(Relation relation, ItemPointer tid)
{
    Buffer user_buf = InvalidBuffer;
    HeapTuple tuple = NULL;
    HeapTuple new_tuple = NULL;
    TM_Result result;
  
    /* alloc memory for old tuple and set tuple id */
    tuple = (HeapTupleData *)heaptup_alloc(BLCKSZ);
    tuple->t_data = (HeapTupleHeader)((char *)tuple + HEAPTUPLESIZE);
    Assert(tid != NULL);
    tuple->t_self = *tid;
    
    if (tableam_tuple_fetch(relation, SnapshotAny, tuple, &user_buf, false, NULL)) {
        result = HeapTupleSatisfiesUpdate(tuple, GetCurrentCommandId(true), user_buf, false);
        if (result != TM_Ok) {
            ereport(ERROR, (errcode(ERRCODE_SYSTEM_ERROR), errmsg("The tuple is updated, please use 'for update'.")));
        }
        
        new_tuple = heapCopyTuple((HeapTuple)tuple, relation->rd_att, NULL);
        ReleaseBuffer(user_buf);
    } else {
        heap_close(relation, NoLock);
        ereport(ERROR, (errcode(ERRCODE_SYSTEM_ERROR), errmsg("The tuple is not found")));
    }
    heap_freetuple(tuple);

    return new_tuple;
}
