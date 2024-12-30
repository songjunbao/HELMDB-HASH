#ifndef VECTOR_H
#define VECTOR_H

#include "utils/numeric.h"

#define VECTOR_MAX_DIM 16000

#define VECTOR_SIZE(_dim)		(offsetof(Vector, x) + sizeof(float)*(_dim))
#define DatumGetVector(x)		((Vector *) PG_DETOAST_DATUM(x))
#define PG_GETARG_VECTOR_P(x)	DatumGetVector(PG_GETARG_DATUM(x))
#define PG_RETURN_VECTOR_P(x)	PG_RETURN_POINTER(x)

typedef struct Vector
{
	int32		vl_len_;		/* varlena header (do not touch directly!) */
	int16		dim;			/* number of dimensions */
	int16		unused;			/* reserved for future use, always zero */
	float		x[FLEXIBLE_ARRAY_MEMBER];
}			Vector;

Vector	   *InitVector(int dim);
void		PrintVector(char *msg, Vector * vector);
int			vector_cmp_internal(Vector * a, Vector * b);

/* functions in vector.cpp */
extern Datum vector_in(PG_FUNCTION_ARGS);
extern Datum vector_out(PG_FUNCTION_ARGS);
extern Datum vector_typmod_in(PG_FUNCTION_ARGS);
extern Datum vector_recv(PG_FUNCTION_ARGS);
extern Datum vector_send(PG_FUNCTION_ARGS);

extern Datum vector(PG_FUNCTION_ARGS);
extern Datum array_to_vector(PG_FUNCTION_ARGS);
extern Datum vector_to_float4(PG_FUNCTION_ARGS);

extern Datum l2_distance(PG_FUNCTION_ARGS);
extern Datum vector_l2_squared_distance(PG_FUNCTION_ARGS);
extern Datum inner_product(PG_FUNCTION_ARGS);
extern Datum vector_negative_inner_product(PG_FUNCTION_ARGS);
extern Datum cosine_distance(PG_FUNCTION_ARGS);
extern Datum vector_spherical_distance(PG_FUNCTION_ARGS);
extern Datum l1_distance(PG_FUNCTION_ARGS);

extern Datum vector_dims(PG_FUNCTION_ARGS);
extern Datum vector_norm(PG_FUNCTION_ARGS);
extern Datum l2_normalize(PG_FUNCTION_ARGS);
extern Datum vector_add(PG_FUNCTION_ARGS);
extern Datum vector_sub(PG_FUNCTION_ARGS);
extern Datum vector_mul(PG_FUNCTION_ARGS);
extern Datum vector_concat(PG_FUNCTION_ARGS);
extern Datum subvector(PG_FUNCTION_ARGS);

extern Datum vector_lt(PG_FUNCTION_ARGS);
extern Datum vector_le(PG_FUNCTION_ARGS);
extern Datum vector_eq(PG_FUNCTION_ARGS);
extern Datum vector_ne(PG_FUNCTION_ARGS);
extern Datum vector_ge(PG_FUNCTION_ARGS);
extern Datum vector_gt(PG_FUNCTION_ARGS);
extern Datum vector_cmp(PG_FUNCTION_ARGS);

extern Datum vector_accum(PG_FUNCTION_ARGS);
extern Datum vector_combine(PG_FUNCTION_ARGS);
extern Datum vector_avg(PG_FUNCTION_ARGS);


#endif
