/* -------------------------------------------------------------------------
 *
 * float.c
 *	  Functions for the built-in floating-point types.
 *
 * Portions Copyright (c) 1996-2012, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 * Portions Copyright (c) 2021, openGauss Contributors
 *
 *
 * IDENTIFICATION
 *	  src/backend/utils/adt/float.c
 *
 * -------------------------------------------------------------------------
 */
#include "postgres.h"
#include "knl/knl_variable.h"

#include <float.h>
#include <math.h>
#include <limits.h>

#include "catalog/pg_type.h"
#include "common/int.h"
#include "libpq/pqformat.h"
#include "utils/array.h"
#include "utils/builtins.h"
#include "optimizer/pgxcship.h"
#include "miscadmin.h"

#ifndef M_PI
/* from my RH5.2 gcc math.h file - thomas 2000-04-03 */
#define M_PI 3.14159265358979323846
#endif

/* Visual C++ etc lacks NAN, and won't accept 0.0/0.0.  NAN definition from
 * http://msdn.microsoft.com/library/default.asp?url=/library/en-us/vclang/html/vclrfNotNumberNANItems.asp
 */
#if defined(WIN32) && !defined(NAN)
static const uint32 nan[2] = {0xffffffff, 0x7fffffff};

#define NAN (*(const double*)nan)
#endif

/* not sure what the following should be, but better to make it over-sufficient */
#define MAXFLOATWIDTH 64
#define MAXDOUBLEWIDTH 128

/*
 * check to see if a float4/8 val has underflowed or overflowed
 */
#define CHECKFLOATVAL(val, inf_is_valid, zero_is_valid)                                                             \
    do {                                                                                                            \
        if (isinf(val) && !(inf_is_valid))                                                                          \
            ereport(ERROR, (errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE), errmsg("value out of range: overflow")));  \
                                                                                                                    \
        if ((val) == 0.0 && !(zero_is_valid))                                                                       \
            ereport(ERROR, (errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE), errmsg("value out of range: underflow"))); \
    } while (0)

/* ========== USER I/O ROUTINES ========== */

static int float4_cmp_internal(float4 a, float4 b);
double float8in_internal(char* str, char** s, bool* hasError);

#ifndef HAVE_CBRT
/*
 * Some machines (in particular, some versions of AIX) have an extern
 * declaration for cbrt() in <math.h> but fail to provide the actual
 * function, which causes configure to not set HAVE_CBRT.  Furthermore,
 * their compilers spit up at the mismatch between extern declaration
 * and static definition.  We work around that here by the expedient
 * of a #define to make the actual name of the static function different.
 */
#define cbrt my_cbrt
static double cbrt(double x);
#endif /* HAVE_CBRT */

/*
 * Routines to provide reasonably platform-independent handling of
 * infinity and NaN.  We assume that isinf() and isnan() are available
 * and work per spec.  (On some platforms, we have to supply our own;
 * see src/port.)  However, generating an Infinity or NaN in the first
 * place is less well standardized; pre-C99 systems tend not to have C99's
 * INFINITY and NAN macros.  We centralize our workarounds for this here.
 */

double float8in_internal(char* str, char** endptr_p, bool* hasError)
{
    double val;
    char* endptr = NULL;
    /* Marking down the initial value of str. */
    const char* orig_num = str;
    const int nanLen = 3;
    const int infinityLen = 8;
    const int minusInfinityLen = 9;

    Assert(str != NULL);

    while (*str != '\0' && isspace((unsigned char)*str)) {
        str++;
    }

    if (*str == '\0') {
        *hasError = TRUE;
        return 0.0;
    }

    errno = 0;
    val = strtod(str, &endptr);

    if (endptr == str || errno != 0) {
        int save_errno = errno;

        if (pg_strncasecmp(str, "NaN", nanLen) == 0) {
            val = get_float8_nan();
            endptr = str + 3;
        } else if (pg_strncasecmp(str, "Infinity", infinityLen) == 0) {
            val = get_float8_infinity();
            endptr = str + 8;
        } else if (pg_strncasecmp(str, "-Infinity", minusInfinityLen) == 0) {
            val = -get_float8_infinity();
            endptr = str + 9;
        } else if (save_errno == ERANGE) {
            if (val == 0.0 || val >= HUGE_VAL || val <= -HUGE_VAL) {
                char* errnumber = pstrdup(str);
                errnumber[endptr - str] = '\0';
                ereport(ERROR,
                    (errmodule(MOD_FUNCTION), errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
                        errmsg("\"%s\" is out of range for type double precision", errnumber),
                        errdetail("N/A"),
                        errcause("Input number exceeding limit of float8."),
                        erraction("Change input number within float8 interval.")));
            }
        } else {
            *hasError = TRUE;
            return 0.0;
        }
    }

    while (*endptr != '\0' && isspace((unsigned char)*endptr)) {
        endptr++;
    }

    /* report stopping point, else report error if not end of string */
    if (endptr_p) {
        *endptr_p = endptr;
    } else if (*endptr != '\0') {
        ereport(ERROR,
            (errmodule(MOD_FUNCTION), errcode(ERRCODE_INVALID_TEXT_REPRESENTATION),
                errmsg("invalid input syntax for type double precision: \"%s\"", orig_num),
                errdetail("N/A"),
                errcause("Wrong input syntax."),
                erraction("Change input syntax.")));
    }
    return val;
}


double get_float8_infinity(void)
{
#ifdef INFINITY
    /* C99 standard way */
    return (double)INFINITY;
#else

    /*
     * On some platforms, HUGE_VAL is an infinity, elsewhere it's just the
     * largest normal double.  We assume forcing an overflow will get us a
     * true infinity.
     */
    return (double)(HUGE_VAL * HUGE_VAL);
#endif
}

float get_float4_infinity(void)
{
#ifdef INFINITY
    /* C99 standard way */
    return (float)INFINITY;
#else

    /*
     * On some platforms, HUGE_VAL is an infinity, elsewhere it's just the
     * largest normal double.  We assume forcing an overflow will get us a
     * true infinity.
     */
    return (float)(HUGE_VAL * HUGE_VAL);
#endif
}

double get_float8_nan(void)
{
    /* (double) NAN doesn't work on some NetBSD/MIPS releases */
#if defined(NAN) && !(defined(__NetBSD__) && defined(__mips__))
    /* C99 standard way */
    return (double)NAN;
#else
    /* Assume we can get a NAN via zero divide */
    return (double)(0.0 / 0.0);
#endif
}

float get_float4_nan(void)
{
#ifdef NAN
    /* C99 standard way */
    return (float)NAN;
#else
    /* Assume we can get a NAN via zero divide */
    return (float)(0.0 / 0.0);
#endif
}

/*
 * Returns -1 if 'val' represents negative infinity, 1 if 'val'
 * represents (positive) infinity, and 0 otherwise. On some platforms,
 * this is equivalent to the isinf() macro, but not everywhere: C99
 * does not specify that isinf() needs to distinguish between positive
 * and negative infinity.
 */
int is_infinite(double val)
{
    int inf = isinf(val);

    if (inf == 0)
        return 0;
    else if (val > 0)
        return 1;
    else
        return -1;
}

/*
 *		float4in		- converts "num" to float
 *						  restricted syntax:
 *						  {<sp>} [+|-] {digit} [.{digit}] [<exp>]
 *						  where <sp> is a space, digit is 0-9,
 *						  <exp> is "e" or "E" followed by an integer.
 */
Datum float4in(PG_FUNCTION_ARGS)
{
    char* num = PG_GETARG_CSTRING(0);
    char* orig_num = NULL;
    double val;
    char* endptr = NULL;

    /*
     * endptr points to the first character _after_ the sequence we recognized
     * as a valid floating point number. orig_num points to the original input
     * string.
     */
    orig_num = num;

    /*
     * Check for an empty-string input to begin with, to avoid the vagaries of
     * strtod() on different platforms.
     */
    if (*num == '\0') {
        if (fcinfo->can_ignore) {
            ereport(WARNING,
                    (errcode(ERRCODE_INVALID_TEXT_REPRESENTATION),
                     errmsg("invalid input syntax for type real: \"%s\". truncated automatically", orig_num)));
            PG_RETURN_FLOAT4(0);
        }
        ereport(ERROR,
            (errcode(ERRCODE_INVALID_TEXT_REPRESENTATION),
                errmsg("invalid input syntax for type real: \"%s\"", orig_num)));
    }

    /* skip leading whitespace */
    while (*num != '\0' && isspace((unsigned char)*num))
        num++;

    errno = 0;
    val = strtod(num, &endptr);

    /* change -0 to 0 */
    if (*num == '-' && val == 0.0) {
        val += 0.0;
    }

    /* did we not see anything that looks like a double? */
    if (endptr == num || errno != 0) {
        int save_errno = errno;

        /*
         * C99 requires that strtod() accept NaN and [-]Infinity, but not all
         * platforms support that yet (and some accept them but set ERANGE
         * anyway...)  Therefore, we check for these inputs ourselves.
         */
        if (pg_strncasecmp(num, "NaN", 3) == 0) {
            val = get_float4_nan();
            endptr = num + 3;
        } else if (pg_strncasecmp(num, "Infinity", 8) == 0) {
            val = get_float4_infinity();
            endptr = num + 8;
        } else if (pg_strncasecmp(num, "-Infinity", 9) == 0) {
            val = -get_float4_infinity();
            endptr = num + 9;
        } else if (save_errno == ERANGE) {
            /*
             * Some platforms return ERANGE for denormalized numbers (those
             * that are not zero, but are too close to zero to have full
             * precision).	We'd prefer not to throw error for that, so try to
             * detect whether it's a "real" out-of-range condition by checking
             * to see if the result is zero or huge.
             */
            if (val == 0.0 || val >= HUGE_VAL || val <= -HUGE_VAL) {
                ereport((fcinfo->can_ignore ? WARNING : ERROR),
                        (errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
                         errmsg("\"%s\" is out of range for type real", orig_num)));
                val = (val == 0.0 ? 0 : (val >= HUGE_VAL ? FLT_MAX : FLT_MIN));
            }
        } else if (!fcinfo->can_ignore)
            ereport(ERROR,
                (errcode(ERRCODE_INVALID_TEXT_REPRESENTATION),
                    errmsg("invalid input syntax for type real: \"%s\"", orig_num)));
    }
#ifdef HAVE_BUGGY_SOLARIS_STRTOD
    else {
        /*
         * Many versions of Solaris have a bug wherein strtod sets endptr to
         * point one byte beyond the end of the string when given "inf" or
         * "infinity".
         */
        if (endptr != num && endptr[-1] == '\0')
            endptr--;
    }
#endif /* HAVE_BUGGY_SOLARIS_STRTOD */

#ifdef HAVE_BUGGY_IRIX_STRTOD

    /*
     * In some IRIX versions, strtod() recognizes only "inf", so if the input
     * is "infinity" we have to skip over "inity".	Also, it may return
     * positive infinity for "-inf".
     */
    if (isinf(val)) {
        if (pg_strncasecmp(num, "Infinity", 8) == 0) {
            val = get_float4_infinity();
            endptr = num + 8;
        } else if (pg_strncasecmp(num, "-Infinity", 9) == 0) {
            val = -get_float4_infinity();
            endptr = num + 9;
        } else if (pg_strncasecmp(num, "-inf", 4) == 0) {
            val = -get_float4_infinity();
            endptr = num + 4;
        }
    }
#endif /* HAVE_BUGGY_IRIX_STRTOD */

    /* skip trailing whitespace */
    while (*endptr != '\0' && isspace((unsigned char)*endptr))
        endptr++;

    /* if there is any junk left at the end of the string, bail out. if can_ignore == true, discard junk and continue */
    if (*endptr != '\0' && !fcinfo->can_ignore)
        ereport(ERROR,
            (errcode(ERRCODE_INVALID_TEXT_REPRESENTATION),
                errmsg("invalid input syntax for type real: \"%s\"", orig_num)));

    /*
     * if we get here, we have a legal double, still need to check to see if
     * it's a legal float4
     */
    if (fcinfo->can_ignore) {
        if (isinf((float4)val) && !isinf(val)) {
            ereport(WARNING, (errmsg("value out of range: overflow")));
            PG_RETURN_FLOAT4(val < 0 ? -FLT_MAX : FLT_MAX);
        }
        if (((float4)val) == 0.0 && val != 0) {
            ereport(WARNING, (errmsg("value out of range: underflow")));
            PG_RETURN_FLOAT4(0);
        }
        if (*endptr != '\0') {
            ereport(WARNING,
                    (errcode(ERRCODE_INVALID_TEXT_REPRESENTATION),
                     errmsg("invalid input syntax for type real: \"%s\". truncated automatically", orig_num)));
        }
        PG_RETURN_FLOAT4((float4)val);
    }
    CHECKFLOATVAL((float4)val, isinf(val), val == 0);

    PG_RETURN_FLOAT4((float4)val);
}

