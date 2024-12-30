/*-------------------------------------------------------------------------
 *
 * mm_label.h
 *	  definition of the system "label" relation (mm_label)
 *	  along with the relation's initial contents.
 *
 *
 * Portions Copyright (c) 2024 by the University of WuHan.
 *
 * src/include/catalog/mm_label.h
 *
 * NOTES
 *	  the genbki.pl script reads this file and generates .bki
 *	  information from the DATA() statements.
 *
 *-------------------------------------------------------------------------
 */
#ifndef MM_LABEL_H
#define MM_LABEL_H

#include "catalog/genbki.h"

#define LabelRelationId 8840
#define GraphLabel_Rowtype_Id 8845

CATALOG(mm_label,8840) BKI_SCHEMA_MACRO
{
	Oid			labelid;			/* oid */
	NameData	labelname;		    /* label name */
	Oid			graphid;		    /* graph oid */
	char		labkind;		    /* see LABEL_KIND_XXX constants below */
}FormData_mm_label;

typedef FormData_mm_label *Form_mm_label;

#define Natts_mm_label 4
#define Anum_mm_label_labelid 1
#define Anum_mm_label_labelname 2
#define Anum_mm_label_graphid 3
#define Anum_mm_label_labkind 4

#define LABEL_KIND_VERTEX	'v'
#define LABEL_KIND_EDGE		'e'

#endif							/* MM_LABEL_H */