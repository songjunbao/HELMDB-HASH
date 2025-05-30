/*-------------------------------------------------------------------------
 *
 * kwlist_d.h
 *    List of keywords represented as a ScanKeywordList.
 *
 * Portions Copyright (c) 1996-2019, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * NOTES
 *  ******************************
 *  *** DO NOT EDIT THIS FILE! ***
 *  ******************************
 *
 *  It has been GENERATED by src/tools/gen_keywordlist.pl
 *
 *-------------------------------------------------------------------------
 */

#ifndef KWLIST_D_H
#define KWLIST_D_H

#include "parser/kwlookup.h"

static const char ScanKeywords_kw_string[] =
	"abort\0"
	"absolute\0"
	"access\0"
	"account\0"
	"action\0"
	"add\0"
	"admin\0"
	"after\0"
	"aggregate\0"
	"algorithm\0"
	"all\0"
	"also\0"
	"alter\0"
	"always\0"
	"analyse\0"
	"analyze\0"
	"and\0"
	"any\0"
	"app\0"
	"append\0"
	"archive\0"
	"array\0"
	"as\0"
	"asc\0"
	"assertion\0"
	"assignment\0"
	"asymmetric\0"
	"at\0"
	"attribute\0"
	"attrs\0"
	"audit\0"
	"authid\0"
	"authorization\0"
	"auto_increment\0"
	"autoextend\0"
	"automapped\0"
	"backward\0"
	"barrier\0"
	"before\0"
	"begin\0"
	"begin_non_anoyblock\0"
	"between\0"
	"bigint\0"
	"binary\0"
	"binary_double\0"
	"binary_integer\0"
	"bit\0"
	"blanks\0"
	"blob\0"
	"blockchain\0"
	"body\0"
	"boolean\0"
	"both\0"
	"bucketcnt\0"
	"buckets\0"
	"by\0"
	"byteawithoutorder\0"
	"byteawithoutorderwithequal\0"
	"cache\0"
	"call\0"
	"called\0"
	"cancelable\0"
	"cascade\0"
	"cascaded\0"
	"case\0"
	"cast\0"
	"catalog\0"
	"catalog_name\0"
	"chain\0"
	"change\0"
	"char\0"
	"character\0"
	"characteristics\0"
	"characterset\0"
	"charset\0"
	"check\0"
	"checkpoint\0"
	"class\0"
	"class_origin\0"
	"clean\0"
	"client\0"
	"client_master_key\0"
	"client_master_keys\0"
	"clob\0"
	"close\0"
	"cluster\0"
	"coalesce\0"
	"collate\0"
	"collation\0"
	"column\0"
	"column_encryption_key\0"
	"column_encryption_keys\0"
	"column_name\0"
	"columns\0"
	"comment\0"
	"comments\0"
	"commit\0"
	"committed\0"
	"compact\0"
	"compatible_illegal_chars\0"
	"compile\0"
	"complete\0"
	"completion\0"
	"compress\0"
	"concurrently\0"
	"condition\0"
	"configuration\0"
	"connect\0"
	"connection\0"
	"consistent\0"
	"constant\0"
	"constraint\0"
	"constraint_catalog\0"
	"constraint_name\0"
	"constraint_schema\0"
	"constraints\0"
	"content\0"
	"continue\0"
	"contview\0"
	"conversion\0"
	"convert\0"
	"coordinator\0"
	"coordinators\0"
	"copy\0"
	"cost\0"
	"create\0"
	"cross\0"
	"csn\0"
	"csv\0"
	"cube\0"
	"current\0"
	"current_catalog\0"
	"current_date\0"
	"current_role\0"
	"current_schema\0"
	"current_time\0"
	"current_timestamp\0"
	"current_user\0"
	"cursor\0"
	"cursor_name\0"
	"cycle\0"
	"data\0"
	"database\0"
	"datafile\0"
	"datanode\0"
	"datanodes\0"
	"datatype_cl\0"
	"date\0"
	"date_format\0"
	"day\0"
	"day_hour\0"
	"day_minute\0"
	"day_second\0"
	"dbcompatibility\0"
	"deallocate\0"
	"dec\0"
	"decimal\0"
	"declare\0"
	"decode\0"
	"default\0"
	"defaults\0"
	"deferrable\0"
	"deferred\0"
	"definer\0"
	"delete\0"
	"delimiter\0"
	"delimiters\0"
	"delta\0"
	"deltamerge\0"
	"desc\0"
	"deterministic\0"
	"diagnostics\0"
	"dictionary\0"
	"dims\0"
	"direct\0"
	"directory\0"
	"disable\0"
	"discard\0"
	"disconnect\0"
	"distinct\0"
	"distribute\0"
	"distribution\0"
	"do\0"
	"document\0"
	"documents\0"
	"domain\0"
	"double\0"
	"drop\0"
	"dumpfile\0"
	"duplicate\0"
	"each\0"
	"elabel\0"
	"elastic\0"
	"else\0"
	"enable\0"
	"enclosed\0"
	"encoding\0"
	"encrypted\0"
	"encrypted_value\0"
	"encryption\0"
	"encryption_type\0"
	"end\0"
	"ends\0"
	"enforced\0"
	"enum\0"
	"eol\0"
	"errors\0"
	"escape\0"
	"escaped\0"
	"escaping\0"
	"event\0"
	"events\0"
	"every\0"
	"except\0"
	"exchange\0"
	"exclude\0"
	"excluded\0"
	"excluding\0"
	"exclusive\0"
	"execute\0"
	"exists\0"
	"expired\0"
	"explain\0"
	"extension\0"
	"external\0"
	"extract\0"
	"false\0"
	"family\0"
	"fast\0"
	"features\0"
	"fenced\0"
	"fetch\0"
	"fields\0"
	"fileheader\0"
	"fill_missing_fields\0"
	"filler\0"
	"filter\0"
	"first\0"
	"fixed\0"
	"float\0"
	"following\0"
	"follows\0"
	"for\0"
	"force\0"
	"foreign\0"
	"formatter\0"
	"forward\0"
	"freeze\0"
	"from\0"
	"full\0"
	"function\0"
	"functions\0"
	"generated\0"
	"get\0"
	"global\0"
	"grant\0"
	"granted\0"
	"graph\0"
	"greatest\0"
	"group\0"
	"grouping\0"
	"groupparent\0"
	"handler\0"
	"having\0"
	"hdfsdirectory\0"
	"header\0"
	"hold\0"
	"hour\0"
	"hour_minute\0"
	"hour_second\0"
	"identified\0"
	"identity\0"
	"if\0"
	"ignore\0"
	"ignore_extra_data\0"
	"ilike\0"
	"immediate\0"
	"immutable\0"
	"implicit\0"
	"in\0"
	"include\0"
	"including\0"
	"increment\0"
	"incremental\0"
	"index\0"
	"indexes\0"
	"infile\0"
	"inherit\0"
	"inherits\0"
	"initial\0"
	"initially\0"
	"initrans\0"
	"inline\0"
	"inner\0"
	"inout\0"
	"input\0"
	"insensitive\0"
	"insert\0"
	"instead\0"
	"int\0"
	"integer\0"
	"internal\0"
	"intersect\0"
	"interval\0"
	"into\0"
	"invisible\0"
	"invoker\0"
	"ip\0"
	"is\0"
	"isnull\0"
	"isolation\0"
	"join\0"
	"jsonarray\0"
	"key\0"
	"key_path\0"
	"key_store\0"
	"kill\0"
	"label\0"
	"language\0"
	"large\0"
	"last\0"
	"lc_collate\0"
	"lc_ctype\0"
	"leading\0"
	"leakproof\0"
	"least\0"
	"left\0"
	"less\0"
	"level\0"
	"like\0"
	"limit\0"
	"lines\0"
	"list\0"
	"listen\0"
	"load\0"
	"local\0"
	"localtime\0"
	"localtimestamp\0"
	"location\0"
	"lock\0"
	"locked\0"
	"log\0"
	"logging\0"
	"login_any\0"
	"login_failure\0"
	"login_success\0"
	"logout\0"
	"loop\0"
	"mapping\0"
	"masking\0"
	"master\0"
	"match\0"
	"matched\0"
	"materialized\0"
	"maxextents\0"
	"maxsize\0"
	"maxtrans\0"
	"maxvalue\0"
	"merge\0"
	"message_text\0"
	"method\0"
	"minextents\0"
	"minus\0"
	"minute\0"
	"minute_second\0"
	"minvalue\0"
	"mode\0"
	"model\0"
	"modify\0"
	"month\0"
	"move\0"
	"movement\0"
	"mysql_errno\0"
	"name\0"
	"names\0"
	"national\0"
	"natural\0"
	"nchar\0"
	"next\0"
	"no\0"
	"nocompress\0"
	"nocycle\0"
	"node\0"
	"nologging\0"
	"nomaxvalue\0"
	"nominvalue\0"
	"none\0"
	"not\0"
	"nothing\0"
	"notify\0"
	"notnull\0"
	"nowait\0"
	"null\0"
	"nullcols\0"
	"nullif\0"
	"nulls\0"
	"number\0"
	"numeric\0"
	"numstr\0"
	"nvarchar\0"
	"nvarchar2\0"
	"nvl\0"
	"object\0"
	"of\0"
	"off\0"
	"offset\0"
	"oids\0"
	"on\0"
	"only\0"
	"operator\0"
	"optimization\0"
	"option\0"
	"optionally\0"
	"options\0"
	"or\0"
	"order\0"
	"out\0"
	"outer\0"
	"outfile\0"
	"over\0"
	"overlaps\0"
	"overlay\0"
	"owned\0"
	"owner\0"
	"package\0"
	"packages\0"
	"parser\0"
	"partial\0"
	"partition\0"
	"partitions\0"
	"passing\0"
	"password\0"
	"pctfree\0"
	"per\0"
	"percent\0"
	"performance\0"
	"perm\0"
	"placing\0"
	"plan\0"
	"plans\0"
	"policy\0"
	"pool\0"
	"position\0"
	"precedes\0"
	"preceding\0"
	"precision\0"
	"predict\0"
	"preferred\0"
	"prefix\0"
	"prepare\0"
	"prepared\0"
	"preserve\0"
	"primary\0"
	"prior\0"
	"priorer\0"
	"private\0"
	"privilege\0"
	"privileges\0"
	"procedural\0"
	"procedure\0"
	"profile\0"
	"publication\0"
	"publish\0"
	"purge\0"
	"query\0"
	"quote\0"
	"randomized\0"
	"range\0"
	"ratio\0"
	"raw\0"
	"read\0"
	"real\0"
	"reassign\0"
	"rebuild\0"
	"recheck\0"
	"recursive\0"
	"recyclebin\0"
	"redisanyvalue\0"
	"ref\0"
	"references\0"
	"refresh\0"
	"reindex\0"
	"reject\0"
	"relative\0"
	"release\0"
	"reloptions\0"
	"remote\0"
	"remove\0"
	"rename\0"
	"repeat\0"
	"repeatable\0"
	"replace\0"
	"replica\0"
	"reset\0"
	"resize\0"
	"resource\0"
	"restart\0"
	"restrict\0"
	"return\0"
	"returned_sqlstate\0"
	"returning\0"
	"returns\0"
	"reuse\0"
	"revoke\0"
	"right\0"
	"role\0"
	"roles\0"
	"rollback\0"
	"rollup\0"
	"rotation\0"
	"row\0"
	"row_count\0"
	"rownum\0"
	"rows\0"
	"rowtype\0"
	"rule\0"
	"sample\0"
	"savepoint\0"
	"schedule\0"
	"schema\0"
	"schema_name\0"
	"scroll\0"
	"search\0"
	"second\0"
	"security\0"
	"select\0"
	"select_array\0"
	"separator\0"
	"sequence\0"
	"sequences\0"
	"serializable\0"
	"server\0"
	"session\0"
	"session_user\0"
	"set\0"
	"setof\0"
	"sets\0"
	"share\0"
	"shippable\0"
	"shortestpath\0"
	"show\0"
	"shrink\0"
	"shutdown\0"
	"siblings\0"
	"similar\0"
	"simple\0"
	"size\0"
	"skip\0"
	"slave\0"
	"slice\0"
	"smalldatetime\0"
	"smalldatetime_format\0"
	"smallint\0"
	"snapshot\0"
	"some\0"
	"source\0"
	"space\0"
	"specification\0"
	"spill\0"
	"split\0"
	"sql\0"
	"stable\0"
	"stacked\0"
	"standalone\0"
	"start\0"
	"starting\0"
	"starts\0"
	"statement\0"
	"statement_id\0"
	"statistics\0"
	"stdin\0"
	"stdout\0"
	"storage\0"
	"store\0"
	"stored\0"
	"stratify\0"
	"stream\0"
	"strict\0"
	"strip\0"
	"subclass_origin\0"
	"subpartition\0"
	"subpartitions\0"
	"subscription\0"
	"substring\0"
	"symmetric\0"
	"synonym\0"
	"sys_refcursor\0"
	"sysdate\0"
	"sysid\0"
	"system\0"
	"table\0"
	"table_name\0"
	"tables\0"
	"tablesample\0"
	"tablespace\0"
	"target\0"
	"temp\0"
	"template\0"
	"temporary\0"
	"terminated\0"
	"text\0"
	"than\0"
	"then\0"
	"time\0"
	"time_format\0"
	"timecapsule\0"
	"timestamp\0"
	"timestamp_format\0"
	"timestampdiff\0"
	"tinyint\0"
	"to\0"
	"toarray\0"
	"todoc\0"
	"tograph\0"
	"totable\0"
	"trailing\0"
	"transaction\0"
	"transform\0"
	"treat\0"
	"trigger\0"
	"trim\0"
	"true\0"
	"truncate\0"
	"trusted\0"
	"tsfield\0"
	"tstag\0"
	"tstime\0"
	"type\0"
	"types\0"
	"unbounded\0"
	"uncommitted\0"
	"unencrypted\0"
	"union\0"
	"unique\0"
	"unknown\0"
	"unlimited\0"
	"unlisten\0"
	"unlock\0"
	"unlogged\0"
	"until\0"
	"unusable\0"
	"unwind\0"
	"update\0"
	"use\0"
	"useeof\0"
	"user\0"
	"using\0"
	"vacuum\0"
	"valid\0"
	"validate\0"
	"validation\0"
	"validator\0"
	"value\0"
	"values\0"
	"varchar\0"
	"varchar2\0"
	"variables\0"
	"variadic\0"
	"varying\0"
	"vcgroup\0"
	"vectors\0"
	"verbose\0"
	"verify\0"
	"version\0"
	"view\0"
	"visible\0"
	"vlabel\0"
	"volatile\0"
	"wait\0"
	"warnings\0"
	"weak\0"
	"when\0"
	"where\0"
	"while\0"
	"whitespace\0"
	"window\0"
	"with\0"
	"within\0"
	"without\0"
	"work\0"
	"workload\0"
	"wrapper\0"
	"write\0"
	"xml\0"
	"xmlattributes\0"
	"xmlconcat\0"
	"xmlelement\0"
	"xmlexists\0"
	"xmlforest\0"
	"xmlparse\0"
	"xmlpi\0"
	"xmlroot\0"
	"xmlserialize\0"
	"year\0"
	"year_month\0"
	"yes\0"
	"zone";