/*
 *		float4out		- converts a float4 number to a string
 *						  using a standard output format
 */
Datum float4out(PG_FUNCTION_ARGS)
{
    float4 num = PG_GETARG_FLOAT4(0);
    char* ascii = (char*)palloc(MAXFLOATWIDTH + 1);
    errno_t rc = EOK;

    if (isnan(num)) {
        rc = strcpy_s(ascii, MAXFLOATWIDTH + 1, "NaN");
        securec_check_ss(rc, "\0", "\0");
        PG_RETURN_CSTRING(ascii);
    }

    switch (is_infinite(num)) {
        case 1:
            rc = strcpy_s(ascii, MAXFLOATWIDTH + 1, "Infinity");
            securec_check_ss(rc, "\0", "\0");
            break;
        case -1:
            rc = strcpy_s(ascii, MAXFLOATWIDTH + 1, "-Infinity");
            securec_check_ss(rc, "\0", "\0");
            break;
        default: {
            int ndig = FLT_DIG + u_sess->attr.attr_common.extra_float_digits;

            if (ndig < 1)
                ndig = 1;

            rc = snprintf_s(ascii, MAXFLOATWIDTH + 1, MAXFLOATWIDTH, "%.*g", ndig, num);
            securec_check_ss(rc, "\0", "\0");
        } break;
    }
    if (DISPLAY_LEADING_ZERO) {
        PG_RETURN_CSTRING(ascii);
    }

    if (!((num > 0 && num < 1) || (num > -1 && num < 0))) {
        PG_RETURN_CSTRING(ascii);
    }
    // Delete 0 before decimal.
    // For Example: convert 0.123 to .123, or -0.123 to -.123
    else {
        char* final_ascii = (char*)palloc0(MAXFLOATWIDTH + 1);
        char* ascii_copy = ascii;

        if ('-' == *ascii_copy) {
            *final_ascii = *ascii_copy++;
        }

        // Skip 0 before decimal
        if ('0' == *ascii_copy) {
            ascii_copy++;
        }

        rc = strncat_s(final_ascii, MAXFLOATWIDTH + 1, ascii_copy, strlen(ascii_copy));
        securec_check(rc, "\0", "\0");

        pfree_ext(ascii);

        PG_RETURN_CSTRING(final_ascii);
    }
}

/*
 *		float4recv			- converts external binary format to float4
 */
Datum float4recv(PG_FUNCTION_ARGS)
{
    StringInfo buf = (StringInfo)PG_GETARG_POINTER(0);

    PG_RETURN_FLOAT4(pq_getmsgfloat4(buf));
}

/*
 *		float4send			- converts float4 to binary format
 */
Datum float4send(PG_FUNCTION_ARGS)
{
    float4 num = PG_GETARG_FLOAT4(0);
    StringInfoData buf;

    pq_begintypsend(&buf);
    pq_sendfloat4(&buf, num);
    PG_RETURN_BYTEA_P(pq_endtypsend(&buf));
}

/*
 *		float8in		- converts "num" to float8
 *						  restricted syntax:
 *						  {<sp>} [+|-] {digit} [.{digit}] [<exp>]
 *						  where <sp> is a space, digit is 0-9,
 *						  <exp> is "e" or "E" followed by an integer.
 */
Datum float8in(PG_FUNCTION_ARGS)
{
    char* num = PG_GETARG_CSTRING(0);
    char* orig_num = NULL;
    double val;
    char* endptr = NULL;

    /*
     * endptr points to the first character _after_ the sequence we recognized
     * as a valid floating point number. orig_num points to the original input
     * string.
     */
    orig_num = num;

    /*
     * Check for an empty-string input to begin with, to avoid the vagaries of
     * strtod() on different platforms.
     */
    if (*num == '\0') {
        if (fcinfo->can_ignore) {
            ereport(WARNING,
                    (errcode(ERRCODE_INVALID_TEXT_REPRESENTATION),
                     errmsg("invalid input syntax for type double precision: \"%s\". truncated automatically", orig_num)));
            PG_RETURN_FLOAT8(0);
        }
        ereport(ERROR,
            (errcode(ERRCODE_INVALID_TEXT_REPRESENTATION),
                errmsg("invalid input syntax for type double precision: \"%s\"", orig_num)));
    }

    /* skip leading whitespace */
    while (*num != '\0' && isspace((unsigned char)*num))
        num++;

    errno = 0;
    val = strtod(num, &endptr);

    /* change -0 to 0 */
    if (*num == '-' && val == 0.0) {
        val += 0.0;
    }

    /* did we not see anything that looks like a double? */
    if (endptr == num || errno != 0) {
        int save_errno = errno;

        /*
         * C99 requires that strtod() accept NaN and [-]Infinity, but not all
         * platforms support that yet (and some accept them but set ERANGE
         * anyway...)  Therefore, we check for these inputs ourselves.
         */
        if (pg_strncasecmp(num, "NaN", 3) == 0) {
            val = get_float8_nan();
            endptr = num + 3;
        } else if (pg_strncasecmp(num, "Infinity", 8) == 0) {
            val = get_float8_infinity();
            endptr = num + 8;
        } else if (pg_strncasecmp(num, "-Infinity", 9) == 0) {
            val = -get_float8_infinity();
            endptr = num + 9;
        } else if (save_errno == ERANGE) {
            /*
             * Some platforms return ERANGE for denormalized numbers (those
             * that are not zero, but are too close to zero to have full
             * precision).	We'd prefer not to throw error for that, so try to
             * detect whether it's a "real" out-of-range condition by checking
             * to see if the result is zero or huge.
             */
            if (val == 0.0 || val >= HUGE_VAL || val <= -HUGE_VAL) {
                ereport(fcinfo->can_ignore ? WARNING : ERROR,
                        (errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
                         errmsg("\"%s\" is out of range for type double precision", orig_num)));
                val = (val == 0.0 ? 0 : (val >= HUGE_VAL ? DBL_MAX : DBL_MIN));
            }
        } else if (!fcinfo->can_ignore)
            ereport(ERROR,
                (errcode(ERRCODE_INVALID_TEXT_REPRESENTATION),
                    errmsg("invalid input syntax for type double precision: \"%s\"", orig_num)));
    }
#ifdef HAVE_BUGGY_SOLARIS_STRTOD
    else {
        /*
         * Many versions of Solaris have a bug wherein strtod sets endptr to
         * point one byte beyond the end of the string when given "inf" or
         * "infinity".
         */
        if (endptr != num && endptr[-1] == '\0')
            endptr--;
    }
#endif /* HAVE_BUGGY_SOLARIS_STRTOD */

#ifdef HAVE_BUGGY_IRIX_STRTOD

    /*
     * In some IRIX versions, strtod() recognizes only "inf", so if the input
     * is "infinity" we have to skip over "inity".	Also, it may return
     * positive infinity for "-inf".
     */
    if (isinf(val)) {
        if (pg_strncasecmp(num, "Infinity", 8) == 0) {
            val = get_float8_infinity();
            endptr = num + 8;
        } else if (pg_strncasecmp(num, "-Infinity", 9) == 0) {
            val = -get_float8_infinity();
            endptr = num + 9;
        } else if (pg_strncasecmp(num, "-inf", 4) == 0) {
            val = -get_float8_infinity();
            endptr = num + 4;
        }
    }
#endif /* HAVE_BUGGY_IRIX_STRTOD */

    /* skip trailing whitespace */
    while (*endptr != '\0' && isspace((unsigned char)*endptr))
        endptr++;

    /* if there is any junk left at the end of the string, bail out. if can_ignore == true, discard junk and continue. */
    if (*endptr != '\0') {
        if (fcinfo->can_ignore) {
            ereport(WARNING,
                    (errcode(ERRCODE_INVALID_TEXT_REPRESENTATION),
                     errmsg("invalid input syntax for type double precision: \"%s\". truncated automatically", orig_num)));
        } else {
            ereport(ERROR,
                    (errcode(ERRCODE_INVALID_TEXT_REPRESENTATION),
                     errmsg("invalid input syntax for type double precision: \"%s\"", orig_num)));
        }
    }

    CHECKFLOATVAL(val, true, true);

    PG_RETURN_FLOAT8(val);
}

/*
 *		float8out		- converts float8 number to a string
 *						  using a standard output format
 */
Datum float8out(PG_FUNCTION_ARGS)
{
    float8 num = PG_GETARG_FLOAT8(0);
    char* ascii = (char*)palloc(MAXDOUBLEWIDTH + 1);
    errno_t rc = EOK;

    if (isnan(num)) {
        rc = strcpy_s(ascii, MAXDOUBLEWIDTH + 1, "NaN");
        securec_check(rc, "\0", "\0");
        PG_RETURN_CSTRING(ascii);
    }
    switch (is_infinite(num)) {
        case 1:
            rc = strcpy_s(ascii, MAXDOUBLEWIDTH + 1, "Infinity");
            securec_check(rc, "\0", "\0");
            break;
        case -1:
            rc = strcpy_s(ascii, MAXDOUBLEWIDTH + 1, "-Infinity");
            securec_check(rc, "\0", "\0");
            break;
        default: {
            int ndig = DBL_DIG + u_sess->attr.attr_common.extra_float_digits;

            if (ndig < 1)
                ndig = 1;

            rc = snprintf_s(ascii, MAXDOUBLEWIDTH + 1, MAXDOUBLEWIDTH, "%.*g", ndig, num);
            securec_check_ss(rc, "\0", "\0");
        } break;
    }
    if (DISPLAY_LEADING_ZERO) {
        PG_RETURN_CSTRING(ascii);
    }

    if (!((num > 0 && num < 1) || (num > -1 && num < 0))) {
        PG_RETURN_CSTRING(ascii);
    }
    // Delete 0 before decimal.
    // For Example: convert 0.123 to .123, or -0.123 to -.123
    else {
        char* final_ascii = (char*)palloc0(MAXFLOATWIDTH + 1);
        char* ascii_copy = ascii;

        if ('-' == *ascii_copy) {
            *final_ascii = *ascii_copy++;
        }

        /* Skip 0 before decimal */
        if ('0' == *ascii_copy) {
            ascii_copy++;
        }

        rc = strncat_s(final_ascii, MAXFLOATWIDTH + 1, ascii_copy, strlen(ascii_copy));
        securec_check(rc, "\0", "\0");

        pfree_ext(ascii);

        PG_RETURN_CSTRING(final_ascii);
    }
}

/*
 *		float8recv			- converts external binary format to float8
 */
Datum float8recv(PG_FUNCTION_ARGS)
{
    StringInfo buf = (StringInfo)PG_GETARG_POINTER(0);

    PG_RETURN_FLOAT8(pq_getmsgfloat8(buf));
}

/*
 *		float8send			- converts float8 to binary format
 */
Datum float8send(PG_FUNCTION_ARGS)
{
    float8 num = PG_GETARG_FLOAT8(0);
    StringInfoData buf;

    pq_begintypsend(&buf);
    pq_sendfloat8(&buf, num);
    PG_RETURN_BYTEA_P(pq_endtypsend(&buf));
}

/* ========== PUBLIC ROUTINES ========== */

/*
 *		======================
 *		FLOAT4 BASE OPERATIONS
 *		======================
 */

/*
 *		float4abs		- returns |arg1| (absolute value)
 */
Datum float4abs(PG_FUNCTION_ARGS)
{
    float4 arg1 = PG_GETARG_FLOAT4(0);

    PG_RETURN_FLOAT4((float4)fabs(arg1));
}

/*
 *		float4um		- returns -arg1 (unary minus)
 */
Datum float4um(PG_FUNCTION_ARGS)
{
    float4 arg1 = PG_GETARG_FLOAT4(0);
    float4 result;

    if (arg1 == 0.0)
        PG_RETURN_FLOAT4(0);

    result = -arg1;
    PG_RETURN_FLOAT4(result);
}

Datum float4up(PG_FUNCTION_ARGS)
{
    float4 arg = PG_GETARG_FLOAT4(0);

    PG_RETURN_FLOAT4(arg);
}

Datum float4larger(PG_FUNCTION_ARGS)
{
    float4 arg1 = PG_GETARG_FLOAT4(0);
    float4 arg2 = PG_GETARG_FLOAT4(1);
    float4 result;

    if (float4_cmp_internal(arg1, arg2) > 0)
        result = arg1;
    else
        result = arg2;
    PG_RETURN_FLOAT4(result);
}

