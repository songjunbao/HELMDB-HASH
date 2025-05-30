/* -------------------------------------------------------------------------
 *
 * parse_relation.h
 *	  prototypes for parse_relation.c.
 *
 *
 * Portions Copyright (c) 1996-2012, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * src/include/parser/parse_relation.h
 *
 * -------------------------------------------------------------------------
 */
#ifndef PARSE_RELATION_H
#define PARSE_RELATION_H

#include "parser/parse_node.h"

#define NORMAL_TABLE_MODEL_TYPE 't'
#define VECTOR_TABLE_MODEL_TYPE 'v'
#define ARRAY_TABLE_MODEL_TYPE 'a'
#define GRAPH_TABLE_MODEL_TYPE 'g'
#define DOCUMENT_TABLE_MODEL_TYPE 'd'
#define OTHER_MODEL_TYPE_NUMS 4
#define OTHER_MODEL_TYPES_ARRAY {VECTOR_TABLE_MODEL_TYPE,ARRAY_TABLE_MODEL_TYPE,GRAPH_TABLE_MODEL_TYPE,DOCUMENT_TABLE_MODEL_TYPE}

extern RangeTblEntry* refnameRangeTblEntry(
    ParseState* pstate, const char* schemaname, const char* refname, int location, int* sublevels_up);
extern CommonTableExpr* scanNameSpaceForCTE(ParseState* pstate, const char* refname, Index* ctelevelsup);
extern void checkNameSpaceConflicts(ParseState* pstate, List* namespace1, List* namespace2);
extern int RTERangeTablePosn(ParseState* pstate, RangeTblEntry* rte, int* sublevels_up);
extern RangeTblEntry* GetRTEByRangeTablePosn(ParseState* pstate, int varno, int sublevels_up);
extern CommonTableExpr* GetCTEForRTE(ParseState* pstate, RangeTblEntry* rte, int rtelevelsup);
extern Node* scanRTEForColumn(ParseState* pstate, RangeTblEntry* rte, char* colname, int location, bool omit = false);
extern Node* colNameToVar(
    ParseState* pstate, char* colname, bool localonly, int location, RangeTblEntry** final_rte = NULL);
extern void markVarForSelectPriv(ParseState* pstate, Var* var, RangeTblEntry* rte);
extern Relation parserOpenTable(ParseState* pstate, const RangeVar* relation, int lockmode, bool isFirstNode = true,
    bool isCreateView = false, bool isSupportSynonym = false);
extern RangeTblEntry* addRangeTableEntry(ParseState* pstate, RangeVar* relation, Alias* alias, bool inh, bool inFromCl,
    bool isFirstNode = true, bool isCreateView = false, bool isSupportSynonym = false);
extern RangeTblEntry* addRangeTableEntryForRelation(
    ParseState* pstate, Relation rel, Alias* alias, bool inh, bool inFromCl);
extern RangeTblEntry* addRangeTableEntryForSubquery(
    ParseState* pstate, Query* subquery, Alias* alias, bool lateral, bool inFromCl, bool sublinkPullUp = false);
extern RangeTblEntry* addRangeTableEntryForFunction(
    ParseState* pstate, char* funcname, Node* funcexpr, RangeFunction* rangefunc, bool lateral, bool inFromCl);
extern RangeTblEntry* addRangeTableEntryForValues(
    ParseState* pstate, List* exprs, List* collations, Alias* alias, bool inFromCl);
extern RangeTblEntry* addRangeTableEntryForJoin(
    ParseState* pstate, List* colnames, JoinType jointype, List* aliasvars, Alias* alias, bool inFromCl);
extern RangeTblEntry* addRangeTableEntryForCTE(
    ParseState* pstate, CommonTableExpr* cte, Index levelsup, RangeVar* rv, bool inFromCl);
extern RangeTblEntry* getRangeTableEntryByRelation(Relation rel);
extern bool isLockedRefname(ParseState* pstate, const char* refname);
extern void addRTEtoQuery(
    ParseState* pstate, RangeTblEntry* rte, bool addToJoinList, bool addToRelNameSpace, bool addToVarNameSpace);
extern void errorMissingRTE(ParseState* pstate, RangeVar* relation, bool hasplus = false);
extern void errorMissingColumn(ParseState *pstate, char *relname, char *colname, int location);
extern void expandRTE(RangeTblEntry* rte, int rtindex, int sublevels_up, int location, bool include_dropped,
    List** colnames, List** colvars, ParseState* pstate = NULL);
extern List* expandRelAttrs(ParseState* pstate, RangeTblEntry* rte, int rtindex, int sublevels_up, int location);
extern int attnameAttNum(Relation rd, const char* attname, bool sysColOK);
extern Name attnumAttName(Relation rd, int attid);
extern Oid attnumTypeId(Relation rd, int attid);
extern Oid attnumCollationId(Relation rd, int attid);
extern bool GetPartitionOidForRTE(RangeTblEntry *rte, RangeVar *relation, ParseState *pstate, Relation rel);
extern bool GetSubPartitionOidForRTE(RangeTblEntry *rte, RangeVar *relation, ParseState *pstate, Relation rel);
extern bool ValidateDependView(Oid view_oid, char obj_type);

extern bool checkModelType(Oid relid,char type);
extern char queryModelType(Oid relid);

extern RangeTblEntry* addRangeTableEntryForCypher(
    ParseState* pstate, Query* cypher_query, Alias* alias, RangeTblEntry* graph_te, bool inFromCl);
#ifdef PGXC
extern int specialAttNum(const char* attname);
#endif

#endif /* PARSE_RELATION_H */