static const uint16 ScanKeywords_kw_offsets[] = {
	0,
	6,
	15,
	22,
	30,
	37,
	41,
	47,
	53,
	63,
	73,
	77,
	82,
	88,
	95,
	103,
	111,
	115,
	119,
	123,
	130,
	138,
	144,
	147,
	151,
	161,
	172,
	183,
	186,
	196,
	202,
	208,
	215,
	229,
	244,
	255,
	266,
	275,
	283,
	290,
	296,
	316,
	324,
	331,
	338,
	352,
	367,
	371,
	378,
	383,
	394,
	399,
	407,
	412,
	422,
	430,
	433,
	451,
	478,
	484,
	489,
	496,
	507,
	515,
	524,
	529,
	534,
	542,
	555,
	561,
	568,
	573,
	583,
	599,
	612,
	620,
	626,
	637,
	643,
	656,
	662,
	669,
	687,
	706,
	711,
	717,
	725,
	734,
	742,
	752,
	759,
	781,
	804,
	816,
	824,
	832,
	841,
	848,
	858,
	866,
	891,
	899,
	908,
	919,
	928,
	941,
	951,
	965,
	973,
	984,
	995,
	1004,
	1015,
	1034,
	1050,
	1068,
	1080,
	1088,
	1097,
	1106,
	1117,
	1125,
	1137,
	1150,
	1155,
	1160,
	1167,
	1173,
	1177,
	1181,
	1186,
	1194,
	1210,
	1223,
	1236,
	1251,
	1264,
	1282,
	1295,
	1302,
	1314,
	1320,
	1325,
	1334,
	1343,
	1352,
	1362,
	1374,
	1379,
	1391,
	1395,
	1404,
	1415,
	1426,
	1442,
	1453,
	1457,
	1465,
	1473,
	1480,
	1488,
	1497,
	1508,
	1517,
	1525,
	1532,
	1542,
	1553,
	1559,
	1570,
	1575,
	1589,
	1601,
	1612,
	1617,
	1624,
	1634,
	1642,
	1650,
	1661,
	1670,
	1681,
	1694,
	1697,
	1706,
	1716,
	1723,
	1730,
	1735,
	1744,
	1754,
	1759,
	1766,
	1774,
	1779,
	1786,
	1795,
	1804,
	1814,
	1830,
	1841,
	1857,
	1861,
	1866,
	1875,
	1880,
	1884,
	1891,
	1898,
	1906,
	1915,
	1921,
	1928,
	1934,
	1941,
	1950,
	1958,
	1967,
	1977,
	1987,
	1995,
	2002,
	2010,
	2018,
	2028,
	2037,
	2045,
	2051,
	2058,
	2063,
	2072,
	2079,
	2085,
	2092,
	2103,
	2123,
	2130,
	2137,
	2143,
	2149,
	2155,
	2165,
	2173,
	2177,
	2183,
	2191,
	2201,
	2209,
	2216,
	2221,
	2226,
	2235,
	2245,
	2255,
	2259,
	2266,
	2272,
	2280,
	2286,
	2295,
	2301,
	2310,
	2322,
	2330,
	2337,
	2351,
	2358,
	2363,
	2368,
	2380,
	2392,
	2403,
	2412,
	2415,
	2422,
	2440,
	2446,
	2456,
	2466,
	2475,
	2478,
	2486,
	2496,
	2506,
	2518,
	2524,
	2532,
	2539,
	2547,
	2556,
	2564,
	2574,
	2583,
	2590,
	2596,
	2602,
	2608,
	2620,
	2627,
	2635,
	2639,
	2647,
	2656,
	2666,
	2675,
	2680,
	2690,
	2698,
	2701,
	2704,
	2711,
	2721,
	2726,
	2736,
	2740,
	2749,
	2759,
	2764,
	2770,
	2779,
	2785,
	2790,
	2801,
	2810,
	2818,
	2828,
	2834,
	2839,
	2844,
	2850,
	2855,
	2861,
	2867,
	2872,
	2879,
	2884,
	2890,
	2900,
	2915,
	2924,
	2929,
	2936,
	2940,
	2948,
	2958,
	2972,
	2986,
	2993,
	2998,
	3006,
	3014,
	3021,
	3027,
	3035,
	3048,
	3059,
	3067,
	3076,
	3085,
	3091,
	3104,
	3111,
	3122,
	3128,
	3135,
	3149,
	3158,
	3163,
	3169,
	3176,
	3182,
	3187,
	3196,
	3208,
	3213,
	3219,
	3228,
	3236,
	3242,
	3247,
	3250,
	3261,
	3269,
	3274,
	3284,
	3295,
	3306,
	3311,
	3315,
	3323,
	3330,
	3338,
	3345,
	3350,
	3359,
	3366,
	3372,
	3379,
	3387,
	3394,
	3403,
	3413,
	3417,
	3424,
	3427,
	3431,
	3438,
	3443,
	3446,
	3451,
	3460,
	3473,
	3480,
	3491,
	3499,
	3502,
	3508,
	3512,
	3518,
	3526,
	3531,
	3540,
	3548,
	3554,
	3560,
	3568,
	3577,
	3584,
	3592,
	3602,
	3613,
	3621,
	3630,
	3638,
	3642,
	3650,
	3662,
	3667,
	3675,
	3680,
	3686,
	3693,
	3698,
	3707,
	3716,
	3726,
	3736,
	3744,
	3754,
	3761,
	3769,
	3778,
	3787,
	3795,
	3801,
	3809,
	3817,
	3827,
	3838,
	3849,
	3859,
	3867,
	3879,
	3887,
	3893,
	3899,
	3905,
	3916,
	3922,
	3928,
	3932,
	3937,
	3942,
	3951,
	3959,
	3967,
	3977,
	3988,
	4002,
	4006,
	4017,
	4025,
	4033,
	4040,
	4049,
	4057,
	4068,
	4075,
	4082,
	4089,
	4096,
	4107,
	4115,
	4123,
	4129,
	4136,
	4145,
	4153,
	4162,
	4169,
	4187,
	4197,
	4205,
	4211,
	4218,
	4224,
	4229,
	4235,
	4244,
	4251,
	4260,
	4264,
	4274,
	4281,
	4286,
	4294,
	4299,
	4306,
	4316,
	4325,
	4332,
	4344,
	4351,
	4358,
	4365,
	4374,
	4381,
	4394,
	4404,
	4413,
	4423,
	4436,
	4443,
	4451,
	4464,
	4468,
	4474,
	4479,
	4485,
	4495,
	4508,
	4513,
	4520,
	4529,
	4538,
	4546,
	4553,
	4558,
	4563,
	4569,
	4575,
	4589,
	4610,
	4619,
	4628,
	4633,
	4640,
	4646,
	4660,
	4666,
	4672,
	4676,
	4683,
	4691,
	4702,
	4708,
	4717,
	4724,
	4734,
	4747,
	4758,
	4764,
	4771,
	4779,
	4785,
	4792,
	4801,
	4808,
	4815,
	4821,
	4837,
	4850,
	4864,
	4877,
	4887,
	4897,
	4905,
	4919,
	4927,
	4933,
	4940,
	4946,
	4957,
	4964,
	4976,
	4987,
	4994,
	4999,
	5008,
	5018,
	5029,
	5034,
	5039,
	5044,
	5049,
	5061,
	5073,
	5083,
	5100,
	5114,
	5122,
	5125,
	5133,
	5139,
	5147,
	5155,
	5164,
	5176,
	5186,
	5192,
	5200,
	5205,
	5210,
	5219,
	5227,
	5235,
	5241,
	5248,
	5253,
	5259,
	5269,
	5281,
	5293,
	5299,
	5306,
	5314,
	5324,
	5333,
	5340,
	5349,
	5355,
	5364,
	5371,
	5378,
	5382,
	5389,
	5394,
	5400,
	5407,
	5413,
	5422,
	5433,
	5443,
	5449,
	5456,
	5464,
	5473,
	5483,
	5492,
	5500,
	5508,
	5516,
	5524,
	5531,
	5539,
	5544,
	5552,
	5559,
	5568,
	5573,
	5582,
	5587,
	5592,
	5598,
	5604,
	5615,
	5622,
	5627,
	5634,
	5642,
	5647,
	5656,
	5664,
	5670,
	5674,
	5688,
	5698,
	5709,
	5719,
	5729,
	5738,
	5744,
	5752,
	5765,
	5770,
	5781,
	5785,
};