Datum float4smaller(PG_FUNCTION_ARGS)
{
    float4 arg1 = PG_GETARG_FLOAT4(0);
    float4 arg2 = PG_GETARG_FLOAT4(1);
    float4 result;

    if (float4_cmp_internal(arg1, arg2) < 0)
        result = arg1;
    else
        result = arg2;
    PG_RETURN_FLOAT4(result);
}

/*
 *		======================
 *		FLOAT8 BASE OPERATIONS
 *		======================
 */

/*
 *		float8abs		- returns |arg1| (absolute value)
 */
Datum float8abs(PG_FUNCTION_ARGS)
{
    float8 arg1 = PG_GETARG_FLOAT8(0);

    PG_RETURN_FLOAT8(fabs(arg1));
}

/*
 *		float8um		- returns -arg1 (unary minus)
 */
Datum float8um(PG_FUNCTION_ARGS)
{
    float8 arg1 = PG_GETARG_FLOAT8(0);
    float8 result;

    if (arg1 == 0.0)
        PG_RETURN_FLOAT8(0);

    result = -arg1;
    PG_RETURN_FLOAT8(result);
}

Datum float8up(PG_FUNCTION_ARGS)
{
    float8 arg = PG_GETARG_FLOAT8(0);

    PG_RETURN_FLOAT8(arg);
}

Datum float8larger(PG_FUNCTION_ARGS)
{
    float8 arg1 = PG_GETARG_FLOAT8(0);
    float8 arg2 = PG_GETARG_FLOAT8(1);
    float8 result;

    if (float8_cmp_internal(arg1, arg2) > 0)
        result = arg1;
    else
        result = arg2;
    PG_RETURN_FLOAT8(result);
}

Datum float8smaller(PG_FUNCTION_ARGS)
{
    float8 arg1 = PG_GETARG_FLOAT8(0);
    float8 arg2 = PG_GETARG_FLOAT8(1);
    float8 result;

    if (float8_cmp_internal(arg1, arg2) < 0)
        result = arg1;
    else
        result = arg2;
    PG_RETURN_FLOAT8(result);
}

/*
 *		====================
 *		ARITHMETIC OPERATORS
 *		====================
 */

/*
 *		float4pl		- returns arg1 + arg2
 *		float4mi		- returns arg1 - arg2
 *		float4mul		- returns arg1 * arg2
 *		float4div		- returns arg1 / arg2
 */
Datum float4pl(PG_FUNCTION_ARGS)
{
    float4 arg1 = PG_GETARG_FLOAT4(0);
    float4 arg2 = PG_GETARG_FLOAT4(1);
    float4 result;

    result = arg1 + arg2;

    /*
     * There isn't any way to check for underflow of addition/subtraction
     * because numbers near the underflow value have already been rounded to
     * the point where we can't detect that the two values were originally
     * different, e.g. on x86, '1e-45'::float4 == '2e-45'::float4 ==
     * 1.4013e-45.
     */
    CHECKFLOATVAL(result, isinf(arg1) || isinf(arg2), true);
    PG_RETURN_FLOAT4(result);
}

Datum float4mi(PG_FUNCTION_ARGS)
{
    float4 arg1 = PG_GETARG_FLOAT4(0);
    float4 arg2 = PG_GETARG_FLOAT4(1);
    float4 result;

    result = arg1 - arg2;
    CHECKFLOATVAL(result, isinf(arg1) || isinf(arg2), true);
    PG_RETURN_FLOAT4(result);
}

Datum float4mul(PG_FUNCTION_ARGS)
{
    float4 arg1 = PG_GETARG_FLOAT4(0);
    float4 arg2 = PG_GETARG_FLOAT4(1);
    float4 result;

    if (arg1 == 0.0 || arg2 == 0.0)
        PG_RETURN_FLOAT4(0);

    result = arg1 * arg2;
    CHECKFLOATVAL(result, isinf(arg1) || isinf(arg2), arg1 == 0 || arg2 == 0);
    PG_RETURN_FLOAT4(result);
}

Datum float4div(PG_FUNCTION_ARGS)
{
    float4 arg1 = PG_GETARG_FLOAT4(0);
    float4 arg2 = PG_GETARG_FLOAT4(1);
    float4 result;

    if (arg2 == 0.0)
        ereport(ERROR, (errcode(ERRCODE_DIVISION_BY_ZERO), errmsg("division by zero")));

    if (arg1 == 0.0)
        PG_RETURN_FLOAT4(0);

    result = arg1 / arg2;

    CHECKFLOATVAL(result, isinf(arg1) || isinf(arg2), arg1 == 0);
    PG_RETURN_FLOAT4(result);
}

/*
 *		float8pl		- returns arg1 + arg2
 *		float8mi		- returns arg1 - arg2
 *		float8mul		- returns arg1 * arg2
 *		float8div		- returns arg1 / arg2
 */
Datum float8pl(PG_FUNCTION_ARGS)
{
    float8 arg1 = PG_GETARG_FLOAT8(0);
    float8 arg2 = PG_GETARG_FLOAT8(1);
    float8 result;

    result = arg1 + arg2;

    CHECKFLOATVAL(result, isinf(arg1) || isinf(arg2), true);
    PG_RETURN_FLOAT8(result);
}

Datum float8mi(PG_FUNCTION_ARGS)
{
    float8 arg1 = PG_GETARG_FLOAT8(0);
    float8 arg2 = PG_GETARG_FLOAT8(1);
    float8 result;

    result = arg1 - arg2;

    CHECKFLOATVAL(result, isinf(arg1) || isinf(arg2), true);
    PG_RETURN_FLOAT8(result);
}

Datum float8mul(PG_FUNCTION_ARGS)
{
    float8 arg1 = PG_GETARG_FLOAT8(0);
    float8 arg2 = PG_GETARG_FLOAT8(1);
    float8 result;

    if (arg1 == 0.0 || arg2 == 0.0)
        PG_RETURN_FLOAT8(0);

    result = arg1 * arg2;

    CHECKFLOATVAL(result, isinf(arg1) || isinf(arg2), arg1 == 0 || arg2 == 0);
    PG_RETURN_FLOAT8(result);
}

Datum float8div(PG_FUNCTION_ARGS)
{
    float8 arg1 = PG_GETARG_FLOAT8(0);
    float8 arg2 = PG_GETARG_FLOAT8(1);
    float8 result;

    if (arg2 == 0.0)
        ereport(ERROR, (errcode(ERRCODE_DIVISION_BY_ZERO), errmsg("division by zero")));

    if (arg1 == 0.0)
        PG_RETURN_FLOAT8(0);

    result = arg1 / arg2;

    CHECKFLOATVAL(result, isinf(arg1) || isinf(arg2), arg1 == 0);
    PG_RETURN_FLOAT8(result);
}

/*
 *		====================
 *		COMPARISON OPERATORS
 *		====================
 */

/*
 *		float4{eq,ne,lt,le,gt,ge}		- float4/float4 comparison operations
 */
static int float4_cmp_internal(float4 a, float4 b)
{
    /*
     * We consider all NANs to be equal and larger than any non-NAN. This is
     * somewhat arbitrary; the important thing is to have a consistent sort
     * order.
     */
    if (isnan(a)) {
        if (isnan(b))
            return 0; /* NAN = NAN */
        else
            return 1; /* NAN > non-NAN */
    } else if (isnan(b)) {
        return -1; /* non-NAN < NAN */
    } else {
        if (a > b)
            return 1;
        else if (a < b)
            return -1;
        else
            return 0;
    }
}

Datum float4eq(PG_FUNCTION_ARGS)
{
    float4 arg1 = PG_GETARG_FLOAT4(0);
    float4 arg2 = PG_GETARG_FLOAT4(1);

    PG_RETURN_BOOL(float4_cmp_internal(arg1, arg2) == 0);
}

Datum float4ne(PG_FUNCTION_ARGS)
{
    float4 arg1 = PG_GETARG_FLOAT4(0);
    float4 arg2 = PG_GETARG_FLOAT4(1);

    PG_RETURN_BOOL(float4_cmp_internal(arg1, arg2) != 0);
}

Datum float4lt(PG_FUNCTION_ARGS)
{
    float4 arg1 = PG_GETARG_FLOAT4(0);
    float4 arg2 = PG_GETARG_FLOAT4(1);

    PG_RETURN_BOOL(float4_cmp_internal(arg1, arg2) < 0);
}

Datum float4le(PG_FUNCTION_ARGS)
{
    float4 arg1 = PG_GETARG_FLOAT4(0);
    float4 arg2 = PG_GETARG_FLOAT4(1);

    PG_RETURN_BOOL(float4_cmp_internal(arg1, arg2) <= 0);
}

Datum float4gt(PG_FUNCTION_ARGS)
{
    float4 arg1 = PG_GETARG_FLOAT4(0);
    float4 arg2 = PG_GETARG_FLOAT4(1);

    PG_RETURN_BOOL(float4_cmp_internal(arg1, arg2) > 0);
}

Datum float4ge(PG_FUNCTION_ARGS)
{
    float4 arg1 = PG_GETARG_FLOAT4(0);
    float4 arg2 = PG_GETARG_FLOAT4(1);

    PG_RETURN_BOOL(float4_cmp_internal(arg1, arg2) >= 0);
}

Datum btfloat4cmp(PG_FUNCTION_ARGS)
{
    float4 arg1 = PG_GETARG_FLOAT4(0);
    float4 arg2 = PG_GETARG_FLOAT4(1);

    PG_RETURN_INT32(float4_cmp_internal(arg1, arg2));
}

static int btfloat4fastcmp(Datum x, Datum y, SortSupport ssup)
{
    float4 arg1 = DatumGetFloat4(x);
    float4 arg2 = DatumGetFloat4(y);

    return float4_cmp_internal(arg1, arg2);
}

Datum btfloat4sortsupport(PG_FUNCTION_ARGS)
{
    SortSupport ssup = (SortSupport)PG_GETARG_POINTER(0);

    ssup->comparator = btfloat4fastcmp;
    PG_RETURN_VOID();
}

/*
 *		float8{eq,ne,lt,le,gt,ge}		- float8/float8 comparison operations
 */
int float8_cmp_internal(float8 a, float8 b)
{
    /*
     * We consider all NANs to be equal and larger than any non-NAN. This is
     * somewhat arbitrary; the important thing is to have a consistent sort
     * order.
     */
    if (isnan(a)) {
        if (isnan(b))
            return 0; /* NAN = NAN */
        else
            return 1; /* NAN > non-NAN */
    } else if (isnan(b)) {
        return -1; /* non-NAN < NAN */
    } else {
        if (a > b)
            return 1;
        else if (a < b)
            return -1;
        else
            return 0;
    }
}

Datum float8eq(PG_FUNCTION_ARGS)
{
    float8 arg1 = PG_GETARG_FLOAT8(0);
    float8 arg2 = PG_GETARG_FLOAT8(1);

    PG_RETURN_BOOL(float8_cmp_internal(arg1, arg2) == 0);
}

Datum float8ne(PG_FUNCTION_ARGS)
{
    float8 arg1 = PG_GETARG_FLOAT8(0);
    float8 arg2 = PG_GETARG_FLOAT8(1);

    PG_RETURN_BOOL(float8_cmp_internal(arg1, arg2) != 0);
}

Datum float8lt(PG_FUNCTION_ARGS)
{
    float8 arg1 = PG_GETARG_FLOAT8(0);
    float8 arg2 = PG_GETARG_FLOAT8(1);

    PG_RETURN_BOOL(float8_cmp_internal(arg1, arg2) < 0);
}

Datum float8le(PG_FUNCTION_ARGS)
{
    float8 arg1 = PG_GETARG_FLOAT8(0);
    float8 arg2 = PG_GETARG_FLOAT8(1);

    PG_RETURN_BOOL(float8_cmp_internal(arg1, arg2) <= 0);
}

Datum float8gt(PG_FUNCTION_ARGS)
{
    float8 arg1 = PG_GETARG_FLOAT8(0);
    float8 arg2 = PG_GETARG_FLOAT8(1);

    PG_RETURN_BOOL(float8_cmp_internal(arg1, arg2) > 0);
}

Datum float8ge(PG_FUNCTION_ARGS)
{
    float8 arg1 = PG_GETARG_FLOAT8(0);
    float8 arg2 = PG_GETARG_FLOAT8(1);

    PG_RETURN_BOOL(float8_cmp_internal(arg1, arg2) >= 0);
}

Datum btfloat8cmp(PG_FUNCTION_ARGS)
{
    float8 arg1 = PG_GETARG_FLOAT8(0);
    float8 arg2 = PG_GETARG_FLOAT8(1);

    PG_RETURN_INT32(float8_cmp_internal(arg1, arg2));
}

