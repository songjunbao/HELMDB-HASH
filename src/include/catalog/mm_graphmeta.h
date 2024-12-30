/* -------------------------------------------------------------------------
 *
 * mm_graphmeta.h
 *	  definition of the system "graphmeta" relation (pg_class)
 *	  along with the relation's initial contents.
 *
 *
 * Portions Copyright (c) 2024 by the University of WuHan.
 *
 * src/include/catalog/mm_graphmeta.h
 *
 * NOTES
 *	  the genbki.pl script reads this file and generates .bki
 *	  information from the DATA() statements.
 *
 * -------------------------------------------------------------------------
 */

#ifndef MM_GRAPHMETA_H
#define MM_GRAPHMETA_H

#include "catalog/genbki.h"

#define GraphMetaId 8820
#define GraphMeta_Rowtype_Id 8825

CATALOG(mm_graphmeta,8820) BKI_SCHEMA_MACRO
{
	Oid			graphid;            /* graph oid */
	int4		edgeid;            /* edge label id */
	int4		start;            /* start vertex label id */
	int4		end;            /* end vertex label id */
	int4		edgecount;        /* # of edge between start and end */
}FormData_mm_graphmeta;

typedef FormData_mm_graphmeta *Form_mm_graphmeta;

#define Natts_mm_graphmeta 5
#define Anum_mm_graphmeta_graphid 1
#define Anum_mm_graphmeta_edgeid 2
#define Anum_mm_graphmeta_start 3
#define Anum_mm_graphmeta_end 4
#define Anum_mm_graphmeta_edgecount 5


#endif  /* MM_GRAPHMETA_H */