#define SCANKEYWORDS_NUM_KEYWORDS 697

static int
ScanKeywords_hash_func(const void *key, size_t keylen)
{
	static const int16 h[1395] = {
		530,   32767, 1126,  558,   501,   -1027, 32767, 0,
		564,   106,   -31,   -109,  32767, 32767, 1497,  -410,
		520,   32767, 32767, 0,     32767, 32767, 32767, 0,
		32767, -348,  32767, 32767, 0,     32767, 32767, 186,
		32767, 0,     634,   195,   248,   -43,   -383,  -475,
		32767, -841,  32767, 1006,  32767, 32767, 32767, 240,
		32767, 0,     57,    32767, 968,   655,   384,   79,
		207,   32767, 315,   453,   0,     0,     -89,   460,
		32767, 32767, 32767, 32767, 32767, 32767, 32767, 32767,
		32767, 146,   -200,  32767, -1270, 484,   -208,  -1634,
		-23,   32767, 2,     32767, 219,   -449,  134,   -294,
		32767, 158,   32767, 32767, -868,  645,   728,   32767,
		0,     32767, 435,   437,   32767, 258,   -11,   32767,
		32767, 32767, 32767, 848,   489,   417,   32767, -1149,
		824,   133,   32767, 32767, 689,   32767, 82,    -145,
		32767, 32767, 0,     384,   32767, 0,     0,     410,
		32767, 482,   199,   477,   32767, -177,  32767, 32767,
		167,   32767, 32767, 362,   32767, 930,   -81,   -561,
		152,   32767, 0,     312,   0,     589,   0,     0,
		32767, 258,   -478,  32767, 443,   0,     32767, 518,
		58,    32767, -114,  -211,  32767, 1580,  32767, 32767,
		-894,  32767, 32767, 32767, 0,     -1782, 32767, 388,
		32767, 196,   600,   32767, 32767, 32767, 0,     -625,
		32767, 32767, 616,   32767, -284,  0,     1556,  43,
		0,     32767, 0,     32767, 244,   298,   84,    74,
		-662,  602,   -59,   32767, 1447,  555,   33,    747,
		5,     459,   32767, 32767, 420,   509,   -283,  32767,
		1579,  32767, 0,     447,   32767, -215,  511,   45,
		32767, 32767, 32767, 32767, 32767, -136,  -358,  32767,
		0,     32767, 656,   0,     419,   0,     867,   -130,
		861,   0,     32767, -474,  0,     0,     32767, -222,
		-86,   32767, 32767, -130,  1267,  0,     1046,  -205,
		32767, 85,    32767, 531,   1072,  899,   393,   -1,
		0,     -464,  32767, -376,  317,   32767, 32767, 32767,
		1472,  193,   32767, 32767, -526,  32767, 0,     32767,
		32767, 239,   -873,  32767, 32767, 693,   0,     -40,
		-562,  32767, -265,  0,     537,   -1447, 32767, 32767,
		0,     51,    32767, 641,   134,   32767, 151,   130,
		32767, 32767, -99,   605,   56,    446,   508,   -31,
		32767, 32767, -1061, 89,    858,   32767, 230,   -560,
		32767, 0,     32767, 226,   75,    478,   1344,  -138,
		32767, 311,   154,   32767, 1483,  0,     32767, 0,
		32767, 32767, 32767, 578,   32767, 32767, 112,   795,
		32767, 0,     266,   -402,  32767, 628,   -96,   32767,
		703,   32767, 329,   32767, -99,   -186,  32767, -371,
		319,   132,   153,   32767, 692,   32767, -203,  0,
		32767, -665,  0,     164,   32767, 32767, 668,   -522,
		32767, 1586,  424,   32767, 480,   32767, 32767, 2016,
		30,    -161,  32767, 32767, 32767, 32767, 32767, 859,
		32767, 107,   0,     32767, 391,   158,   32767, 262,
		1527,  32767, 807,   32767, 645,   267,   -437,  32767,
		121,   32767, 263,   285,   -929,  -273,  32767, 32767,
		-5,    -442,  32767, -323,  32767, 0,     32767, 32767,
		32767, 532,   32767, 0,     32767, 30,    348,   289,
		32767, 198,   32767, 181,   32767, 32767, 32767, 32767,
		0,     32767, 124,   149,   32767, 32767, 32767, 201,
		114,   32767, 0,     32767, 936,   0,     -428,  538,
		-335,  0,     100,   32767, 32767, 124,   443,   32767,
		-275,  1221,  -1068, 32767, -283,  462,   32767, 156,
		487,   -484,  1287,  32767, 461,   0,     389,   649,
		32767, 219,   -166,  32767, 0,     0,     32767, 32767,
		327,   32767, 0,     30,    0,     32767, 0,     32767,
		0,     32767, 587,   448,   32767, 301,   32767, -476,
		-415,  0,     556,   32767, 32767, 32767, 32767, 900,
		0,     32767, 312,   32767, -762,  32767, 32767, 32767,
		32767, 155,   32767, 32767, 0,     617,   -1745, 814,
		20,    65,    587,   32767, 38,    0,     293,   0,
		570,   32767, -205,  485,   165,   0,     474,   32767,
		50,    0,     32767, 32767, 0,     163,   508,   -37,
		1389,  -167,  521,   223,   32767, -287,  32767, 32767,
		32767, 327,   0,     32767, 287,   32767, 32767, 4,
		-40,   1696,  32767, 32767, 32767, 32767, -134,  32767,
		0,     32767, 32767, 32767, 1051,  588,   -195,  131,
		32767, 0,     -1335, 214,   675,   32767, -272,  257,
		0,     0,     358,   727,   0,     64,    -853,  -43,
		695,   0,     0,     32767, 386,   564,   153,   1457,
		-320,  80,    222,   32767, 32767, -166,  1341,  -918,
		32767, 32767, 0,     32767, 2325,  150,   32767, -215,
		32767, 412,   -248,  644,   1331,  0,     550,   -122,
		32767, 32767, -389,  549,   32767, 32767, -632,  32767,
		32767, 179,   32767, 43,    206,   1486,  32767, 32767,
		-1501, 656,   32767, -778,  0,     2,     335,   32767,
		308,   85,    67,    0,     32767, 32767, -547,  -375,
		180,   -839,  630,   -171,  32767, 8,     32767, 375,
		430,   -1466, 84,    32767, 32767, 0,     32767, 32767,
		1699,  -243,  32767, 32767, 32767, 1255,  32767, 1800,
		0,     32767, 32767, 32767, 293,   32767, 1273,  480,
		-438,  32767, 32767, 403,   32767, 182,   32767, 333,
		32767, 0,     32767, 32767, 452,   465,   32767, 0,
		477,   425,   30,    32767, 173,   241,   32767, 32767,
		32767, 32767, -1001, 32767, 612,   1146,  0,     32767,
		424,   -419,  0,     675,   509,   518,   -1138, 32767,
		44,    151,   -322,  305,   32767, 122,   681,   32767,
		0,     32767, -1191, 0,     -1219, 685,   32767, 32767,
		630,   202,   0,     32767, 275,   32767, 311,   -284,
		-113,  32767, -840,  32767, -535,  238,   32767, 32767,
		192,   126,   -164,  487,   -687,  32767, 621,   597,
		32767, 0,     32767, 1687,  0,     467,   148,   1696,
		32767, 674,   -205,  32767, 32767, 551,   32767, 307,
		32767, 608,   -419,  0,     236,   32767, 269,   32767,
		377,   32767, 32767, 0,     251,   510,   32767, 596,
		-35,   32767, 66,    -382,  310,   -607,  145,   46,
		19,    0,     32767, 32767, 32767, 951,   32767, 534,
		32767, 0,     0,     32767, 278,   32767, 32767, 0,
		524,   0,     32767, -964,  129,   32767, 0,     -380,
		211,   32767, -342,  249,   2327,  338,   32767, 93,
		1941,  32767, 164,   -982,  160,   235,   314,   32767,
		97,    32767, 0,     -10,   159,   343,   32767, 32767,
		1129,  -76,   32767, 32767, 32767, 1487,  125,   32767,
		281,   32767, 32767, 160,   260,   420,   0,     -191,
		-474,  32767, 69,    822,   0,     32767, 32767, 32767,
		0,     284,   32767, 256,   0,     0,     0,     292,
		32767, 32767, 1640,  32767, -618,  32767, 401,   32767,
		83,    -345,  402,   71,    32767, 32767, -303,  -467,
		677,   32767, -1060, 32767, 0,     0,     314,   32767,
		7,     0,     674,   -477,  32767, 32767, 150,   32767,
		-400,  418,   32767, -268,  0,     672,   0,     -125,
		0,     659,   -446,  563,   0,     0,     649,   32767,
		0,     301,   291,   0,     32767, 32767, 202,   0,
		32767, 32767, 32767, 32767, -122,  32767, 32767, -1372,
		398,   0,     32767, 32767, 191,   -136,  0,     32767,
		0,     644,   32767, 32767, 32767, 347,   0,     31,
		0,     -721,  32767, 32767, 354,   536,   32767, 32767,
		-504,  194,   0,     -143,  739,   80,    0,     22,
		-540,  0,     538,   -428,  -365,  145,   -1263, 32767,
		351,   634,   32767, 32767, 32767, 32767, 32767, 32767,
		-110,  32767, 0,     0,     32767, 489,   0,     -247,
		32767, 32767, 32767, 32767, 32767, 700,   32767, 32767,
		0,     32767, 0,     32767, 492,   910,   24,    32767,
		32767, 0,     326,   0,     239,   32767, 0,     0,
		401,   40,    1040,  32767, 81,    32767, 32767, -1462,
		35,    32767, 32767, 32767, -1488, 1349,  32767, 547,
		-87,   32767, 32767, 32767, 649,   -29,   32767, 0,
		98,    32767, 63,    234,   0,     -463,  32767, -9,
		221,   277,   32767, 32767, 0,     0,     -894,  32767,
		0,     513,   -49,   660,   1941,  -55,   299,   32767,
		32767, 68,    32767, 32767, 627,   -524,  -443,  187,
		551,   757,   361,   0,     32767, 0,     -166,  245,
		32767, 32767, 683,   32767, 32767, -166,  1413,  0,
		141,   149,   32767, 1544,  147,   726,   842,   157,
		32767, 356,   623,   1028,  201,   32767, 32767, 0,
		32767, 32767, 72,    581,   -253,  196,   0,     459,
		261,   32767, 32767, 32767, -399,  207,   94,    32767,
		342,   32767, 32767, 0,     32767, 177,   781,   32767,
		814,   0,     32767, 32767, 1289,  436,   32767, 32767,
		664,   0,     32767, -213,  174,   430,   1752,  297,
		190,   32767, 78,    411,   32767, 32767, 32767, 32767,
		0,     32767, 32767, 32767, 32767, 32767, 165,   32767,
		463,   181,   32767, 588,   0,     781,   32767, 0,
		1361,  32767, 32767, 32767, 477,   136,   226,   -403,
		32767, 32767, 32767, 32767, 0,     32767, 0,     670,
		309,   1133,  669,   0,     -133,  -274,  32767, 0,
		0,     472,   554,   32767, 102,   810,   624,   32767,
		-1341, 113,   32767, 32767, 32767, -248,  32767, 172,
		727,   203,   32767, 761,   717,   0,     32767, 0,
		0,     -890,  32767, 32767, 293,   -1174, 0,     32767,
		32767, 32767, 32767, 388,   1377,  32767, 32767, 523,
		-1414, 32767, 32767, 695,   322,   32767, 32767, -337,
		32767, 581,   0,     32767, 0,     -1130, 32767, 32767,
		23,    921,   137,   32767, 0,     587,   32767, 445,
		32767, -124,  -1477, 0,     242,   32767, 36,    448,
		512,   32767, 1291,  257,   32767, -406,  0,     0,
		251,   32767, 0,     0,     0,     529,   32767, 32767,
		-233,  32767, 32767, 286,   32767, 11,    32767, 32767,
		392,   -1381, -263,  32767, 0,     0,     32767, 645,
		0,     648,   -162,  0,     -205,  32767, 32767, -101,
		32767, -45,   27,    32767, 4,     242,   0,     32767,
		668,   -1093, 32767, 32767, 228,   54,    32767, 320,
		32767, 774,   0,     0,     -37,   32767, -113,  0,
		-514,  336,   32767, 32767, 628,   -469,  32767, 32767,
		32767, 609,   0,     0,     0,     32767, 81,    32767,
		0,     -413,  32767, 467,   132,   -119,  32767, 32767,
		32767, 473,   32767, 218,   32767, 10,    537,   857,
		-82,   0,     32767, 341,   32767, 32767, 32767, 32767,
		32767, 258,   0
	};

	const unsigned char *k = (const unsigned char *) key;
	uint32		a = 0;
	uint32		b = 1;

	while (keylen--)
	{
		unsigned char c = *k++ | 0x20;

		a = a * 257 + c;
		b = b * 17 + c;
	}
	return h[a % 1395] + h[b % 1395];
}

const ScanKeywordList ScanKeywords = {
	ScanKeywords_kw_string,
	ScanKeywords_kw_offsets,
	ScanKeywords_hash_func,
	SCANKEYWORDS_NUM_KEYWORDS,
	26
};

#endif							/* KWLIST_D_H */