static int btfloat8fastcmp(Datum x, Datum y, SortSupport ssup)
{
    float8 arg1 = DatumGetFloat8(x);
    float8 arg2 = DatumGetFloat8(y);

    return float8_cmp_internal(arg1, arg2);
}

Datum btfloat8sortsupport(PG_FUNCTION_ARGS)
{
    SortSupport ssup = (SortSupport)PG_GETARG_POINTER(0);

    ssup->comparator = btfloat8fastcmp;
    PG_RETURN_VOID();
}

Datum btfloat48cmp(PG_FUNCTION_ARGS)
{
    float4 arg1 = PG_GETARG_FLOAT4(0);
    float8 arg2 = PG_GETARG_FLOAT8(1);

    /* widen float4 to float8 and then compare */
    PG_RETURN_INT32(float8_cmp_internal(arg1, arg2));
}

Datum btfloat84cmp(PG_FUNCTION_ARGS)
{
    float8 arg1 = PG_GETARG_FLOAT8(0);
    float4 arg2 = PG_GETARG_FLOAT4(1);

    /* widen float4 to float8 and then compare */
    PG_RETURN_INT32(float8_cmp_internal(arg1, arg2));
}

/*
 *		===================
 *		CONVERSION ROUTINES
 *		===================
 */

/*
 *		ftod			- converts a float4 number to a float8 number
 */
Datum ftod(PG_FUNCTION_ARGS)
{
    float4 num = PG_GETARG_FLOAT4(0);

    PG_RETURN_FLOAT8((float8)num);
}

/*
 *		dtof			- converts a float8 number to a float4 number
 */
Datum dtof(PG_FUNCTION_ARGS)
{
    float8 num = PG_GETARG_FLOAT8(0);

    if (fcinfo->can_ignore) {
        if (isinf((float4)num) && !isinf(num)) {
            ereport(WARNING, (errmsg("value out of range: overflow")));
            PG_RETURN_FLOAT4(num < 0 ? -FLT_MAX : FLT_MAX);
        }
        if (((float4)num) == 0.0 && num != 0) {
            ereport(WARNING, (errmsg("value out of range: underflow")));
            PG_RETURN_FLOAT4(0);
        }
        PG_RETURN_FLOAT4((float4)num);
    }

    CHECKFLOATVAL((float4)num, isinf(num), num == 0);

    PG_RETURN_FLOAT4((float4)num);
}

/*
 *		dtoi4			- converts a float8 number to an int4 number
 */
Datum dtoi4(PG_FUNCTION_ARGS)
{
    float8 num = PG_GETARG_FLOAT8(0);

    /*
     * Get rid of any fractional part in the input.  This is so we don't fail
     * on just-out-of-range values that would round into range.  Note
     * assumption that rint() will pass through a NaN or Inf unchanged.
     */
    num = rint(num);

    /*
     * Range check.  We must be careful here that the boundary values are
     * expressed exactly in the float domain.  We expect PG_INT32_MIN to be an
     * exact power of 2, so it will be represented exactly; but PG_INT32_MAX
     * isn't, and might get rounded off, so avoid using it.
     */
    if (num < (float8)PG_INT32_MIN || num >= -((float8)PG_INT32_MIN) || isnan(num)) {
        if (fcinfo->can_ignore && !isnan(num)) {
            ereport(WARNING, (errmsg("integer out of range")));
            PG_RETURN_INT32(num < (float8)PG_INT32_MIN ? INT_MIN : INT_MAX);
        }
        ereport(ERROR, (errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE), errmsg("integer out of range")));
    }

    PG_RETURN_INT32((int32)num);
}

/*
 *		dtoi2			- converts a float8 number to an int2 number
 */
Datum dtoi2(PG_FUNCTION_ARGS)
{
    float8 num = PG_GETARG_FLOAT8(0);

    /*
     * Get rid of any fractional part in the input.  This is so we don't fail
     * on just-out-of-range values that would round into range.  Note
     * assumption that rint() will pass through a NaN or Inf unchanged.
     */
    num = rint(num);

    /*
     * Range check.  We must be careful here that the boundary values are
     * expressed exactly in the float domain.  We expect PG_INT16_MIN  to be an
     * exact power of 2, so it will be represented exactly; but PG_INT16_MAX
     * isn't, and might get rounded off, so avoid using it.
     */
    if (num < (float8)PG_INT16_MIN || num >= -((float8)PG_INT16_MIN) || isnan(num)) {
        if (fcinfo->can_ignore && !isnan(num)) {
            ereport(WARNING, (errmsg("smallint out of range")));
            PG_RETURN_INT16(num < (float8)PG_INT16_MIN ? SHRT_MIN : SHRT_MAX);
        }
        ereport(ERROR, (errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE), errmsg("smallint out of range")));
    }

    PG_RETURN_INT16((int16)num);
}

/*
 *		i4tod			- converts an int4 number to a float8 number
 */
Datum i4tod(PG_FUNCTION_ARGS)
{
    int32 num = PG_GETARG_INT32(0);

    PG_RETURN_FLOAT8((float8)num);
}

/*
 *		i2tod			- converts an int2 number to a float8 number
 */
Datum i2tod(PG_FUNCTION_ARGS)
{
    int16 num = PG_GETARG_INT16(0);

    PG_RETURN_FLOAT8((float8)num);
}

/*
 *		ftoi4			- converts a float4 number to an int4 number
 */
Datum ftoi4(PG_FUNCTION_ARGS)
{
    float4 num = PG_GETARG_FLOAT4(0);

    /*
     * Get rid of any fractional part in the input.  This is so we don't fail
     * on just-out-of-range values that would round into range.  Note
     * assumption that rint() will pass through a NaN or Inf unchanged.
     */
    num = rint(num);

    /*
     * Range check.  We must be careful here that the boundary values are
     * expressed exactly in the float domain.  We expect PG_INT32_MIN to be an
     * exact power of 2, so it will be represented exactly; but PG_INT32_MAX
     * isn't, and might get rounded off, so avoid using it.
     */

    if (fcinfo->can_ignore && num < (float4)PG_INT32_MIN) {
        ereport(WARNING, (errmsg("integer out of range")));
        PG_RETURN_INT32((int32)INT_MIN);
    }

    if (fcinfo->can_ignore && num >= -((float4)PG_INT32_MIN)) {
        ereport(WARNING, (errmsg("integer out of range")));
        PG_RETURN_INT32((int32)INT_MAX);
    }

    if (num < (float4)PG_INT32_MIN || num >= -((float4)PG_INT32_MIN) || isnan(num))
        ereport(ERROR, (errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE), errmsg("integer out of range")));

    PG_RETURN_INT32((int32)num);
}

/*
 *		ftoi2			- converts a float4 number to an int2 number
 */
Datum ftoi2(PG_FUNCTION_ARGS)
{
    float4 num = PG_GETARG_FLOAT4(0);

    /*
     * Get rid of any fractional part in the input.  This is so we don't fail
     * on just-out-of-range values that would round into range.  Note
     * assumption that rint() will pass through a NaN or Inf unchanged.
     */
    num = rint(num);

    /*
     * Range check.  We must be careful here that the boundary values are
     * expressed exactly in the float domain.  We expect PG_INT16_MIN  to be an
     * exact power of 2, so it will be represented exactly; but PG_INT16_MAX
     * isn't, and might get rounded off, so avoid using it.
     */
    if (fcinfo->can_ignore && num < (float4)PG_INT16_MIN) {
        ereport(WARNING, (errmsg("smallint out of range")));
        PG_RETURN_INT16((int16)SHRT_MIN);
    }

    if (fcinfo->can_ignore && num >= -((float4)PG_INT16_MIN)) {
        ereport(WARNING, (errmsg("smallint out of range")));
        PG_RETURN_INT16((int16)SHRT_MAX);
    }

    if (num < (float4)PG_INT16_MIN || num >= -((float4)PG_INT16_MIN) || isnan(num))
        ereport(ERROR, (errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE), errmsg("smallint out of range")));

    PG_RETURN_INT16((int16)num);
}

/*
 *		i4tof			- converts an int4 number to a float4 number
 */
Datum i4tof(PG_FUNCTION_ARGS)
{
    int32 num = PG_GETARG_INT32(0);

    PG_RETURN_FLOAT4((float4)num);
}

/*
 *		i2tof			- converts an int2 number to a float4 number
 */
Datum i2tof(PG_FUNCTION_ARGS)
{
    int16 num = PG_GETARG_INT16(0);

    PG_RETURN_FLOAT4((float4)num);
}

/*
 *		=======================
 *		RANDOM FLOAT8 OPERATORS
 *		=======================
 */

/*
 *		dround			- returns	ROUND(arg1)
 */
Datum dround(PG_FUNCTION_ARGS)
{
    float8 arg1 = PG_GETARG_FLOAT8(0);

    PG_RETURN_FLOAT8(rint(arg1));
}

/*
 *		dceil			- returns the smallest integer greater than or
 *						  equal to the specified float
 */
Datum dceil(PG_FUNCTION_ARGS)
{
    float8 arg1 = PG_GETARG_FLOAT8(0);

    float8 result = ceil(arg1);
    if (DB_IS_CMPT(A_FORMAT) && -0.0 == result) {
        /* ceil function won't return -0 if compatible with O type database */
        result = 0.0;
    }

    PG_RETURN_FLOAT8(result);
}

/*
 *		dfloor			- returns the largest integer lesser than or
 *						  equal to the specified float
 */
Datum dfloor(PG_FUNCTION_ARGS)
{
    float8 arg1 = PG_GETARG_FLOAT8(0);

    PG_RETURN_FLOAT8(floor(arg1));
}

/*
 *		dsign			- returns -1 if the argument is less than 0, 0
 *						  if the argument is equal to 0, and 1 if the
 *						  argument is greater than zero.
 */
Datum dsign(PG_FUNCTION_ARGS)
{
    float8 arg1 = PG_GETARG_FLOAT8(0);
    float8 result;

    if (arg1 > 0)
        result = 1.0;
    else if (arg1 < 0)
        result = -1.0;
    else
        result = 0.0;

    PG_RETURN_FLOAT8(result);
}

/*
 *		dtrunc			- returns truncation-towards-zero of arg1,
 *						  arg1 >= 0 ... the greatest integer less
 *										than or equal to arg1
 *						  arg1 < 0	... the least integer greater
 *										than or equal to arg1
 */
Datum dtrunc(PG_FUNCTION_ARGS)
{
    float8 arg1 = PG_GETARG_FLOAT8(0);
    float8 result;

    if (arg1 >= 0)
        result = floor(arg1);
    else
        result = -floor(-arg1);

    PG_RETURN_FLOAT8(result);
}

/*
 *		dsqrt			- returns square root of arg1
 */
Datum dsqrt(PG_FUNCTION_ARGS)
{
    float8 arg1 = PG_GETARG_FLOAT8(0);
    float8 result;

    if (arg1 < 0)
        ereport(ERROR,
            (errcode(ERRCODE_INVALID_ARGUMENT_FOR_POWER_FUNCTION),
                errmsg("cannot take square root of a negative number")));

    result = sqrt(arg1);

    CHECKFLOATVAL(result, isinf(arg1), arg1 == 0);
    PG_RETURN_FLOAT8(result);
}

/*
 *		dcbrt			- returns cube root of arg1
 */
Datum dcbrt(PG_FUNCTION_ARGS)
{
    float8 arg1 = PG_GETARG_FLOAT8(0);
    float8 result;

    result = cbrt(arg1);
    CHECKFLOATVAL(result, isinf(arg1), arg1 == 0);
    PG_RETURN_FLOAT8(result);
}

/*
 *		dpow			- returns pow(arg1,arg2)
 */
