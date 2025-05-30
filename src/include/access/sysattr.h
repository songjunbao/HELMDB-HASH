/* -------------------------------------------------------------------------
 *
 * sysattr.h
 *	  openGauss system attribute definitions.
 *
 *
 * Portions Copyright (c) 1996-2012, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * src/include/access/sysattr.h
 *
 * -------------------------------------------------------------------------
 */
#ifndef SYSATTR_H
#define SYSATTR_H

/*
 * Attribute numbers for the system-defined attributes
 */
#define SelfItemPointerAttributeNumber (-1)
#define ObjectIdAttributeNumber (-2)
#define MinTransactionIdAttributeNumber (-3)
#define MinCommandIdAttributeNumber (-4)
#define MaxTransactionIdAttributeNumber (-5)
#define MaxCommandIdAttributeNumber (-6)
#define TableOidAttributeNumber (-7)
#ifdef PGXC

#define XC_NodeIdAttributeNumber (-8)
#define BucketIdAttributeNumber (-9)
#define UidAttributeNumber (-10)
#ifdef USE_SPQ
#define RootSelfItemPointerAttributeNumber (-11)
#define FirstLowInvalidHeapAttributeNumber (-12)
#else
#define FirstLowInvalidHeapAttributeNumber (-11)
#endif

#else
#define FirstLowInvalidHeapAttributeNumber (-8)
#endif

#endif /* SYSATTR_H */
