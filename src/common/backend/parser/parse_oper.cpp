/* -------------------------------------------------------------------------
 *
 * parse_oper.cpp
 *		handle operator things for parser
 *
 * Portions Copyright (c) 1996-2012, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *	  src/common/backend/parser/parse_oper.cpp
 *
 * -------------------------------------------------------------------------
 */

#include "postgres.h"
#include "knl/knl_variable.h"

#include "catalog/pg_operator.h"
#include "catalog/pg_type.h"
#include "lib/stringinfo.h"
#include "nodes/nodeFuncs.h"
#include "parser/parse_coerce.h"
#include "parser/parse_func.h"
#include "parser/parse_oper.h"
#include "parser/parse_type.h"
#include "utils/builtins.h"
#include "utils/inval.h"
#include "utils/lsyscache.h"
#include "utils/syscache.h"
#include "utils/typcache.h"

/*
 * The lookup key for the operator lookaside hash table.  Unused bits must be
 * zeroes to ensure hashing works consistently --- in particular, oprname
 * must be zero-padded and any unused entries in search_path must be zero.
 *
 * search_path contains the actual search_path with which the entry was
 * derived (minus temp namespace if any), or else the single specified
 * schema OID if we are looking up an explicitly-qualified operator name.
 *
 * search_path has to be fixed-length since the hashtable code insists on
 * fixed-size keys.  If your search path is longer than that, we just punt
 * and don't cache anything.
 */

/* If your search_path is longer than this, sucks to be you ... */
#define MAX_CACHED_PATH_LEN 16

typedef struct OprCacheKey {
    char oprname[NAMEDATALEN];
    Oid left_arg;  /* Left input OID, or 0 if prefix op */
    Oid right_arg; /* Right input OID, or 0 if postfix op */
    Oid search_path[MAX_CACHED_PATH_LEN];
    bool use_a_style_coercion;
} OprCacheKey;

typedef struct OprCacheEntry {
    /* the hash lookup key MUST BE FIRST */
    OprCacheKey key;

    Oid opr_oid; /* OID of the resolved operator */
} OprCacheEntry;

static Oid binary_oper_exact(List* opname, Oid arg1, Oid arg2, bool use_a_style_coercion);
static FuncDetailCode oper_select_candidate(int nargs, Oid* input_typeids, FuncCandidateList candidates, Oid* operOid);
static const char* op_signature_string(List* op, char oprkind, Oid arg1, Oid arg2);
static void op_error(
    ParseState* pstate, List* op, char oprkind, Oid arg1, Oid arg2, FuncDetailCode fdresult, int location);
static bool make_oper_cache_key(
    OprCacheKey* key, List* opname, Oid ltypeId, Oid rtypeId, bool use_a_style_coercion = false);
static Oid find_oper_cache_entry(OprCacheKey* key);
static void make_oper_cache_entry(OprCacheKey* key, Oid opr_oid);

/*
 * LookupOperName
 *		Given a possibly-qualified operator name and exact input datatypes,
 *		look up the operator.
 *
 * Pass oprleft = InvalidOid for a prefix op, oprright = InvalidOid for
 * a postfix op.
 *
 * If the operator name is not schema-qualified, it is sought in the current
 * namespace search path.
 *
 * If the operator is not found, we return InvalidOid if noError is true,
 * else raise an error.  pstate and location are used only to report the
 * error position; pass NULL/-1 if not available.
 */
Oid LookupOperName(ParseState* pstate, List* opername, Oid oprleft, Oid oprright, bool noError, int location)
{
    Oid result;

    result = OpernameGetOprid(opername, oprleft, oprright);
    if (OidIsValid(result))
        return result;

    /* we don't use op_error here because only an exact match is wanted */
    if (!noError) {
        char oprkind;

        if (!OidIsValid(oprleft))
            oprkind = 'l';
        else if (!OidIsValid(oprright))
            oprkind = 'r';
        else
            oprkind = 'b';

        ereport(ERROR,
            (errcode(ERRCODE_UNDEFINED_FUNCTION),
                errmsg("operator does not exist: %s", op_signature_string(opername, oprkind, oprleft, oprright)),
                parser_errposition(pstate, location)));
    }

    return InvalidOid;
}

/*
 * LookupOperNameTypeNames
 *		Like LookupOperName, but the argument types are specified by
 *		TypeName nodes.
 *
 * Pass oprleft = NULL for a prefix op, oprright = NULL for a postfix op.
 */
Oid LookupOperNameTypeNames(
    ParseState* pstate, List* opername, TypeName* oprleft, TypeName* oprright, bool noError, int location)
{
    Oid leftoid, rightoid;

    if (oprleft == NULL)
        leftoid = InvalidOid;
    else
        leftoid = typenameTypeId(pstate, oprleft);

    if (oprright == NULL)
        rightoid = InvalidOid;
    else
        rightoid = typenameTypeId(pstate, oprright);

    return LookupOperName(pstate, opername, leftoid, rightoid, noError, location);
}

