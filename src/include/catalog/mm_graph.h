/* -------------------------------------------------------------------------
 *
 * mm_graph.h
 *	  definition of the system "graph" relation (pg_class)
 *	  along with the relation's initial contents.
 *
 *
 * Portions Copyright (c) 2024 by the University of WuHan.
 *
 * src/include/catalog/mm_graph.h
 *
 * NOTES
 *	  the genbki.pl script reads this file and generates .bki
 *	  information from the DATA() statements.
 *
 * -------------------------------------------------------------------------
 */
#ifndef MM_GRAPH_H
#define MM_GRAPH_H

#include "catalog/genbki.h"

#define GraphRelationId 8810
#define GraphRelation_Rowtype_Id 8815

CATALOG(mm_graph,8810) BKI_SCHEMA_MACRO
{
	Oid			graphoid;
	NameData	graphname;
	Oid			nspid;
}FormData_mm_graph;

typedef FormData_mm_graph *Form_mm_graph;

#define Natts_mm_graph 3
#define Anum_mm_graph_graphoid 1
#define Anum_mm_graph_graphname 2
#define Anum_mm_graph_nspid 3

#endif                  /* MM_GRAPH_H */