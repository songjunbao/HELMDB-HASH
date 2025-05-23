/* -------------------------------------------------------------------------
 *
 * paths.h
 *	  prototypes for various files in optimizer/path
 *
 *
 * Portions Copyright (c) 1996-2012, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * src/include/optimizer/paths.h
 *
 * -------------------------------------------------------------------------
 */
#ifndef PATHS_H
#define PATHS_H

#include "nodes/relation.h"

typedef void (*set_rel_pathlist_hook_type) (PlannerInfo *root,
                                           RelOptInfo *rel,
                                           Index rti,
                                           RangeTblEntry *rte);
extern THR_LOCAL PGDLLIMPORT set_rel_pathlist_hook_type set_rel_pathlist_hook;

typedef void (*set_join_pathlist_hook_type) (PlannerInfo *root,
                                               RelOptInfo *joinrel,
                                               RelOptInfo *outerrel,
                                               RelOptInfo *innerrel,
                                               JoinType jointype,
                                               SpecialJoinInfo *sjinfo,
                                               Relids param_source_rels,
                                               SemiAntiJoinFactors *semifactors,
                                               List *restrictlist);
extern THR_LOCAL PGDLLIMPORT set_join_pathlist_hook_type set_join_pathlist_hook;

extern RelOptInfo* make_one_rel(PlannerInfo* root, List* joinlist, Relids non_keypreserved = NULL);
extern RelOptInfo* standard_join_search(PlannerInfo* root, int levels_needed, List* initial_rels);

extern void set_base_rel_sizes(PlannerInfo* root, bool onlyRelatinalTable = false);
extern void set_rel_size(PlannerInfo* root, RelOptInfo* rel, Index rti, RangeTblEntry* rte);

#ifdef OPTIMIZER_DEBUG
extern void debug_print_rel(PlannerInfo* root, RelOptInfo* rel);
#endif

/*
 * indxpath.c
 *	  routines to generate index paths
 */
typedef enum {
    NONFEATURED_INDEX,   /* Ordinary index, placeholder */
    GLOBAL_PARTITION_INDEX = 1,
    CROSSBUCKET_INDEX,
    GLOBAL_PARTITION_AND_CROSSBUCKET_INDEX   /* GPI/CBI hybrid */
} IndexFeature;

extern void create_index_paths(PlannerInfo* root, RelOptInfo* rel);
extern List* generate_bitmap_or_paths(
    PlannerInfo* root, RelOptInfo* rel, List* clauses, List* other_clauses, bool restriction_only);
extern IndexFeature getIndexFeature(bool isPartitioned, bool hasBucket);
extern List* GenerateBitmapOrPathsWithFeaturedIndex(
    PlannerInfo* root, RelOptInfo* rel, const List* clauses, List* other_clauses, bool restriction_only,
    IndexFeature idx_feature);
extern bool relation_has_unique_index_for(
    PlannerInfo* root, RelOptInfo* rel, List* restrictlist, List* exprlist, List* oprlist);
extern bool eclass_member_matches_indexcol(
    EquivalenceClass* ec, EquivalenceMember* em, IndexOptInfo* index, int indexcol);
extern bool match_index_to_operand(Node* operand, int indexcol, IndexOptInfo* index, bool match_prefixkey = false);
extern void expand_indexqual_conditions(
    IndexOptInfo* index, List* indexclauses, List* indexclausecols, List** indexquals_p, List** indexqualcols_p);
extern void check_partial_indexes(PlannerInfo* root, RelOptInfo* rel);
extern Expr* adjust_rowcompare_for_index(
    RowCompareExpr* clause, IndexOptInfo* index, int indexcol, List** indexcolnos, bool* var_on_left_p);
/*
 * Check index path whether use global partition index to scan
 */
inline bool CheckIndexPathUseGPI(IndexPath* ipath)
{
    return ipath->indexinfo->isGlobal;
}

/*
 * tidpath.h
 *	  routines to generate tid paths
 */
extern void create_tidscan_paths(PlannerInfo* root, RelOptInfo* rel);

/*
 * joinpath.c
 *	   routines to create join paths
 */
extern void add_paths_to_joinrel(PlannerInfo* root, RelOptInfo* joinrel, RelOptInfo* outerrel, RelOptInfo* innerrel,
    JoinType jointype, SpecialJoinInfo* sjinfo, List* restrictlist);
extern bool clause_sides_match_join(RestrictInfo* rinfo, RelOptInfo* outerrel, RelOptInfo* innerrel);

#ifdef PGXC
/*
 * rquerypath.c
 * 		routines to create RemoteQuery paths
 */
extern bool create_plainrel_rqpath(PlannerInfo* root, RelOptInfo* rel, RangeTblEntry* rte);
extern void create_joinrel_rqpath(PlannerInfo* root, RelOptInfo* joinrel, RelOptInfo* outerrel, RelOptInfo* innerrel,
    List* restrictlist, JoinType jointype, SpecialJoinInfo* sjinfo);
#endif /* PGXC */

/*
 * joinrels.c
 *	  routines to determine which relations to join
 */
extern void join_search_one_level(PlannerInfo* root, int level);
extern RelOptInfo* make_join_rel(PlannerInfo* root, RelOptInfo* rel1, RelOptInfo* rel2);
extern bool have_join_order_restriction(PlannerInfo* root, RelOptInfo* rel1, RelOptInfo* rel2);

/*
 * equivclass.c
 *	  routines for managing EquivalenceClasses
 */