/*
 * get_sort_group_operators - get default sorting/grouping operators for type
 *
 * We fetch the "<", "=", and ">" operators all at once to reduce lookup
 * overhead (knowing that most callers will be interested in at least two).
 * However, a given datatype might have only an "=" operator, if it is
 * hashable but not sortable.  (Other combinations of present and missing
 * operators shouldn't happen, unless the system catalogs are messed up.)
 *
 * If an operator is missing and the corresponding needXX flag is true,
 * throw a standard error message, else return InvalidOid.
 *
 * In addition to the operator OIDs themselves, this function can identify
 * whether the "=" operator is hashable.
 *
 * Callers can pass NULL pointers for any results they don't care to get.
 *
 * Note: the results are guaranteed to be exact or binary-compatible matches,
 * since most callers are not prepared to cope with adding any run-time type
 * coercion steps.
 */
void get_sort_group_operators(
    Oid argtype, bool needLT, bool needEQ, bool needGT, Oid* ltOpr, Oid* eqOpr, Oid* gtOpr, bool* isHashable)
{
    TypeCacheEntry* typentry = NULL;
    int cache_flags;
    Oid lt_opr;
    Oid eq_opr;
    Oid gt_opr;
    bool hashable = false;

    /*
     * Look up the operators using the type cache.
     *
     * Note: the search algorithm used by typcache.c ensures that the results
     * are consistent, ie all from matching opclasses.
     */
    if (isHashable != NULL)
        cache_flags = TYPECACHE_LT_OPR | TYPECACHE_EQ_OPR | TYPECACHE_GT_OPR | TYPECACHE_HASH_PROC;
    else
        cache_flags = TYPECACHE_LT_OPR | TYPECACHE_EQ_OPR | TYPECACHE_GT_OPR;

    typentry = lookup_type_cache(argtype, cache_flags);
    lt_opr = typentry->lt_opr;
    eq_opr = typentry->eq_opr;
    gt_opr = typentry->gt_opr;
    hashable = OidIsValid(typentry->hash_proc);

    /* Report errors if needed */
    if ((needLT && !OidIsValid(lt_opr)) || (needGT && !OidIsValid(gt_opr)))
        ereport(ERROR,
            (errcode(ERRCODE_UNDEFINED_FUNCTION),
                errmsg("could not identify an ordering operator for type %s", format_type_be(argtype)),
                errhint("Use an explicit ordering operator or modify the query.")));
    if (needEQ && !OidIsValid(eq_opr))
        ereport(ERROR,
            (errcode(ERRCODE_UNDEFINED_FUNCTION),
                errmsg("could not identify an equality operator for type %s", format_type_be(argtype))));

    /* Return results as needed */
    if (ltOpr != NULL)
        *ltOpr = lt_opr;
    if (eqOpr != NULL)
        *eqOpr = eq_opr;
    if (gtOpr != NULL)
        *gtOpr = gt_opr;
    if (isHashable != NULL)
        *isHashable = hashable;
}

/* given operator tuple, return the operator OID */
Oid oprid(Operator op)
{
    return HeapTupleGetOid(op);
}

/* given operator tuple, return the underlying function's OID */
Oid oprfuncid(Operator op)
{
    Form_pg_operator pgopform = (Form_pg_operator)GETSTRUCT(op);

    return pgopform->oprcode;
}

static Form_pg_operator check_operator_is_shell(List* opname, ParseState* pstate, int location, Operator tup)
{
    Form_pg_operator opform = (Form_pg_operator)GETSTRUCT(tup);
    /* Check it's not a shell */
    if (!RegProcedureIsValid(opform->oprcode))
        ereport(ERROR,
            (errcode(ERRCODE_UNDEFINED_FUNCTION),
                errmsg("operator is only a shell: %s",
                    op_signature_string(opname, opform->oprkind, opform->oprleft, opform->oprright)),
                parser_errposition(pstate, location)));
    return opform;
}

static HeapTuple find_mapping_in_cache(OprCacheKey key, bool key_ok)
{
    HeapTuple tup = NULL;
    if (key_ok) {
        Oid operOid = find_oper_cache_entry(&key);
        if (OidIsValid(operOid))
            tup = SearchSysCache1(OPEROID, ObjectIdGetDatum(operOid));
    }
    return tup;
}

/*
 * Check for an "exact" match to the specified operand types.
 *
 * If one operand is an unknown literal, assume it should be taken to be
 * the same type as the other operand for this purpose.  Also, consider
 * the possibility that the other operand is a domain type that needs to
 * be reduced to its base type to find an "exact" match.
 *
 * If A-style coercion is active, other = unkonw will be treated as
 * text = text rather than other = other for other cases.
 */
