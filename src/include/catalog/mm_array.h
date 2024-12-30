/*
* Copyright (c) 2022 Huawei Technologies Co.,Ltd.
*
* openGauss is licensed under Mulan PSL v2.
* You can use this software according to the terms and conditions of the Mulan PSL v2.
* You may obtain a copy of Mulan PSL v2 at:
*
*          http://license.coscl.org.cn/MulanPSL2
*
* THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
* EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
* MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
* See the Mulan PSL v2 for more details.
* ---------------------------------------------------------------------------------------
 *
 * mm_array.h
 *      definition of the system "array" relation (mm_array)
 *      along with the relation's initial contents.
 *
 *
 *
 * src/include/catalog/mm_array.h
 *
 * NOTES
 *      the genbki.pl script reads this file and generates .bki
 *      information from the DATA() statements.
 *
 *      XXX do NOT break up DATA() statements into multiple lines!
 *          the scripts are not as smart as you might think...
 *
 * -------------------------------------------------------------------------
 */
#ifndef MM_ARRAY_H
#define MM_ARRAY_H

#include "catalog/genbki.h"
#include "nodes/pg_list.h"

/* ----------------
 *        mm_array definition.  cpp turns this into
 *        typedef struct FormData_mm_array
 * ----------------
 */
#define ArrayRelationId    9045
#define ArrayRelation_Rowtype_Id 9046

CATALOG(mm_array,9045) BKI_ROWTYPE_OID(9046) BKI_SCHEMA_MACRO BKI_WITHOUT_OIDS
{
    NameData     arrname;       /* name of array */    
    Oid          arrid;         /* Oid of array */
    Oid          arrnsp;        /* Oid of schema array belongs to */
    int4         arrdimnum;     /* Number of dimensions */
    int4         arrattrnum;    /* Number of attributes */
    bool         arrasmatrix;   /* If the array is stored as a matix */
} FormData_mm_array;

/* ----------------
 *        Form_mm_array corresponds to a pointer to a tuple with
 *        the format of mm_array relation.
 * ----------------
 */
typedef FormData_mm_array *Form_mm_array;

/* ----------------
 *        compiler constants for mm_array
 * ----------------
 */
#define Natts_mm_array              6
#define Anum_mm_array_arrname       1
#define Anum_mm_array_arrid         2
#define Anum_mm_array_arrnsp        3
#define Anum_mm_array_arrdimnum     4
#define Anum_mm_array_arrattrnum    5
#define Anum_mm_array_arrasmatrix   6

/* ----------------
 *        mm_array has no initial contents
 * ----------------
 */

#endif   /* MM_ARRAY_H */