Datum dpow(PG_FUNCTION_ARGS)
{
    float8 arg1 = PG_GETARG_FLOAT8(0);
    float8 arg2 = PG_GETARG_FLOAT8(1);
    float8 result;

    /*
     * The SQL spec requires that we emit a particular SQLSTATE error code for
     * certain error conditions.  Specifically, we don't return a
     * divide-by-zero error code for 0 ^ -1.
     */
    if (arg1 == 0 && arg2 < 0) {
        ereport(ERROR, (errcode(ERRCODE_INVALID_ARGUMENT_FOR_POWER_FUNCTION), 
            errmsg("zero raised to a negative power is undefined")));
    }

    if (arg1 < 0 && floor(arg2) != arg2) {
        ereport(ERROR, (errcode(ERRCODE_INVALID_ARGUMENT_FOR_POWER_FUNCTION),
            errmsg("a negative number raised to a non-integer power yields a complex result")));
    }
    /*
     * pow() sets errno only on some platforms, depending on whether it
     * follows _IEEE_, _POSIX_, _XOPEN_, or _SVID_, so we try to avoid using
     * errno.  However, some platform/CPU combinations return errno == EDOM
     * and result == NaN for negative arg1 and very large arg2 (they must be
     * using something different from our floor() test to decide it's
     * invalid).  Other platforms (HPPA) return errno == ERANGE and a large
     * (HUGE_VAL) but finite result to signal overflow.
     */
    errno = 0;
    result = pow(arg1, arg2);
    if (errno == EDOM && isnan(result)) {
        if ((fabs(arg1) > 1 && arg2 >= 0) || (fabs(arg1) < 1 && arg2 < 0)) {
            /* The sign of Inf is not significant in this case. */
            result = get_float8_infinity();
        } else if (fabs(arg1) != 1) {
            result = 0;
        } else {
            result = 1;
        }
    } else if (errno == ERANGE && result != 0 && !isinf(result)) {
        result = get_float8_infinity();
    }
    CHECKFLOATVAL(result, isinf(arg1) || isinf(arg2), arg1 == 0);
    PG_RETURN_FLOAT8(result);
}

/*
 *		dexp			- returns the exponential function of arg1
 */
Datum dexp(PG_FUNCTION_ARGS)
{
    float8 arg1 = PG_GETARG_FLOAT8(0);
    float8 result;

    errno = 0;
    result = exp(arg1);
    if (errno == ERANGE && result != 0 && !isinf(result))
        result = get_float8_infinity();

    CHECKFLOATVAL(result, isinf(arg1), false);
    PG_RETURN_FLOAT8(result);
}

/*
 *		dlog1			- returns the natural logarithm of arg1
 */
Datum dlog1(PG_FUNCTION_ARGS)
{
    float8 arg1 = PG_GETARG_FLOAT8(0);
    float8 result;

    /*
     * Emit particular SQLSTATE error codes for ln(). This is required by the
     * SQL standard.
     */
    if (arg1 == 0.0)
        ereport(ERROR, (errcode(ERRCODE_INVALID_ARGUMENT_FOR_LOG), errmsg("cannot take logarithm of zero")));
    if (arg1 < 0)
        ereport(
            ERROR, (errcode(ERRCODE_INVALID_ARGUMENT_FOR_LOG), errmsg("cannot take logarithm of a negative number")));

    result = log(arg1);

    CHECKFLOATVAL(result, isinf(arg1), arg1 == 1);
    PG_RETURN_FLOAT8(result);
}

/*
 *		dlog10			- returns the base 10 logarithm of arg1
 */
Datum dlog10(PG_FUNCTION_ARGS)
{
    float8 arg1 = PG_GETARG_FLOAT8(0);
    float8 result;

    /*
     * Emit particular SQLSTATE error codes for log(). The SQL spec doesn't
     * define log(), but it does define ln(), so it makes sense to emit the
     * same error code for an analogous error condition.
     */
    if (arg1 == 0.0)
        ereport(ERROR, (errcode(ERRCODE_INVALID_ARGUMENT_FOR_LOG), errmsg("cannot take logarithm of zero")));
    if (arg1 < 0)
        ereport(
            ERROR, (errcode(ERRCODE_INVALID_ARGUMENT_FOR_LOG), errmsg("cannot take logarithm of a negative number")));

    result = log10(arg1);

    CHECKFLOATVAL(result, isinf(arg1), arg1 == 1);
    PG_RETURN_FLOAT8(result);
}

/*
 *		dacos			- returns the arccos of arg1 (radians)
 */
Datum dacos(PG_FUNCTION_ARGS)
{
    float8 arg1 = PG_GETARG_FLOAT8(0);
    float8 result;

    /*
     * We use errno here because the trigonometric functions are cyclic and
     * hard to check for underflow.
     */
    errno = 0;
    result = acos(arg1);
    if (errno != 0)
        ereport(ERROR, (errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE), errmsg("input is out of range")));

    CHECKFLOATVAL(result, isinf(arg1), true);
    PG_RETURN_FLOAT8(result);
}

/*
 *		dasin			- returns the arcsin of arg1 (radians)
 */
Datum dasin(PG_FUNCTION_ARGS)
{
    float8 arg1 = PG_GETARG_FLOAT8(0);
    float8 result;

    errno = 0;
    result = asin(arg1);
    if (errno != 0)
        ereport(ERROR, (errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE), errmsg("input is out of range")));

    CHECKFLOATVAL(result, isinf(arg1), true);
    PG_RETURN_FLOAT8(result);
}

/*
 *		datan			- returns the arctan of arg1 (radians)
 */
Datum datan(PG_FUNCTION_ARGS)
{
    float8 arg1 = PG_GETARG_FLOAT8(0);
    float8 result;

    errno = 0;
    result = atan(arg1);
    if (errno != 0)
        ereport(ERROR, (errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE), errmsg("input is out of range")));

    CHECKFLOATVAL(result, isinf(arg1), true);
    PG_RETURN_FLOAT8(result);
}

/*
 *		atan2			- returns the arctan2 of arg1 (radians)
 */
Datum datan2(PG_FUNCTION_ARGS)
{
    float8 arg1 = PG_GETARG_FLOAT8(0);
    float8 arg2 = PG_GETARG_FLOAT8(1);
    float8 result;

    errno = 0;
    result = atan2(arg1, arg2);
    if (errno != 0)
        ereport(ERROR, (errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE), errmsg("input is out of range")));

    CHECKFLOATVAL(result, isinf(arg1) || isinf(arg2), true);
    PG_RETURN_FLOAT8(result);
}

/*
 *		dcos			- returns the cosine of arg1 (radians)
 */
Datum dcos(PG_FUNCTION_ARGS)
{
    float8 arg1 = PG_GETARG_FLOAT8(0);
    float8 result;

    if (isnan(arg1)) {
        PG_RETURN_FLOAT8(get_float8_nan());
    }

    errno = 0;
    result = cos(arg1);
    if (errno != 0 || isinf(arg1)) {
        ereport(ERROR, (errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE), errmsg("input is out of range")));
    }

    CHECKFLOATVAL(result, isinf(arg1), true);
    PG_RETURN_FLOAT8(result);
}

/*
 *		dcot			- returns the cotangent of arg1 (radians)
 */
Datum dcot(PG_FUNCTION_ARGS)
{
    float8 arg1 = PG_GETARG_FLOAT8(0);
    float8 result;

    if (isnan(arg1)) {
        PG_RETURN_FLOAT8(get_float8_nan());
    }
    errno = 0;
    result = tan(arg1);
    if (errno != 0 || isinf(arg1)) {
        ereport(ERROR, (errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE), errmsg("input is out of range")));
    }

    result = 1.0 / result;
    CHECKFLOATVAL(result, true, true);
    PG_RETURN_FLOAT8(result);
}

/*
 *		dsin			- returns the sine of arg1 (radians)
 */
Datum dsin(PG_FUNCTION_ARGS)
{
    float8 arg1 = PG_GETARG_FLOAT8(0);
    float8 result;

    if (isnan(arg1)) {
        PG_RETURN_FLOAT8(get_float8_nan());
    }

    errno = 0;
    result = sin(arg1);
    if (errno != 0 || isinf(arg1))
        ereport(ERROR, (errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE), errmsg("input is out of range")));

    CHECKFLOATVAL(result, isinf(arg1), true);
    PG_RETURN_FLOAT8(result);
}

/*
 *		dtan			- returns the tangent of arg1 (radians)
 */
Datum dtan(PG_FUNCTION_ARGS)
{
    float8 arg1 = PG_GETARG_FLOAT8(0);
    float8 result;

    if (isnan(arg1)) {
        PG_RETURN_FLOAT8(get_float8_nan());
    }

    errno = 0;
    result = tan(arg1);
    if (errno != 0 || isinf(arg1)) {
        ereport(ERROR, (errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE), errmsg("input is out of range")));
    }
    CHECKFLOATVAL(result, true, true);
    PG_RETURN_FLOAT8(result);
}

/*
 *		degrees		- returns degrees converted from radians
 */
Datum degrees(PG_FUNCTION_ARGS)
{
    float8 arg1 = PG_GETARG_FLOAT8(0);
    float8 result;

    result = arg1 * (180.0 / M_PI);

    CHECKFLOATVAL(result, isinf(arg1), arg1 == 0);
    PG_RETURN_FLOAT8(result);
}

/*
 *		dpi				- returns the constant PI
 */
Datum dpi(PG_FUNCTION_ARGS)
{
    PG_RETURN_FLOAT8(M_PI);
}

/*
 *		radians		- returns radians converted from degrees
 */
Datum radians(PG_FUNCTION_ARGS)
{
    float8 arg1 = PG_GETARG_FLOAT8(0);
    float8 result;

    result = arg1 * (M_PI / 180.0);

    CHECKFLOATVAL(result, isinf(arg1), arg1 == 0);
    PG_RETURN_FLOAT8(result);
}

/*
 *		drandom		- returns a random number
 */
Datum drandom(PG_FUNCTION_ARGS)
{
    float8 result;

    /* result [0.0 - 1.0) */
    result = (double)gs_random() / ((double)MAX_RANDOM_VALUE + 1);

    PG_RETURN_FLOAT8(result);
}

/*
 *		setseed		- set seed for the random number generator
 */
Datum setseed(PG_FUNCTION_ARGS)
{
    float8 seed = PG_GETARG_FLOAT8(0);
    int iseed;

    if (seed < -1 || seed > 1)
        ereport(ERROR,
            (errcode(ERRCODE_INVALID_PARAMETER_VALUE), errmsg("setseed parameter %f out of range [-1,1]", seed)));

    iseed = (int)(seed * MAX_RANDOM_VALUE);
    gs_srandom((unsigned int)iseed);
    u_sess->opt_cxt.is_randomfunc_shippable = false;

    PG_RETURN_VOID();
}

/*
 *		=========================
 *		FLOAT AGGREGATE OPERATORS
 *		=========================
 *
 *		float8_accum		- accumulate for AVG(), variance aggregates, etc.
 *		float4_accum		- same, but input data is float4
 *		float8_avg			- produce final result for float AVG()
 *		float8_var_samp		- produce final result for float VAR_SAMP()
 *		float8_var_pop		- produce final result for float VAR_POP()
 *		float8_stddev_samp	- produce final result for float STDDEV_SAMP()
 *		float8_stddev_pop	- produce final result for float STDDEV_POP()
 *
 * The transition datatype for all these aggregates is a 3-element array
 * of float8, holding the values N, sum(X), sum(X*X) in that order.
 *
 * Note that we represent N as a float to avoid having to build a special
 * datatype.  Given a reasonable floating-point implementation, there should
 * be no accuracy loss unless N exceeds 2 ^ 52 or so (by which time the
 * user will have doubtless lost interest anyway...)
 */

float8* check_float8_array(ArrayType* transarray, const char* caller, int n)
{
    /*
     * We expect the input to be an N-element float array; verify that. We
     * don't need to use deconstruct_array() since the array data is just
     * going to look like a C array of N float8 values.
     */
    if (ARR_NDIM(transarray) != 1 || ARR_DIMS(transarray)[0] != n || ARR_HASNULL(transarray) ||
        ARR_ELEMTYPE(transarray) != FLOAT8OID)
        ereport(
            ERROR, (errcode(ERRCODE_ARRAY_SUBSCRIPT_ERROR), errmsg("%s: expected %d-element float8 array", caller, n)));

    return (float8*)ARR_DATA_PTR(transarray);
}

Datum float8_accum(PG_FUNCTION_ARGS)
{
    ArrayType* transarray = PG_GETARG_ARRAYTYPE_P(0);
    float8 newval = PG_GETARG_FLOAT8(1);
    float8* transvalues = NULL;
    float8 N, sumX, sumX2;

    transvalues = check_float8_array(transarray, "float8_accum", 3);
    N = transvalues[0];
    sumX = transvalues[1];
    sumX2 = transvalues[2];

    N += 1.0;
    sumX += newval;
    CHECKFLOATVAL(sumX, isinf(transvalues[1]) || isinf(newval), true);
    //  fcinfo->context is a flag for distinguish avg and other agg
    if (fcinfo->context == NULL || fcinfo->context->type != (NodeTag)true) {
	    sumX2 += newval * newval;
	    CHECKFLOATVAL(sumX2, isinf(transvalues[2]) || isinf(newval), true);
    }

    /*
     * If we're invoked as an aggregate, we can cheat and modify our first
     * parameter in-place to reduce palloc overhead. Otherwise we construct a
     * new array with the updated transition data and return it.
     */
    if (AggCheckCallContext(fcinfo, NULL)) {
        transvalues[0] = N;
        transvalues[1] = sumX;
        transvalues[2] = sumX2;

        PG_RETURN_ARRAYTYPE_P(transarray);
    } else {
        Datum transdatums[3];
        ArrayType* result = NULL;

        transdatums[0] = Float8GetDatumFast(N);
        transdatums[1] = Float8GetDatumFast(sumX);
        transdatums[2] = Float8GetDatumFast(sumX2);

        result = construct_array(transdatums, 3, FLOAT8OID, sizeof(float8), FLOAT8PASSBYVAL, 'd');

        PG_RETURN_ARRAYTYPE_P(result);
    }
}