static Oid binary_oper_exact(List* opname, Oid arg1, Oid arg2, bool use_a_style_coercion)
{
    Oid result;
    bool was_unknown = false;

    if (use_a_style_coercion) {
        /*
         * For A-style decode,
         * decode(<num>, <unknwon>, ...) will be compared as characters
         * decode(<unknwon/known string>, <num>, ...) will be compared as numbers
         * Note that decode(<num>, <known string type>, ...) categories are not
         * handled, because PG-style coercion suffers from blankspace padding of
         * bpchar and displaying fractional part of numeric, the behavior is tricky
         * to describe.
         */
        char arg1_category = get_typecategory(arg1);
        char arg2_category = get_typecategory(arg2);
        if (arg1_category == TYPCATEGORY_NUMERIC && arg2_category == TYPCATEGORY_UNKNOWN) {
            return OpernameGetOprid(opname, TEXTOID, TEXTOID);
        } else if (arg2_category == TYPCATEGORY_NUMERIC &&
                   (arg1_category == TYPCATEGORY_UNKNOWN || arg1_category == TYPCATEGORY_STRING)) {
            return OpernameGetOprid(opname, NUMERICOID, NUMERICOID);
        }
    }

    /* Unspecified type for one of the arguments? then use the other */
    if ((arg1 == UNKNOWNOID) && (arg2 != InvalidOid)) {
        arg1 = arg2;
        was_unknown = true;
    } else if ((arg2 == UNKNOWNOID) && (arg1 != InvalidOid)) {
        arg2 = arg1;
        was_unknown = true;
    }

    result = OpernameGetOprid(opname, arg1, arg2);
    if (OidIsValid(result))
        return result;

    if (was_unknown) {
        /* arg1 and arg2 are the same here, need only look at arg1 */
        Oid basetype = getBaseType(arg1);
        if (basetype != arg1) {
            result = OpernameGetOprid(opname, basetype, basetype);
            if (OidIsValid(result))
                return result;
        }
    }

    return InvalidOid;
}

/*
 *		Given the input argtype array and one or more candidates
 *		for the operator, attempt to resolve the conflict.
 *
 * Returns FUNCDETAIL_NOTFOUND, FUNCDETAIL_MULTIPLE, or FUNCDETAIL_NORMAL.
 * In the success case the Oid of the best candidate is stored in *operOid.
 *
 * Note that the caller has already determined that there is no candidate
 * exactly matching the input argtype(s).  Incompatible candidates are not yet
 * pruned away, however.
 */
static FuncDetailCode oper_select_candidate(
    int nargs, Oid* input_typeids, FuncCandidateList candidates, Oid* operOid) /* output argument */
{
    int ncandidates;

    /*
     * Delete any candidates that cannot actually accept the given input
     * types, whether directly or by coercion.
     */
    ncandidates = func_match_argtypes(nargs, input_typeids, candidates, &candidates);
    /* Done if no candidate or only one candidate survives */
    if (ncandidates == 0) {
        *operOid = InvalidOid;
        return FUNCDETAIL_NOTFOUND;
    }
    if (ncandidates == 1) {
        *operOid = candidates->oid;
        return FUNCDETAIL_NORMAL;
    }

    /*
     * Use the same heuristics as for ambiguous functions to resolve the
     * conflict.
     */
    candidates = func_select_candidate(nargs, input_typeids, candidates);
    if (candidates) {
        *operOid = candidates->oid;
        return FUNCDETAIL_NORMAL;
    }

    *operOid = InvalidOid;
    return FUNCDETAIL_MULTIPLE; /* failed to select a best candidate */
}

/*
 * @Description: if the type is a int type that can be converted to numeric
 * @in typid - type oid
 * @return - return true if the type is a numeric type that can be converted to numeric.
 */
bool IsIntType(Oid typeoid)
{
    switch (typeoid) {
        case INT1OID:
        case INT2OID:
        case INT4OID:
        case INT8OID:
            return true;
        default:
            return false;
    }
}

/* oper() -- search for a binary operator
 * Given operator name, types of arg1 and arg2, return oper struct.
 *
 * IMPORTANT: the returned operator (if any) is only promised to be
 * coercion-compatible with the input datatypes.  Do not use this if
 * you need an exact- or binary-compatible match; see compatible_oper.
 *
 * If no matching operator found, return NULL if noError is true,
 * raise an error if it is false.  pstate and location are used only to report
 * the error position; pass NULL/-1 if not available.
 *
 * NOTE: on success, the returned object is a syscache entry.  The caller
 * must ReleaseSysCache() the entry when done with it.
 */