extern bool process_equivalence(PlannerInfo* root, RestrictInfo* restrictinfo, bool below_outer_join);
extern Expr* canonicalize_ec_expression(Expr* expr, Oid req_type, Oid req_collation);
extern void reconsider_outer_join_clauses(PlannerInfo* root);
extern EquivalenceClass* get_eclass_for_sort_expr(PlannerInfo* root, Expr* expr, List* opfamilies, Oid opcintype,
    Oid collation, Index sortref, bool groupSet, Relids rel, bool create_it);
extern void generate_base_implied_equalities(PlannerInfo* root);
extern void generate_base_implied_qualities(PlannerInfo* root);
extern List* generate_join_implied_equalities(
    PlannerInfo* root, Relids join_relids, Relids outer_relids, RelOptInfo* inner_rel);
extern List* generate_join_implied_equalities_for_ecs(
    PlannerInfo* root, List* eclasses, Relids join_relids, Relids outer_relids, RelOptInfo* inner_rel);
extern bool exprs_known_equal(PlannerInfo* root, Node* item1, Node* item2);
extern void add_child_rel_equivalences(
    PlannerInfo* root, AppendRelInfo* appinfo, RelOptInfo* parent_rel, RelOptInfo* child_rel);
extern void mutate_eclass_expressions(PlannerInfo* root, Node* (*mutator)(), void* context, bool include_child_exprs);
extern List* generate_implied_equalities_for_indexcol(PlannerInfo* root, IndexOptInfo* index, int indexcol, Relids prohibited_rels);
extern bool have_relevant_eclass_joinclause(PlannerInfo* root, RelOptInfo* rel1, RelOptInfo* rel2);
extern bool has_relevant_eclass_joinclause(PlannerInfo* root, RelOptInfo* rel1);
extern bool eclass_useful_for_merging(EquivalenceClass* eclass, RelOptInfo* rel);
extern bool is_redundant_derived_clause(RestrictInfo* rinfo, List* clauselist);
extern PathKey* make_canonical_pathkey(PlannerInfo* root, EquivalenceClass* eclass, Oid opfamily, int strategy, bool nulls_first);
/*
 * pathkeys.c
 *	  utilities for matching and building path keys
 */
typedef enum {
    PATHKEYS_EQUAL,    /* pathkeys are identical */
    PATHKEYS_BETTER1,  /* pathkey 1 is a superset of pathkey 2 */
    PATHKEYS_BETTER2,  /* vice versa */
    PATHKEYS_DIFFERENT /* neither pathkey includes the other */
} PathKeysComparison;

/* Passthrough data for standard_qp_callback */
typedef struct {
    List* tlist;         /* preprocessed query targetlist */
    List* activeWindows; /* active windows, if any */
    List* groupClause;   /* overrides parse->groupClause */
} standard_qp_extra;

extern List* canonicalize_pathkeys(PlannerInfo* root, List* pathkeys);
extern List* remove_param_pathkeys(PlannerInfo* root, List* pathkeys);
extern void construct_pathkeys(PlannerInfo *root, List *tlist, List *activeWindows,
                   List *groupClause, bool canonical);
extern PathKeysComparison compare_pathkeys(List* keys1, List* keys2);
extern bool pathkeys_contained_in(List* keys1, List* keys2);
extern Path* get_cheapest_path_for_pathkeys(
    List* paths, List* pathkeys, Relids required_outer, CostSelector cost_criterion);
extern Path* get_cheapest_fractional_path_for_pathkeys(
    List* paths, List* pathkeys, Relids required_outer, double fraction);
extern List* build_index_pathkeys(PlannerInfo* root, IndexOptInfo* index, ScanDirection scandir);
extern List* convert_subquery_pathkeys(PlannerInfo* root, RelOptInfo* rel, List* subquery_pathkeys);
extern List* build_join_pathkeys(PlannerInfo* root, RelOptInfo* joinrel, JoinType jointype, List* outer_pathkeys);
extern List* make_pathkeys_for_sortclauses(PlannerInfo* root, List* sortclauses, List* tlist, bool canonicalize);
extern void initialize_mergeclause_eclasses(PlannerInfo* root, RestrictInfo* restrictinfo);
extern void update_mergeclause_eclasses(PlannerInfo* root, RestrictInfo* restrictinfo);
extern List* find_mergeclauses_for_outer_pathkeys(PlannerInfo* root, List* pathkeys, List* restrictinfos);
extern List* select_outer_pathkeys_for_merge(PlannerInfo* root, List* mergeclauses, RelOptInfo* joinrel);
extern List* make_inner_pathkeys_for_merge(PlannerInfo* root, List* mergeclauses, List* outer_pathkeys);
extern List* trim_mergeclauses_for_inner_pathkeys(PlannerInfo* root, List* mergeclauses, List* pathkeys);

extern List* truncate_useless_pathkeys(PlannerInfo* root, RelOptInfo* rel, List* pathkeys);
extern bool has_useful_pathkeys(PlannerInfo* root, RelOptInfo* rel);
extern void set_path_rows(Path* path, double rows, double multiple = 1);
extern EquivalenceClass* get_expr_eqClass(PlannerInfo* root, Expr* expr);
extern void delete_eq_member(PlannerInfo* root, List* tlist, List* collectiveGroupExpr);
extern bool CheckPathUseGlobalPartIndex(Path* path);

extern void standard_qp_init(PlannerInfo *root, void *extra, List *tlist, List *activeWindows, List *groupClause);
extern void standard_qp_callback(PlannerInfo *root, void *extra);

#endif /* PATHS_H */