Datum float4_accum(PG_FUNCTION_ARGS)
{
    ArrayType* transarray = PG_GETARG_ARRAYTYPE_P(0);

    /* do computations as float8 */
    float8 newval = PG_GETARG_FLOAT4(1);
    float8* transvalues = NULL;
    float8 N, sumX, sumX2;

    transvalues = check_float8_array(transarray, "float4_accum", 3);
    N = transvalues[0];
    sumX = transvalues[1];
    sumX2 = transvalues[2];

    N += 1.0;
    sumX += newval;
    CHECKFLOATVAL(sumX, isinf(transvalues[1]) || isinf(newval), true);
    sumX2 += newval * newval;
    CHECKFLOATVAL(sumX2, isinf(transvalues[2]) || isinf(newval), true);

    /*
     * If we're invoked as an aggregate, we can cheat and modify our first
     * parameter in-place to reduce palloc overhead. Otherwise we construct a
     * new array with the updated transition data and return it.
     */
    if (AggCheckCallContext(fcinfo, NULL)) {
        transvalues[0] = N;
        transvalues[1] = sumX;
        transvalues[2] = sumX2;

        PG_RETURN_ARRAYTYPE_P(transarray);
    } else {
        Datum transdatums[3];
        ArrayType* result = NULL;

        transdatums[0] = Float8GetDatumFast(N);
        transdatums[1] = Float8GetDatumFast(sumX);
        transdatums[2] = Float8GetDatumFast(sumX2);

        result = construct_array(transdatums, 3, FLOAT8OID, sizeof(float8), FLOAT8PASSBYVAL, 'd');

        PG_RETURN_ARRAYTYPE_P(result);
    }
}

Datum float8_avg(PG_FUNCTION_ARGS)
{
    ArrayType* transarray = PG_GETARG_ARRAYTYPE_P(0);
    float8* transvalues = NULL;
    float8 N, sumX;

    transvalues = check_float8_array(transarray, "float8_avg", 3);
    N = transvalues[0];
    sumX = transvalues[1];
    /* ignore sumX2 */

    /* SQL92 defines AVG of no values to be NULL */
    if (N == 0.0)
        PG_RETURN_NULL();

    PG_RETURN_FLOAT8(sumX / N);
}

Datum float8_var_pop(PG_FUNCTION_ARGS)
{
    ArrayType* transarray = PG_GETARG_ARRAYTYPE_P(0);
    float8* transvalues = NULL;
    float8 N, sumX, sumX2, numerator;

    transvalues = check_float8_array(transarray, "float8_var_pop", 3);
    N = transvalues[0];
    sumX = transvalues[1];
    sumX2 = transvalues[2];

    /* Population variance is undefined when N is 0, so return NULL */
    if (N == 0.0)
        PG_RETURN_NULL();

    numerator = N * sumX2 - sumX * sumX;
    CHECKFLOATVAL(numerator, isinf(sumX2) || isinf(sumX), true);

    /* Watch out for roundoff error producing a negative numerator */
    if (numerator <= 0.0)
        PG_RETURN_FLOAT8(0.0);

    PG_RETURN_FLOAT8(numerator / (N * N));
}

Datum float8_var_samp(PG_FUNCTION_ARGS)
{
    ArrayType* transarray = PG_GETARG_ARRAYTYPE_P(0);
    float8* transvalues = NULL;
    float8 N, sumX, sumX2, numerator;

    transvalues = check_float8_array(transarray, "float8_var_samp", 3);
    N = transvalues[0];
    sumX = transvalues[1];
    sumX2 = transvalues[2];

    /* Sample variance is undefined when N is 0 or 1, so return NULL */
    if (N <= 1.0)
        PG_RETURN_NULL();

    numerator = N * sumX2 - sumX * sumX;
    CHECKFLOATVAL(numerator, isinf(sumX2) || isinf(sumX), true);

    /* Watch out for roundoff error producing a negative numerator */
    if (numerator <= 0.0)
        PG_RETURN_FLOAT8(0.0);

    PG_RETURN_FLOAT8(numerator / (N * (N - 1.0)));
}

Datum float8_stddev_pop(PG_FUNCTION_ARGS)
{
    ArrayType* transarray = PG_GETARG_ARRAYTYPE_P(0);
    float8* transvalues = NULL;
    float8 N, sumX, sumX2, numerator;

    transvalues = check_float8_array(transarray, "float8_stddev_pop", 3);
    N = transvalues[0];
    sumX = transvalues[1];
    sumX2 = transvalues[2];

    /* Population stddev is undefined when N is 0, so return NULL */
    if (N == 0.0)
        PG_RETURN_NULL();

    numerator = N * sumX2 - sumX * sumX;
    CHECKFLOATVAL(numerator, isinf(sumX2) || isinf(sumX), true);

    /* Watch out for roundoff error producing a negative numerator */
    if (numerator <= 0.0)
        PG_RETURN_FLOAT8(0.0);

    PG_RETURN_FLOAT8(sqrt(numerator / (N * N)));
}

Datum float8_stddev_samp(PG_FUNCTION_ARGS)
{
    ArrayType* transarray = PG_GETARG_ARRAYTYPE_P(0);
    float8* transvalues = NULL;
    float8 N, sumX, sumX2, numerator;

    transvalues = check_float8_array(transarray, "float8_stddev_samp", 3);
    N = transvalues[0];
    sumX = transvalues[1];
    sumX2 = transvalues[2];

    /* Sample stddev is undefined when N is 0 or 1, so return NULL */
    if (N <= 1.0)
        PG_RETURN_NULL();

    numerator = N * sumX2 - sumX * sumX;
    CHECKFLOATVAL(numerator, isinf(sumX2) || isinf(sumX), true);

    /* Watch out for roundoff error producing a negative numerator */
    if (numerator <= 0.0)
        PG_RETURN_FLOAT8(0.0);

    PG_RETURN_FLOAT8(sqrt(numerator / (N * (N - 1.0))));
}

/*
 *		=========================
 *		SQL2003 BINARY AGGREGATES
 *		=========================
 *
 * The transition datatype for all these aggregates is a 6-element array of
 * float8, holding the values N, sum(X), sum(X*X), sum(Y), sum(Y*Y), sum(X*Y)
 * in that order.  Note that Y is the first argument to the aggregates!
 *
 * It might seem attractive to optimize this by having multiple accumulator
 * functions that only calculate the sums actually needed.	But on most
 * modern machines, a couple of extra floating-point multiplies will be
 * insignificant compared to the other per-tuple overhead, so I've chosen
 * to minimize code space instead.
 */

Datum float8_regr_accum(PG_FUNCTION_ARGS)
{
    ArrayType* transarray = PG_GETARG_ARRAYTYPE_P(0);
    float8 newvalY = PG_GETARG_FLOAT8(1);
    float8 newvalX = PG_GETARG_FLOAT8(2);
    float8* transvalues = NULL;
    float8 N, sumX, sumX2, sumY, sumY2, sumXY;

    transvalues = check_float8_array(transarray, "float8_regr_accum", 6);
    N = transvalues[0];
    sumX = transvalues[1];
    sumX2 = transvalues[2];
    sumY = transvalues[3];
    sumY2 = transvalues[4];
    sumXY = transvalues[5];

    N += 1.0;
    sumX += newvalX;
    CHECKFLOATVAL(sumX, isinf(transvalues[1]) || isinf(newvalX), true);
    sumX2 += newvalX * newvalX;
    CHECKFLOATVAL(sumX2, isinf(transvalues[2]) || isinf(newvalX), true);
    sumY += newvalY;
    CHECKFLOATVAL(sumY, isinf(transvalues[3]) || isinf(newvalY), true);
    sumY2 += newvalY * newvalY;
    CHECKFLOATVAL(sumY2, isinf(transvalues[4]) || isinf(newvalY), true);
    sumXY += newvalX * newvalY;
    CHECKFLOATVAL(sumXY, isinf(transvalues[5]) || isinf(newvalX) || isinf(newvalY), true);

    /*
     * If we're invoked as an aggregate, we can cheat and modify our first
     * parameter in-place to reduce palloc overhead. Otherwise we construct a
     * new array with the updated transition data and return it.
     */
    if (AggCheckCallContext(fcinfo, NULL)) {
        transvalues[0] = N;
        transvalues[1] = sumX;
        transvalues[2] = sumX2;
        transvalues[3] = sumY;
        transvalues[4] = sumY2;
        transvalues[5] = sumXY;

        PG_RETURN_ARRAYTYPE_P(transarray);
    } else {
        Datum transdatums[6];
        ArrayType* result = NULL;

        transdatums[0] = Float8GetDatumFast(N);
        transdatums[1] = Float8GetDatumFast(sumX);
        transdatums[2] = Float8GetDatumFast(sumX2);
        transdatums[3] = Float8GetDatumFast(sumY);
        transdatums[4] = Float8GetDatumFast(sumY2);
        transdatums[5] = Float8GetDatumFast(sumXY);

        result = construct_array(transdatums, 6, FLOAT8OID, sizeof(float8), FLOAT8PASSBYVAL, 'd');

        PG_RETURN_ARRAYTYPE_P(result);
    }
}

Datum float8_regr_sxx(PG_FUNCTION_ARGS)
{
    ArrayType* transarray = PG_GETARG_ARRAYTYPE_P(0);
    float8* transvalues = NULL;
    float8 N, sumX, sumX2, numerator;

    transvalues = check_float8_array(transarray, "float8_regr_sxx", 6);
    N = transvalues[0];
    sumX = transvalues[1];
    sumX2 = transvalues[2];

    /* if N is 0 we should return NULL */
    if (N < 1.0)
        PG_RETURN_NULL();

    numerator = N * sumX2 - sumX * sumX;
    CHECKFLOATVAL(numerator, isinf(sumX2) || isinf(sumX), true);

    /* Watch out for roundoff error producing a negative numerator */
    if (numerator <= 0.0)
        PG_RETURN_FLOAT8(0.0);

    PG_RETURN_FLOAT8(numerator / N);
}

Datum float8_regr_syy(PG_FUNCTION_ARGS)
{
    ArrayType* transarray = PG_GETARG_ARRAYTYPE_P(0);
    float8* transvalues = NULL;
    float8 N, sumY, sumY2, numerator;

    transvalues = check_float8_array(transarray, "float8_regr_syy", 6);
    N = transvalues[0];
    sumY = transvalues[3];
    sumY2 = transvalues[4];

    /* if N is 0 we should return NULL */
    if (N < 1.0)
        PG_RETURN_NULL();

    numerator = N * sumY2 - sumY * sumY;
    CHECKFLOATVAL(numerator, isinf(sumY2) || isinf(sumY), true);

    /* Watch out for roundoff error producing a negative numerator */
    if (numerator <= 0.0)
        PG_RETURN_FLOAT8(0.0);

    PG_RETURN_FLOAT8(numerator / N);
}

Datum float8_regr_sxy(PG_FUNCTION_ARGS)
{
    ArrayType* transarray = PG_GETARG_ARRAYTYPE_P(0);
    float8* transvalues = NULL;
    float8 N, sumX, sumY, sumXY, numerator;

    transvalues = check_float8_array(transarray, "float8_regr_sxy", 6);
    N = transvalues[0];
    sumX = transvalues[1];
    sumY = transvalues[3];
    sumXY = transvalues[5];

    /* if N is 0 we should return NULL */
    if (N < 1.0)
        PG_RETURN_NULL();

    numerator = N * sumXY - sumX * sumY;
    CHECKFLOATVAL(numerator, isinf(sumXY) || isinf(sumX) || isinf(sumY), true);

    /* A negative result is valid here */

    PG_RETURN_FLOAT8(numerator / N);
}

Datum float8_regr_avgx(PG_FUNCTION_ARGS)
{
    ArrayType* transarray = PG_GETARG_ARRAYTYPE_P(0);
    float8* transvalues = NULL;
    float8 N, sumX;

    transvalues = check_float8_array(transarray, "float8_regr_avgx", 6);
    N = transvalues[0];
    sumX = transvalues[1];

    /* if N is 0 we should return NULL */
    if (N < 1.0)
        PG_RETURN_NULL();

    PG_RETURN_FLOAT8(sumX / N);
}