Operator oper(ParseState* pstate, List* opname, Oid ltypeId, Oid rtypeId, bool noError, int location, bool inNumeric)
{
    Oid operOid;
    OprCacheKey key;
    bool key_ok = false;
    FuncDetailCode fdresult = FUNCDETAIL_NOTFOUND;
    HeapTuple tup;
    bool use_a_style_coercion = false;

    if (inNumeric && u_sess->attr.attr_sql.convert_string_to_digit &&
        ((IsIntType(ltypeId) && IsCharType(rtypeId)) || (IsIntType(rtypeId) && IsCharType(ltypeId)))) {
        ltypeId = NUMERICOID;
        rtypeId = NUMERICOID;
    }

    /*
     * In A_FORMAT compatibility and CHAR_COERCE_COMPAT, we choose TEXT-related
     * operators for varchar and bpchar.
     */
    if (DB_IS_CMPT(A_FORMAT) && CHAR_COERCE_COMPAT && (((ltypeId == VARCHAROID) && (rtypeId == BPCHAROID)) ||
        ((rtypeId == VARCHAROID) && (ltypeId == BPCHAROID)))) {
        ltypeId = TEXTOID;
        rtypeId = TEXTOID;
    }

    /*
     * Try to find the mapping in the lookaside cache.
     */
    if (pstate != NULL) {
        use_a_style_coercion = pstate->p_is_decode && ENABLE_SQL_BETA_FEATURE(A_STYLE_COERCE);
    }
        
    key_ok = make_oper_cache_key(&key, opname, ltypeId, rtypeId, use_a_style_coercion);
    tup = find_mapping_in_cache(key, key_ok);
    if (HeapTupleIsValid(tup))
        return (Operator)tup;

    /*
     * First try for an "exact" match.
     */
    operOid = binary_oper_exact(opname, ltypeId, rtypeId, use_a_style_coercion);
    if (!OidIsValid(operOid)) {
        /*
         * Otherwise, search for the most suitable candidate.
         */
        FuncCandidateList clist;

        /* Get binary operators of given name */
        clist = OpernameGetCandidates(opname, 'b');
        /* No operators found? Then fail... */
        if (clist != NULL) {
            /*
             * Unspecified type for one of the arguments? then use the other
             * (XXX this is probably dead code?)
             */
            Oid inputOids[2];

            if (rtypeId == InvalidOid)
                rtypeId = ltypeId;
            else if (ltypeId == InvalidOid)
                ltypeId = rtypeId;
            inputOids[0] = ltypeId;
            inputOids[1] = rtypeId;
            fdresult = oper_select_candidate(2, inputOids, clist, &operOid);
        }
    }

    if (OidIsValid(operOid))
        tup = SearchSysCache1(OPEROID, ObjectIdGetDatum(operOid));

    if (HeapTupleIsValid(tup)) {
        if (key_ok)
            make_oper_cache_entry(&key, operOid);
    } else if (!noError)
        op_error(pstate, opname, 'b', ltypeId, rtypeId, fdresult, location);

    return (Operator)tup;
}

/*
 *	given an opname and input datatypes, find a compatible binary operator
 *
 *	This is tighter than oper() because it will not return an operator that
 *	requires coercion of the input datatypes (but binary-compatible operators
 *	are accepted).	Otherwise, the semantics are the same.
 */
Operator compatible_oper(ParseState* pstate, List* op, Oid arg1, Oid arg2, bool noError, int location)
{
    Operator optup;
    Form_pg_operator opform;

    /* oper() will find the best available match */
    optup = oper(pstate, op, arg1, arg2, noError, location);
    if (optup == (Operator)NULL)
        return (Operator)NULL; /* must be noError case */

    /* but is it good enough? */
    opform = (Form_pg_operator)GETSTRUCT(optup);
    if (IsBinaryCoercible(arg1, opform->oprleft) && IsBinaryCoercible(arg2, opform->oprright))
        return optup;

    /* nope... */
    ReleaseSysCache(optup);

    if (!noError)
        ereport(ERROR,
            (errcode(ERRCODE_UNDEFINED_FUNCTION),
                errmsg("operator requires run-time type coercion: %s", op_signature_string(op, 'b', arg1, arg2)),
                parser_errposition(pstate, location)));

    return (Operator)NULL;
}

/* compatible_oper_opid() -- get OID of a binary operator
 *
 * This is a convenience routine that extracts only the operator OID
 * from the result of compatible_oper().  InvalidOid is returned if the
 * lookup fails and noError is true.
 */
Oid compatible_oper_opid(List* op, Oid arg1, Oid arg2, bool noError)
{
    Operator optup;
    Oid result;

    optup = compatible_oper(NULL, op, arg1, arg2, noError, -1);
    if (optup != NULL) {
        result = oprid(optup);
        ReleaseSysCache(optup);
        return result;
    }
    return InvalidOid;
}

/* right_oper() -- search for a unary right operator (postfix operator)
 * Given operator name and type of arg, return oper struct.
 *
 * IMPORTANT: the returned operator (if any) is only promised to be
 * coercion-compatible with the input datatype.  Do not use this if
 * you need an exact- or binary-compatible match.
 *
 * If no matching operator found, return NULL if noError is true,
 * raise an error if it is false.  pstate and location are used only to report
 * the error position; pass NULL/-1 if not available.
 *
 * NOTE: on success, the returned object is a syscache entry.  The caller
 * must ReleaseSysCache() the entry when done with it.
 */
