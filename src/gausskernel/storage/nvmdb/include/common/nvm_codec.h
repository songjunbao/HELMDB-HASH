#ifndef NVMDB_CODEC_H
#define NVMDB_CODEC_H

#include "common/nvm_types.h"

namespace NVMDB {

enum CODE_TYPE {
    CODE_ROWID = 1,
    CODE_INT32,
    CODE_UINT32,
    CODE_INT64,
    CODE_UINT64,
    CODE_FLOAT,
    CODE_VARCHAR,
    CODE_INVALID = 255,
};

void EncodeInt32(char *buf, int32 i);

int32 DecodeInt32(char *buf);

void EncodeUint32(char *buf, uint32 u);

uint32 DecodeUint32(const char *buf);

void EncodeUint64(char *buf, uint64 u);

uint64 DecodeUint64(const char *buf);

void EncodeInt64(char *buf, int64 i);

int64 DecodeInt64(char *buf);

void EncodeVarchar(char *buf, const char *data, int len);

void DecodeVarchar(char *buf, char *data, int maxlen);

}  // namespace NVMDB

#endif  // NVMDB_CODEC_H
