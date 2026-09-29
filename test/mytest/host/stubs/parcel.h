#pragma once
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
namespace OHOS {
class Parcel;
struct Parcelable { virtual ~Parcelable() = default; virtual bool Marshalling(Parcel &) const = 0; };
class Parcel {
public:
    std::vector<uint8_t> bytes; size_t cursor = 0;
    size_t GetDataSize() const { return bytes.size(); }
    template<class T> bool Write(T v) { auto p = reinterpret_cast<uint8_t *>(&v); bytes.insert(bytes.end(),p,p+sizeof(v)); return true; }
    template<class T> bool Read(T &v) { if (bytes.size()-cursor < sizeof(v)) return false; memcpy(&v,bytes.data()+cursor,sizeof(v)); cursor+=sizeof(v); return true; }
    bool WriteInt32(int32_t v) { return Write(v); } bool ReadInt32(int32_t &v) { return Read(v); }
    bool WriteUint64(uint64_t v) { return Write(v); } bool ReadUint64(uint64_t &v) { return Read(v); }
    bool WriteUint32(uint32_t v) { return Write(v); } bool ReadUint32(uint32_t &v) { return Read(v); }
    bool WriteBool(bool v) { return Write(v); } bool ReadBool(bool &v) { return Read(v); }
    bool WriteString(const std::string &s) { WriteInt32(s.size()); bytes.insert(bytes.end(),s.begin(),s.end()); return true; }
    bool ReadString(std::string &s) { int32_t n; if (!ReadInt32(n) || n < 0 || (size_t)n > bytes.size()-cursor) return false; s.assign((char*)bytes.data()+cursor,n); cursor+=n; return true; }
};
}