Operator right_oper(ParseState* pstate, List* op, Oid arg, bool noError, int location)
{
    Oid operOid;
    OprCacheKey key;
    bool key_ok = false;
    FuncDetailCode fdresult = FUNCDETAIL_NOTFOUND;
    HeapTuple tup;

    /*
     * Try to find the mapping in the lookaside cache.
     */
    key_ok = make_oper_cache_key(&key, op, arg, InvalidOid);
    tup = find_mapping_in_cache(key, key_ok);
    if (HeapTupleIsValid(tup))
        return (Operator)tup;
    /*
     * First try for an "exact" match.
     */
    operOid = OpernameGetOprid(op, arg, InvalidOid);
    if (!OidIsValid(operOid)) {
        /*
         * Otherwise, search for the most suitable candidate.
         */
        FuncCandidateList clist;

        /* Get postfix operators of given name */
        clist = OpernameGetCandidates(op, 'r');
        /* No operators found? Then fail... */
        if (clist != NULL) {
            /*
             * We must run oper_select_candidate even if only one candidate,
             * otherwise we may falsely return a non-type-compatible operator.
             */
            fdresult = oper_select_candidate(1, &arg, clist, &operOid);
        }
    }

    if (OidIsValid(operOid))
        tup = SearchSysCache1(OPEROID, ObjectIdGetDatum(operOid));

    if (HeapTupleIsValid(tup)) {
        if (key_ok)
            make_oper_cache_entry(&key, operOid);
    } else if (!noError)
        op_error(pstate, op, 'r', arg, InvalidOid, fdresult, location);

    return (Operator)tup;
}

/* left_oper() -- search for a unary left operator (prefix operator)
 * Given operator name and type of arg, return oper struct.
 *
 * IMPORTANT: the returned operator (if any) is only promised to be
 * coercion-compatible with the input datatype.  Do not use this if
 * you need an exact- or binary-compatible match.
 *
 * If no matching operator found, return NULL if noError is true,
 * raise an error if it is false.  pstate and location are used only to report
 * the error position; pass NULL/-1 if not available.
 *
 * NOTE: on success, the returned object is a syscache entry.  The caller
 * must ReleaseSysCache() the entry when done with it.
 */
Operator left_oper(ParseState* pstate, List* op, Oid arg, bool noError, int location)
{
    Oid operOid;
    OprCacheKey key;
    bool key_ok = false;
    FuncDetailCode fdresult = FUNCDETAIL_NOTFOUND;
    HeapTuple tup;

    /*
     * Try to find the mapping in the lookaside cache.
     */
    key_ok = make_oper_cache_key(&key, op, InvalidOid, arg);
    tup = find_mapping_in_cache(key, key_ok);
    if (HeapTupleIsValid(tup))
        return (Operator)tup;
    /*
     * First try for an "exact" match.
     */
    operOid = OpernameGetOprid(op, InvalidOid, arg);
    if (!OidIsValid(operOid)) {
        /*
         * Otherwise, search for the most suitable candidate.
         */
        FuncCandidateList clist;

        /* Get prefix operators of given name */
        clist = OpernameGetCandidates(op, 'l');
        /* No operators found? Then fail... */
        if (clist != NULL) {
            /*
             * The returned list has args in the form (0, oprright). Move the
             * useful data into args[0] to keep oper_select_candidate simple.
             * XXX we are assuming here that we may scribble on the list!
             */
            FuncCandidateList clisti;

            for (clisti = clist; clisti != NULL; clisti = clisti->next) {
                clisti->args[0] = clisti->args[1];
            }

            /*
             * We must run oper_select_candidate even if only one candidate,
             * otherwise we may falsely return a non-type-compatible operator.
             */
            fdresult = oper_select_candidate(1, &arg, clist, &operOid);
        }
    }

    if (OidIsValid(operOid))
        tup = SearchSysCache1(OPEROID, ObjectIdGetDatum(operOid));

    if (HeapTupleIsValid(tup)) {
        if (key_ok)
            make_oper_cache_entry(&key, operOid);
    } else if (!noError)
        op_error(pstate, op, 'l', InvalidOid, arg, fdresult, location);

    return (Operator)tup;
}

/*
 * op_signature_string
 *		Build a string representing an operator name, including arg type(s).
 *		The result is something like "integer + integer".
 *
 * This is typically used in the construction of operator-not-found error
 * messages.
 */
static const char* op_signature_string(List* op, char oprkind, Oid arg1, Oid arg2)
{
    StringInfoData argbuf;

    initStringInfo(&argbuf);

    if (oprkind != 'l')
        appendStringInfo(&argbuf, "%s ", format_type_be(arg1));

    appendStringInfoString(&argbuf, NameListToString(op));

    if (oprkind != 'r')
        appendStringInfo(&argbuf, " %s", format_type_be(arg2));

    return argbuf.data; /* return palloc'd string buffer */
}

/*
 * op_error - utility routine to complain about an unresolvable operator
 */
