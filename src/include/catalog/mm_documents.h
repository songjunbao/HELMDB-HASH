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
 * mm_documents.h
 *      definition of the system "documents" relation (mm_documents)
 *      along with the relation's initial contents.
 *
 *
 *
 * src/include/catalog/mm_documents.h
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
#ifndef MM_DOCUMENTS_H
#define MM_DOCUMENTS_H

#include "catalog/genbki.h"
#include "nodes/pg_list.h"

/* ----------------
 *        mm_documents definition.  cpp turns this into
 *        typedef struct FormData_mm_documents
 * ----------------
 */
#define DocumentsRelationId    9041
#define DocumentsRelation_Rowtype_Id 9042

CATALOG(mm_documents,9041) BKI_ROWTYPE_OID(9042) BKI_SCHEMA_MACRO BKI_WITHOUT_OIDS
{
    NameData     docsname;       /* name of documents */    
    Oid          docsid;         /* Oid of documents */
    Oid          docsnsp;        /* Oid of schema documents belongs to */
} FormData_mm_documents;

/* ----------------
 *        Form_mm_documents corresponds to a pointer to a tuple with
 *        the format of mm_documents relation.
 * ----------------
 */
typedef FormData_mm_documents *Form_mm_documents;

/* ----------------
 *        compiler constants for mm_documents
 * ----------------
 */
#define Natts_mm_documents              3
#define Anum_mm_documents_docsname      1
#define Anum_mm_documents_docsid        2
#define Anum_mm_documents_docsnsp       3

/* ----------------
 *        mm_documents initial contents
 * ----------------
 */
#define FIXED_DOCUMENT_COLNAME "doc"
#endif   /* MM_DOCUMENTS_H */
