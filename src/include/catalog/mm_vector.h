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
 * mm_vector.h
 *      definition of the system "vector" relation (mm_vector)
 *      along with the relation's initial contents.
 *
 *
 *
 * src/include/catalog/mm_vector.h
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
#ifndef MM_VECTOR_H
#define MM_VECTOR_H

#include "catalog/genbki.h"
#include "nodes/pg_list.h"

/* ----------------
 *        mm_vector definition.  cpp turns this into
 *        typedef struct FormData_mm_vector
 * ----------------
 */
#define VectorRelationId    8860
#define VectorRelation_Rowtype_Id 8861

CATALOG(mm_vector,8860) BKI_ROWTYPE_OID(8861) BKI_SCHEMA_MACRO BKI_WITHOUT_OIDS
{
    NameData     vecname;       /* name of vector */    
    Oid          vecid;         /* Oid of vector */
    Oid          vecnsp;        /* Oid of schema vector belongs to */
    int8          dims;          /* Dims of Vector */
} FormData_mm_vector;

/* ----------------
 *        Form_mm_vector corresponds to a pointer to a tuple with
 *        the format of mm_vector relation.
 * ----------------
 */
typedef FormData_mm_vector *Form_mm_vector;

/* ----------------
 *        compiler constants for mm_vector
 * ----------------
 */
#define Natts_mm_vector              4
#define Anum_mm_vector_vecname      1
#define Anum_mm_vector_vecid        2
#define Anum_mm_vector_vecnsp       3
#define Anum_mm_vector_dims         4
/* ----------------
 *        mm_vector initial contents
 * ----------------
 */
#define FIXED_VECTOR_COLNAME "vec"
#endif   /* MM_VECTOR_H */