static void op_error(
    ParseState* pstate, List* op, char oprkind, Oid arg1, Oid arg2, FuncDetailCode fdresult, int location)
{
    if (fdresult == FUNCDETAIL_MULTIPLE)
        ereport(ERROR,
            (errcode(ERRCODE_AMBIGUOUS_FUNCTION),
                errmsg("operator is not unique: %s", op_signature_string(op, oprkind, arg1, arg2)),
                errhint("Could not choose a best candidate operator. "
                        "You might need to add explicit type casts."),
                parser_errposition(pstate, location)));
    else
        ereport(ERROR,
            (errcode(ERRCODE_UNDEFINED_FUNCTION),
                errmsg("operator does not exist: %s", op_signature_string(op, oprkind, arg1, arg2)),
                errhint("No operator matches the given name and argument type(s). "
                        "You might need to add explicit type casts."),
                parser_errposition(pstate, location)));
}

/*
 *		parse_get_last_srf
 *
 * Check pstate, if pstate is NULL, we can't use pstate->p_last_srf directly,
 * it's okay to return a NULL value.
 */
Node* parse_get_last_srf(ParseState* pstate)
{
    if (pstate != NULL) {
        return pstate->p_last_srf;
    }
    return NULL;
}

/*
 *		Operator expression construction.
 *
 * Transform operator expression ensuring type compatibility.
 * This is where some type conversion happens.
 *
 * As with coerce_type, pstate may be NULL if no special unknown-Param
 * processing is wanted.
 */
Expr* make_op(ParseState* pstate, List* opname, Node* ltree, Node* rtree, Node* last_srf, int location, bool inNumeric)
{
    Oid ltypeId, rtypeId;
    Operator tup;
    Form_pg_operator opform;
    Oid actual_arg_types[2];
    Oid declared_arg_types[2];
    int nargs;
    List* args = NIL;
    Oid rettype;
    OpExpr* result = NULL;

    /* Select the operator */
    if (rtree == NULL) {
        /* right operator */
        ltypeId = exprType(ltree);
        rtypeId = InvalidOid;
        tup = right_oper(pstate, opname, ltypeId, false, location);
    } else if (ltree == NULL) {
        /* left operator */
        rtypeId = exprType(rtree);
        ltypeId = InvalidOid;
        tup = left_oper(pstate, opname, rtypeId, false, location);
    } else {
        /* otherwise, binary operator */
        ltypeId = exprType(ltree);
        rtypeId = exprType(rtree);
        /* if one of the types is a encrypted type and we are in a function/procedure creation flow */
        if (IsClientLogicType(ltypeId) || IsClientLogicType(rtypeId)) {
            if(pstate != NULL && pstate->p_create_proc_operator_hook) {
                pstate->p_create_proc_operator_hook(pstate, ltree, rtree, &ltypeId, &rtypeId);
            }
        }
        tup = oper(pstate, opname, ltypeId, rtypeId, false, location, inNumeric);
    }

    opform = check_operator_is_shell(opname, pstate, location, tup);

    /* Do typecasting and build the expression tree */
    if (rtree == NULL) {
        /* right operator */
        args = list_make1(ltree);
        actual_arg_types[0] = ltypeId;
        declared_arg_types[0] = opform->oprleft;
        nargs = 1;
    } else if (ltree == NULL) {
        /* left operator */
        args = list_make1(rtree);
        actual_arg_types[0] = rtypeId;
        declared_arg_types[0] = opform->oprright;
        nargs = 1;
    } else {
        /* otherwise, binary operator */
        args = list_make2(ltree, rtree);
        actual_arg_types[0] = ltypeId;
        actual_arg_types[1] = rtypeId;
        declared_arg_types[0] = opform->oprleft;
        declared_arg_types[1] = opform->oprright;
        nargs = 2;
    }

    /*
     * enforce consistency with polymorphic argument and return types,
     * possibly adjusting return type or declared_arg_types (which will be
     * used as the cast destination by make_fn_arguments)
     */
    rettype = enforce_generic_type_consistency(actual_arg_types, declared_arg_types, nargs, opform->oprresult, false);

    /* perform the necessary typecasting of arguments */
    make_fn_arguments(pstate, args, actual_arg_types, declared_arg_types);

    /* and build the expression node */
    result = makeNode(OpExpr);
    result->opno = oprid(tup);
    result->opfuncid = opform->oprcode;
    result->opresulttype = rettype;
    result->opretset = get_func_retset(opform->oprcode);
    /* opcollid and inputcollid will be set by parse_collate.c */
    result->args = args;
    result->location = location;

    /* if it returns a set, check that's OK */
    if (result->opretset && pstate && pstate->p_is_flt_frame) {
        check_srf_call_placement(pstate, last_srf, location);
        /* ... and remember it for error checks at higher levels */
        pstate->p_last_srf = (Node*)result;
    }

    ReleaseSysCache(tup);

    return (Expr*)result;
}

/*
 *		Build expression tree for "scalar op ANY/ALL (array)" construct.
 */