Datum float8_regr_avgy(PG_FUNCTION_ARGS)
{
    ArrayType* transarray = PG_GETARG_ARRAYTYPE_P(0);
    float8* transvalues = NULL;
    float8 N, sumY;

    transvalues = check_float8_array(transarray, "float8_regr_avgy", 6);
    N = transvalues[0];
    sumY = transvalues[3];

    /* if N is 0 we should return NULL */
    if (N < 1.0)
        PG_RETURN_NULL();

    PG_RETURN_FLOAT8(sumY / N);
}

Datum float8_covar_pop(PG_FUNCTION_ARGS)
{
    ArrayType* transarray = PG_GETARG_ARRAYTYPE_P(0);
    float8* transvalues = NULL;
    float8 N, sumX, sumY, sumXY, numerator;

    transvalues = check_float8_array(transarray, "float8_covar_pop", 6);
    N = transvalues[0];
    sumX = transvalues[1];
    sumY = transvalues[3];
    sumXY = transvalues[5];

    /* if N is 0 we should return NULL */
    if (N < 1.0)
        PG_RETURN_NULL();

    numerator = N * sumXY - sumX * sumY;
    CHECKFLOATVAL(numerator, isinf(sumXY) || isinf(sumX) || isinf(sumY), true);

    PG_RETURN_FLOAT8(numerator / (N * N));
}

Datum float8_covar_samp(PG_FUNCTION_ARGS)
{
    ArrayType* transarray = PG_GETARG_ARRAYTYPE_P(0);
    float8* transvalues = NULL;
    float8 N, sumX, sumY, sumXY, numerator;

    transvalues = check_float8_array(transarray, "float8_covar_samp", 6);
    N = transvalues[0];
    sumX = transvalues[1];
    sumY = transvalues[3];
    sumXY = transvalues[5];

    /* if N is <= 1 we should return NULL */
    if (N < 2.0)
        PG_RETURN_NULL();

    numerator = N * sumXY - sumX * sumY;
    CHECKFLOATVAL(numerator, isinf(sumXY) || isinf(sumX) || isinf(sumY), true);

    PG_RETURN_FLOAT8(numerator / (N * (N - 1.0)));
}

Datum float8_corr(PG_FUNCTION_ARGS)
{
    ArrayType* transarray = PG_GETARG_ARRAYTYPE_P(0);
    float8* transvalues = NULL;
    float8 N, sumX, sumX2, sumY, sumY2, sumXY, numeratorX, numeratorY, numeratorXY;

    transvalues = check_float8_array(transarray, "float8_corr", 6);
    N = transvalues[0];
    sumX = transvalues[1];
    sumX2 = transvalues[2];
    sumY = transvalues[3];
    sumY2 = transvalues[4];
    sumXY = transvalues[5];

    /* if N is 0 we should return NULL */
    if (N < 1.0)
        PG_RETURN_NULL();

    numeratorX = N * sumX2 - sumX * sumX;
    CHECKFLOATVAL(numeratorX, isinf(sumX2) || isinf(sumX), true);
    numeratorY = N * sumY2 - sumY * sumY;
    CHECKFLOATVAL(numeratorY, isinf(sumY2) || isinf(sumY), true);
    numeratorXY = N * sumXY - sumX * sumY;
    CHECKFLOATVAL(numeratorXY, isinf(sumXY) || isinf(sumX) || isinf(sumY), true);
    if (numeratorX <= 0 || numeratorY <= 0)
        PG_RETURN_NULL();

    PG_RETURN_FLOAT8(numeratorXY / sqrt(numeratorX * numeratorY));
}

Datum float8_regr_r2(PG_FUNCTION_ARGS)
{
    ArrayType* transarray = PG_GETARG_ARRAYTYPE_P(0);
    float8* transvalues = NULL;
    float8 N, sumX, sumX2, sumY, sumY2, sumXY, numeratorX, numeratorY, numeratorXY;

    transvalues = check_float8_array(transarray, "float8_regr_r2", 6);
    N = transvalues[0];
    sumX = transvalues[1];
    sumX2 = transvalues[2];
    sumY = transvalues[3];
    sumY2 = transvalues[4];
    sumXY = transvalues[5];

    /* if N is 0 we should return NULL */
    if (N < 1.0)
        PG_RETURN_NULL();

    numeratorX = N * sumX2 - sumX * sumX;
    CHECKFLOATVAL(numeratorX, isinf(sumX2) || isinf(sumX), true);
    numeratorY = N * sumY2 - sumY * sumY;
    CHECKFLOATVAL(numeratorY, isinf(sumY2) || isinf(sumY), true);
    numeratorXY = N * sumXY - sumX * sumY;
    CHECKFLOATVAL(numeratorXY, isinf(sumXY) || isinf(sumX) || isinf(sumY), true);
    if (numeratorX <= 0)
        PG_RETURN_NULL();
    /* per spec, horizontal line produces 1.0 */
    if (numeratorY <= 0)
        PG_RETURN_FLOAT8(1.0);

    PG_RETURN_FLOAT8((numeratorXY * numeratorXY) / (numeratorX * numeratorY));
}

Datum float8_regr_slope(PG_FUNCTION_ARGS)
{
    ArrayType* transarray = PG_GETARG_ARRAYTYPE_P(0);
    float8* transvalues = NULL;
    float8 N, sumX, sumX2, sumY, sumXY, numeratorX, numeratorXY;

    transvalues = check_float8_array(transarray, "float8_regr_slope", 6);
    N = transvalues[0];
    sumX = transvalues[1];
    sumX2 = transvalues[2];
    sumY = transvalues[3];
    sumXY = transvalues[5];

    /* if N is 0 we should return NULL */
    if (N < 1.0)
        PG_RETURN_NULL();

    numeratorX = N * sumX2 - sumX * sumX;
    CHECKFLOATVAL(numeratorX, isinf(sumX2) || isinf(sumX), true);
    numeratorXY = N * sumXY - sumX * sumY;
    CHECKFLOATVAL(numeratorXY, isinf(sumXY) || isinf(sumX) || isinf(sumY), true);
    if (numeratorX <= 0)
        PG_RETURN_NULL();

    PG_RETURN_FLOAT8(numeratorXY / numeratorX);
}

Datum float8_regr_intercept(PG_FUNCTION_ARGS)
{
    ArrayType* transarray = PG_GETARG_ARRAYTYPE_P(0);
    float8* transvalues = NULL;
    float8 N, sumX, sumX2, sumY, sumXY, numeratorX, numeratorXXY;

    transvalues = check_float8_array(transarray, "float8_regr_intercept", 6);
    N = transvalues[0];
    sumX = transvalues[1];
    sumX2 = transvalues[2];
    sumY = transvalues[3];
    sumXY = transvalues[5];

    /* if N is 0 we should return NULL */
    if (N < 1.0)
        PG_RETURN_NULL();

    numeratorX = N * sumX2 - sumX * sumX;
    CHECKFLOATVAL(numeratorX, isinf(sumX2) || isinf(sumX), true);
    numeratorXXY = sumY * sumX2 - sumX * sumXY;
    CHECKFLOATVAL(numeratorXXY, isinf(sumY) || isinf(sumX2) || isinf(sumX) || isinf(sumXY), true);
    if (numeratorX <= 0)
        PG_RETURN_NULL();

    PG_RETURN_FLOAT8(numeratorXXY / numeratorX);
}

/*
 *		====================================
 *		MIXED-PRECISION ARITHMETIC OPERATORS
 *		====================================
 */

/*
 *		float48pl		- returns arg1 + arg2
 *		float48mi		- returns arg1 - arg2
 *		float48mul		- returns arg1 * arg2
 *		float48div		- returns arg1 / arg2
 */
Datum float48pl(PG_FUNCTION_ARGS)
{
    float4 arg1 = PG_GETARG_FLOAT4(0);
    float8 arg2 = PG_GETARG_FLOAT8(1);
    float8 result;

    result = arg1 + arg2;
    CHECKFLOATVAL(result, isinf(arg1) || isinf(arg2), true);
    PG_RETURN_FLOAT8(result);
}

Datum float48mi(PG_FUNCTION_ARGS)
{
    float4 arg1 = PG_GETARG_FLOAT4(0);
    float8 arg2 = PG_GETARG_FLOAT8(1);
    float8 result;

    result = arg1 - arg2;
    CHECKFLOATVAL(result, isinf(arg1) || isinf(arg2), true);
    PG_RETURN_FLOAT8(result);
}

Datum float48mul(PG_FUNCTION_ARGS)
{
    float4 arg1 = PG_GETARG_FLOAT4(0);
    float8 arg2 = PG_GETARG_FLOAT8(1);
    float8 result;

    if (arg1 == 0.0 || arg2 == 0.0)
        PG_RETURN_FLOAT8(0);

    result = arg1 * arg2;
    CHECKFLOATVAL(result, isinf(arg1) || isinf(arg2), arg1 == 0 || arg2 == 0);
    PG_RETURN_FLOAT8(result);
}

Datum float48div(PG_FUNCTION_ARGS)
{
    float4 arg1 = PG_GETARG_FLOAT4(0);
    float8 arg2 = PG_GETARG_FLOAT8(1);
    float8 result;

    if (arg2 == 0.0)
        ereport(ERROR, (errcode(ERRCODE_DIVISION_BY_ZERO), errmsg("division by zero")));

    if (arg1 == 0.0)
        PG_RETURN_FLOAT8(0);

    result = arg1 / arg2;
    CHECKFLOATVAL(result, isinf(arg1) || isinf(arg2), arg1 == 0);
    PG_RETURN_FLOAT8(result);
}

/*
 *		float84pl		- returns arg1 + arg2
 *		float84mi		- returns arg1 - arg2
 *		float84mul		- returns arg1 * arg2
 *		float84div		- returns arg1 / arg2
 */
Datum float84pl(PG_FUNCTION_ARGS)
{
    float8 arg1 = PG_GETARG_FLOAT8(0);
    float4 arg2 = PG_GETARG_FLOAT4(1);
    float8 result;

    result = arg1 + arg2;

    CHECKFLOATVAL(result, isinf(arg1) || isinf(arg2), true);
    PG_RETURN_FLOAT8(result);
}

Datum float84mi(PG_FUNCTION_ARGS)
{
    float8 arg1 = PG_GETARG_FLOAT8(0);
    float4 arg2 = PG_GETARG_FLOAT4(1);
    float8 result;

    result = arg1 - arg2;

    CHECKFLOATVAL(result, isinf(arg1) || isinf(arg2), true);
    PG_RETURN_FLOAT8(result);
}

Datum float84mul(PG_FUNCTION_ARGS)
{
    float8 arg1 = PG_GETARG_FLOAT8(0);
    float4 arg2 = PG_GETARG_FLOAT4(1);
    float8 result;

    if (arg1 == 0.0 || arg2 == 0.0)
        PG_RETURN_FLOAT8(0);

    result = arg1 * arg2;

    CHECKFLOATVAL(result, isinf(arg1) || isinf(arg2), arg1 == 0 || arg2 == 0);
    PG_RETURN_FLOAT8(result);
}

Datum float84div(PG_FUNCTION_ARGS)
{
    float8 arg1 = PG_GETARG_FLOAT8(0);
    float4 arg2 = PG_GETARG_FLOAT4(1);
    float8 result;

    if (arg2 == 0.0)
        ereport(ERROR, (errcode(ERRCODE_DIVISION_BY_ZERO), errmsg("division by zero")));

    if (arg1 == 0.0)
        PG_RETURN_FLOAT8(0);

    result = arg1 / arg2;

    CHECKFLOATVAL(result, isinf(arg1) || isinf(arg2), arg1 == 0);
    PG_RETURN_FLOAT8(result);
}

/*
 *		====================
 *		COMPARISON OPERATORS
 *		====================
 */

/*
 *		float48{eq,ne,lt,le,gt,ge}		- float4/float8 comparison operations
 */
Datum float48eq(PG_FUNCTION_ARGS)
{
    float4 arg1 = PG_GETARG_FLOAT4(0);
    float8 arg2 = PG_GETARG_FLOAT8(1);

    PG_RETURN_BOOL(float8_cmp_internal(arg1, arg2) == 0);
}

Datum float48ne(PG_FUNCTION_ARGS)
{
    float4 arg1 = PG_GETARG_FLOAT4(0);
    float8 arg2 = PG_GETARG_FLOAT8(1);

    PG_RETURN_BOOL(float8_cmp_internal(arg1, arg2) != 0);
}

Datum float48lt(PG_FUNCTION_ARGS)
{
    float4 arg1 = PG_GETARG_FLOAT4(0);
    float8 arg2 = PG_GETARG_FLOAT8(1);

    PG_RETURN_BOOL(float8_cmp_internal(arg1, arg2) < 0);
}