Expr* make_scalar_array_op(ParseState* pstate, List* opname, bool useOr, Node* ltree, Node* rtree, int location)
{
    Oid ltypeId, rtypeId, atypeId, res_atypeId;
    Operator tup;
    Form_pg_operator opform;
    Oid actual_arg_types[2];
    Oid declared_arg_types[2];
    List* args = NIL;
    Oid rettype;
    ScalarArrayOpExpr* result = NULL;

    ltypeId = exprType(ltree);
    atypeId = exprType(rtree);
    /*
     * The right-hand input of the operator will be the element type of the
     * array.  However, if we currently have just an untyped literal on the
     * right, stay with that and hope we can resolve the operator.
     */
    if (atypeId == UNKNOWNOID)
        rtypeId = UNKNOWNOID;
    else {
        rtypeId = get_base_element_type(atypeId);
        if (!OidIsValid(rtypeId))
            ereport(ERROR,
                (errcode(ERRCODE_WRONG_OBJECT_TYPE),
                    errmsg("op ANY/ALL (array) requires array on right side"),
                    parser_errposition(pstate, location)));
    }

    /* Now resolve the operator */
    tup = oper(pstate, opname, ltypeId, rtypeId, false, location);
    opform = check_operator_is_shell(opname, pstate, location, tup);

    args = list_make2(ltree, rtree);
    actual_arg_types[0] = ltypeId;
    actual_arg_types[1] = rtypeId;
    declared_arg_types[0] = opform->oprleft;
    declared_arg_types[1] = opform->oprright;

    /*
     * enforce consistency with polymorphic argument and return types,
     * possibly adjusting return type or declared_arg_types (which will be
     * used as the cast destination by make_fn_arguments)
     */
    rettype = enforce_generic_type_consistency(actual_arg_types, declared_arg_types, 2, opform->oprresult, false);
    /*
     * Check that operator result is boolean
     */
    if (rettype != BOOLOID)
        ereport(ERROR,
            (errcode(ERRCODE_WRONG_OBJECT_TYPE),
                errmsg("op ANY/ALL (array) requires operator to yield boolean"),
                parser_errposition(pstate, location)));
    if (get_func_retset(opform->oprcode))
        ereport(ERROR,
            (errcode(ERRCODE_WRONG_OBJECT_TYPE),
                errmsg("op ANY/ALL (array) requires operator not to return a set"),
                parser_errposition(pstate, location)));

    /*
     * Now switch back to the array type on the right, arranging for any
     * needed cast to be applied.  Beware of polymorphic operators here;
     * enforce_generic_type_consistency may or may not have replaced a
     * polymorphic type with a real one.
     */
    if (IsPolymorphicType(declared_arg_types[1])) {
        /* assume the actual array type is OK */
        res_atypeId = atypeId;
    } else {
        res_atypeId = get_array_type(declared_arg_types[1]);
        if (!OidIsValid(res_atypeId))
            ereport(ERROR,
                (errcode(ERRCODE_UNDEFINED_OBJECT),
                    errmsg("could not find array type for data type %s", format_type_be(declared_arg_types[1])),
                    parser_errposition(pstate, location)));
    }
    actual_arg_types[1] = atypeId;
    declared_arg_types[1] = res_atypeId;

    /* perform the necessary typecasting of arguments */
    make_fn_arguments(pstate, args, actual_arg_types, declared_arg_types);

    /* and build the expression node */
    result = makeNode(ScalarArrayOpExpr);
    result->opno = oprid(tup);
    result->opfuncid = opform->oprcode;
    result->useOr = useOr;
    /* inputcollid will be set by parse_collate.c */
    result->args = args;
    result->location = location;

    ReleaseSysCache(tup);

    return (Expr*)result;
}

/*
 * Lookaside cache to speed operator lookup.  Possibly this should be in
 * a separate module under utils/cache/ ?
 *
 * The idea here is that the mapping from operator name and given argument
 * types is constant for a given search path (or single specified schema OID)
 * so long as the contents of pg_operator and pg_cast don't change.  And that
 * mapping is pretty expensive to compute, especially for ambiguous operators;
 * this is mainly because there are a *lot* of instances of popular operator
 * names such as "=", and we have to check each one to see which is the
 * best match.	So once we have identified the correct mapping, we save it
 * in a cache that need only be flushed on pg_operator or pg_cast change.
 * (pg_cast must be considered because changes in the set of implicit casts
 * affect the set of applicable operators for any given input datatype.)
 *
 * XXX in principle, ALTER TABLE ... INHERIT could affect the mapping as
 * well, but we disregard that since there's no convenient way to find out
 * about it, and it seems a pretty far-fetched corner-case anyway.
 *
 * Note: at some point it might be worth doing a similar cache for function
 * lookups.  However, the potential gain is a lot less since (a) function
 * names are generally not overloaded as heavily as operator names, and
 * (b) we'd have to flush on pg_proc updates, which are probably a good
 * deal more common than pg_operator updates.
 */

/*
 * make_oper_cache_key
 *		Fill the lookup key struct given operator name and arg types.
 *
 * Returns TRUE if successful, FALSE if the search_path overflowed
 * (hence no caching is possible).
 */
static bool make_oper_cache_key(OprCacheKey* key, List* opname, Oid ltypeId, Oid rtypeId, bool use_a_style_coercion)
{
    char* schemaname = NULL;
    char* opername = NULL;
    errno_t rc = EOK;

    /* deconstruct the name list */
    DeconstructQualifiedName(opname, &schemaname, &opername);

    /* ensure zero-fill for stable hashing */
    rc = memset_s(key, sizeof(OprCacheKey), 0, sizeof(OprCacheKey));
    securec_check(rc, "\0", "\0");

    /* save operator name and input types into key */
    strlcpy(key->oprname, opername, NAMEDATALEN);
    key->left_arg = ltypeId;
    key->right_arg = rtypeId;
    key->use_a_style_coercion = use_a_style_coercion;

    if (schemaname != NULL) {
        /* search only in exact schema given */
        key->search_path[0] = LookupExplicitNamespace(schemaname);
    } else {
        /* get the active search path */
        if (fetch_search_path_array(key->search_path, MAX_CACHED_PATH_LEN) > MAX_CACHED_PATH_LEN)
            return false; /* oops, didn't fit */
    }

    return true;
}

/*
 * find_oper_cache_entry
 *
 * Look for a cache entry matching the given key.  If found, return the
 * contained operator OID, else return InvalidOid.
 */
static Oid find_oper_cache_entry(OprCacheKey* key)
{
    OprCacheEntry* oprentry = NULL;

    if (u_sess->parser_cxt.opr_cache_hash == NULL) {
        /* First time through: initialize the hash table */
        HASHCTL ctl;
        errno_t rc;

        rc = memset_s(&ctl, sizeof(ctl), 0, sizeof(ctl));
        securec_check(rc, "\0", "\0");
        ctl.keysize = sizeof(OprCacheKey);
        ctl.entrysize = sizeof(OprCacheEntry);
        ctl.hash = tag_hash;
        ctl.hcxt = u_sess->cache_mem_cxt;
        u_sess->parser_cxt.opr_cache_hash =
            hash_create("Operator lookup cache", 256, &ctl, HASH_ELEM | HASH_FUNCTION | HASH_CONTEXT);

        /* Arrange to flush cache on pg_operator and pg_cast changes */
        CacheRegisterSessionSyscacheCallback(OPERNAMENSP, InvalidateOprCacheCallBack, (Datum)0);
        CacheRegisterSessionSyscacheCallback(CASTSOURCETARGET, InvalidateOprCacheCallBack, (Datum)0);
    }

    /* Look for an existing entry */
    oprentry = (OprCacheEntry*)hash_search(u_sess->parser_cxt.opr_cache_hash, (void*)key, HASH_FIND, NULL);
    if (oprentry == NULL)
        return InvalidOid;

    return oprentry->opr_oid;
}

/*
 * make_oper_cache_entry
 *
 * Insert a cache entry for the given key.
 */
static void make_oper_cache_entry(OprCacheKey* key, Oid opr_oid)
{
    OprCacheEntry* oprentry = NULL;

    if (unlikely(u_sess->parser_cxt.opr_cache_hash == NULL)) {
        ereport(ERROR, 
            (errcode(ERRCODE_UNEXPECTED_NULL_VALUE), 
                errmsg("u_sess->parser_cxt.opr_cache_hash should not be null")));
    }

    oprentry = (OprCacheEntry*)hash_search(u_sess->parser_cxt.opr_cache_hash, (void*)key, HASH_ENTER, NULL);
    oprentry->opr_oid = opr_oid;
}

/*
 * Callback for pg_operator and pg_cast inval events
 *
 * We also use it for invaliding the operator cache when type conversion priority
 * changed, e.g. modified parameter convert_string_to_digit.
 */
void InvalidateOprCacheCallBack(Datum arg, int cacheid, uint32 hashvalue)
{
    HASH_SEQ_STATUS status;
    OprCacheEntry* hentry = NULL;

    if (u_sess->parser_cxt.opr_cache_hash == NULL) {
        /*
         * Check whether InvalidateOprCacheCallBack is called because of pg_operator
         * or pg_cast is modified.
         */
        if (cacheid == OPERNAMENSP || cacheid == CASTSOURCETARGET)
            ereport(ERROR,
                (errmodule(MOD_OPT),
                    errcode(ERRCODE_UNEXPECTED_NULL_VALUE),
                    errmsg("OprCacheHash is unexpected null")));
        else
            return;
    }

    /* Currently we just flush all entries; hard to be smarter ... */
    hash_seq_init(&status, u_sess->parser_cxt.opr_cache_hash);

    while ((hentry = (OprCacheEntry*)hash_seq_search(&status)) != NULL) {
        if (hash_search(u_sess->parser_cxt.opr_cache_hash, (void*)&hentry->key, HASH_REMOVE, NULL) == NULL)
            ereport(ERROR, (errcode(ERRCODE_DATA_EXCEPTION), errmsg("hash table corrupted")));
    }
}