Datum float48le(PG_FUNCTION_ARGS)
{
    float4 arg1 = PG_GETARG_FLOAT4(0);
    float8 arg2 = PG_GETARG_FLOAT8(1);

    PG_RETURN_BOOL(float8_cmp_internal(arg1, arg2) <= 0);
}

Datum float48gt(PG_FUNCTION_ARGS)
{
    float4 arg1 = PG_GETARG_FLOAT4(0);
    float8 arg2 = PG_GETARG_FLOAT8(1);

    PG_RETURN_BOOL(float8_cmp_internal(arg1, arg2) > 0);
}

Datum float48ge(PG_FUNCTION_ARGS)
{
    float4 arg1 = PG_GETARG_FLOAT4(0);
    float8 arg2 = PG_GETARG_FLOAT8(1);

    PG_RETURN_BOOL(float8_cmp_internal(arg1, arg2) >= 0);
}

/*
 *		float84{eq,ne,lt,le,gt,ge}		- float8/float4 comparison operations
 */
Datum float84eq(PG_FUNCTION_ARGS)
{
    float8 arg1 = PG_GETARG_FLOAT8(0);
    float4 arg2 = PG_GETARG_FLOAT4(1);

    PG_RETURN_BOOL(float8_cmp_internal(arg1, arg2) == 0);
}

Datum float84ne(PG_FUNCTION_ARGS)
{
    float8 arg1 = PG_GETARG_FLOAT8(0);
    float4 arg2 = PG_GETARG_FLOAT4(1);

    PG_RETURN_BOOL(float8_cmp_internal(arg1, arg2) != 0);
}

Datum float84lt(PG_FUNCTION_ARGS)
{
    float8 arg1 = PG_GETARG_FLOAT8(0);
    float4 arg2 = PG_GETARG_FLOAT4(1);

    PG_RETURN_BOOL(float8_cmp_internal(arg1, arg2) < 0);
}

Datum float84le(PG_FUNCTION_ARGS)
{
    float8 arg1 = PG_GETARG_FLOAT8(0);
    float4 arg2 = PG_GETARG_FLOAT4(1);

    PG_RETURN_BOOL(float8_cmp_internal(arg1, arg2) <= 0);
}

Datum float84gt(PG_FUNCTION_ARGS)
{
    float8 arg1 = PG_GETARG_FLOAT8(0);
    float4 arg2 = PG_GETARG_FLOAT4(1);

    PG_RETURN_BOOL(float8_cmp_internal(arg1, arg2) > 0);
}

Datum float84ge(PG_FUNCTION_ARGS)
{
    float8 arg1 = PG_GETARG_FLOAT8(0);
    float4 arg2 = PG_GETARG_FLOAT4(1);

    PG_RETURN_BOOL(float8_cmp_internal(arg1, arg2) >= 0);
}

/*
 * Implements the float8 version of the width_bucket() function
 * defined by SQL2003. See also width_bucket_numeric().
 *
 * 'bound1' and 'bound2' are the lower and upper bounds of the
 * histogram's range, respectively. 'count' is the number of buckets
 * in the histogram. width_bucket() returns an integer indicating the
 * bucket number that 'operand' belongs to in an equiwidth histogram
 * with the specified characteristics. An operand smaller than the
 * lower bound is assigned to bucket 0. An operand greater than the
 * upper bound is assigned to an additional bucket (with number
 * count+1). We don't allow "NaN" for any of the float8 inputs, and we
 * don't allow either of the histogram bounds to be +/- infinity.
 */
Datum width_bucket_float8(PG_FUNCTION_ARGS)
{
    float8 operand = PG_GETARG_FLOAT8(0);
    float8 bound1 = PG_GETARG_FLOAT8(1);
    float8 bound2 = PG_GETARG_FLOAT8(2);
    int32 count = PG_GETARG_INT32(3);
    int32 result;

    if (count <= 0.0)
        ereport(ERROR,
            (errcode(ERRCODE_INVALID_ARGUMENT_FOR_WIDTH_BUCKET_FUNCTION), errmsg("count must be greater than zero")));

    if (isnan(operand) || isnan(bound1) || isnan(bound2))
        ereport(ERROR,
            (errcode(ERRCODE_INVALID_ARGUMENT_FOR_WIDTH_BUCKET_FUNCTION),
                errmsg("operand, lower bound, and upper bound cannot be NaN")));

    /* Note that we allow "operand" to be infinite */
    if (isinf(bound1) || isinf(bound2))
        ereport(ERROR,
            (errcode(ERRCODE_INVALID_ARGUMENT_FOR_WIDTH_BUCKET_FUNCTION),
                errmsg("lower and upper bounds must be finite")));

    if (bound1 < bound2) {
        if (operand < bound1)
            result = 0;
        else if (operand >= bound2) {
            if (pg_add_s32_overflow(count, 1, &result))
                ereport(ERROR, (errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE), errmsg("integer out of range")));
        } else
            result = (int32)(((float8)count * (operand - bound1) / (bound2 - bound1)) + 1);
    } else if (bound1 > bound2) {
        if (operand > bound1)
            result = 0;
        else if (operand <= bound2) {
            if (pg_add_s32_overflow(count, 1, &result))
                ereport(ERROR, (errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE), errmsg("integer out of range")));
        } else
            result = (int32)(((float8)count * (bound1 - operand) / (bound1 - bound2)) + 1);
    } else {
        ereport(ERROR,
            (errcode(ERRCODE_INVALID_ARGUMENT_FOR_WIDTH_BUCKET_FUNCTION),
                errmsg("lower bound cannot equal upper bound")));
        result = 0; /* keep the compiler quiet */
    }

    PG_RETURN_INT32(result);
}

#ifdef PGXC
Datum float8_collect(PG_FUNCTION_ARGS)
{
    ArrayType* collectarray = PG_GETARG_ARRAYTYPE_P(0);
    ArrayType* transarray = PG_GETARG_ARRAYTYPE_P(1);
    float8* collectvalues = NULL;
    float8* transvalues = NULL;
    float8 N, sumX, sumX2;

    collectvalues = check_float8_array(collectarray, "float8_collect", 3);
    transvalues = check_float8_array(transarray, "float8_collect", 3);
    N = collectvalues[0];
    sumX = collectvalues[1];
    sumX2 = collectvalues[2];

    N += transvalues[0];
    sumX += transvalues[1];
    CHECKFLOATVAL(sumX, isinf(collectvalues[1]) || isinf(transvalues[1]), true);
    sumX2 += transvalues[2];
    CHECKFLOATVAL(sumX2, isinf(collectvalues[2]) || isinf(transvalues[2]), true);

    /*
     * If we're invoked by nodeAgg, we can cheat and modify our first
     * parameter in-place to reduce palloc overhead. Otherwise we construct a
     * new array with the updated transition data and return it.
     */
    if (fcinfo->context && (IsA(fcinfo->context, AggState) || IsA(fcinfo->context, WindowAggState))) {
        collectvalues[0] = N;
        collectvalues[1] = sumX;
        collectvalues[2] = sumX2;

        PG_RETURN_ARRAYTYPE_P(collectarray);
    } else {
        Datum collectdatums[3];
        ArrayType* result = NULL;

        collectdatums[0] = Float8GetDatumFast(N);
        collectdatums[1] = Float8GetDatumFast(sumX);
        collectdatums[2] = Float8GetDatumFast(sumX2);

        result = construct_array(collectdatums, 3, FLOAT8OID, sizeof(float8), FLOAT8PASSBYVAL, 'd');

        PG_RETURN_ARRAYTYPE_P(result);
    }
}

Datum float8_regr_collect(PG_FUNCTION_ARGS)
{
    ArrayType* collectarray = PG_GETARG_ARRAYTYPE_P(0);
    ArrayType* transarray = PG_GETARG_ARRAYTYPE_P(1);
    float8* collectvalues = NULL;
    float8* transvalues = NULL;
    float8 N, sumX, sumX2, sumY, sumY2, sumXY;

    collectvalues = check_float8_array(collectarray, "float8_accum", 6);
    transvalues = check_float8_array(transarray, "float8_accum", 6);
    N = collectvalues[0];
    sumX = collectvalues[1];
    sumX2 = collectvalues[2];
    sumY = collectvalues[3];
    sumY2 = collectvalues[4];
    sumXY = collectvalues[5];

    N += transvalues[0];
    sumX += transvalues[1];
    CHECKFLOATVAL(sumX, isinf(collectvalues[1]) || isinf(transvalues[1]), true);
    sumX2 += transvalues[2];
    CHECKFLOATVAL(sumX2, isinf(collectvalues[2]) || isinf(transvalues[2]), true);
    sumY += transvalues[3];
    CHECKFLOATVAL(sumY, isinf(collectvalues[3]) || isinf(transvalues[3]), true);
    sumY2 += transvalues[4];
    CHECKFLOATVAL(sumY2, isinf(collectvalues[4]) || isinf(transvalues[4]), true);
    sumXY += transvalues[5];
    CHECKFLOATVAL(sumXY, isinf(collectvalues[5]) || isinf(transvalues[5]), true);

    /*
     * If we're invoked by nodeAgg, we can cheat and modify our first
     * parameter in-place to reduce palloc overhead. Otherwise we construct a
     * new array with the updated transition data and return it.
     */
    if (fcinfo->context && (IsA(fcinfo->context, AggState) || IsA(fcinfo->context, WindowAggState))) {
        collectvalues[0] = N;
        collectvalues[1] = sumX;
        collectvalues[2] = sumX2;
        collectvalues[3] = sumY;
        collectvalues[4] = sumY2;
        collectvalues[5] = sumXY;

        PG_RETURN_ARRAYTYPE_P(collectarray);
    } else {
        Datum collectdatums[6];
        ArrayType* result = NULL;

        collectdatums[0] = Float8GetDatumFast(N);
        collectdatums[1] = Float8GetDatumFast(sumX);
        collectdatums[2] = Float8GetDatumFast(sumX2);
        collectdatums[3] = Float8GetDatumFast(sumY);
        collectdatums[4] = Float8GetDatumFast(sumY2);
        collectdatums[5] = Float8GetDatumFast(sumXY);

        result = construct_array(collectdatums, 6, FLOAT8OID, sizeof(float8), FLOAT8PASSBYVAL, 'd');

        PG_RETURN_ARRAYTYPE_P(result);
    }
}
#endif

/* ========== PRIVATE ROUTINES ========== */

#ifndef HAVE_CBRT

static double cbrt(double x)
{
    int isneg = (x < 0.0);
    double absx = fabs(x);
    double tmpres = pow(absx, (double)1.0 / (double)3.0);

    /*
     * The result is somewhat inaccurate --- not really pow()'s fault, as the
     * exponent it's handed contains roundoff error.  We can improve the
     * accuracy by doing one iteration of Newton's formula.  Beware of zero
     * input however.
     */
    if (tmpres > 0.0)
        tmpres -= (tmpres - absx / (tmpres * tmpres)) / (double)3.0;

    return isneg ? -tmpres : tmpres;
}

#endif /* !HAVE_CBRT */

// convert text to float8 before multiply
// return float8, report a warning when overflow
Datum text_multiply_float8(PG_FUNCTION_ARGS)
{
    char* str = text_to_cstring(PG_GETARG_TEXT_P(0));
    float8 arg1 = DatumGetFloat8(DirectFunctionCall1(float8in, CStringGetDatum(str)));
    float8 arg2 = PG_GETARG_FLOAT8(1);
    float8 result;

    result = arg1 * arg2;
    pfree_ext(str);

    CHECKFLOATVAL(result, isinf(arg1) || isinf(arg2), arg1 == 0 || arg2 == 0);
    PG_RETURN_FLOAT8(result);
}

// convert text to float8 before multiply
Datum float8_multiply_text(PG_FUNCTION_ARGS)
{
    return DirectFunctionCall2(text_multiply_float8, PG_GETARG_DATUM(1), PG_GETARG_DATUM(0));
}

/*convert from float8 to interval*/
Datum float8_interval(PG_FUNCTION_ARGS)
{
    Datum val = PG_GETARG_DATUM(0);
    return DirectFunctionCall1(numeric_interval, DirectFunctionCall1(float8_numeric, val));
}

/*convert from float8 to interval*/
Datum float8_to_interval(PG_FUNCTION_ARGS)
{
    Datum val = PG_GETARG_DATUM(0);
    int32 typmod = PG_GETARG_INT32(1);
    return DirectFunctionCall2(numeric_to_interval,
                                DirectFunctionCall1(float8_numeric, val),
                                Int32GetDatum(typmod));
}
