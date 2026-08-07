#include "NpzVolume.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <zlib.h>

#if defined(_MSC_VER)
#define NPZ_FSEEK _fseeki64
#else
#define NPZ_FSEEK fseeko
#endif

namespace npz
{

const char *dtypeName(DType t)
{
    switch (t)
    {
    case DType::Bool: return "bool";
    case DType::Int8: return "int8";
    case DType::UInt8: return "uint8";
    case DType::Int16: return "int16";
    case DType::UInt16: return "uint16";
    case DType::Int32: return "int32";
    case DType::UInt32: return "uint32";
    case DType::Int64: return "int64";
    case DType::UInt64: return "uint64";
    case DType::Float16: return "float16";
    case DType::Float32: return "float32";
    case DType::Float64: return "float64";
    default: return "unknown";
    }
}

bool dtypeIsInteger(DType t)
{
    switch (t)
    {
    case DType::Bool:
    case DType::Int8:
    case DType::UInt8:
    case DType::Int16:
    case DType::UInt16:
    case DType::Int32:
    case DType::UInt32:
    case DType::Int64:
    case DType::UInt64:
        return true;
    default:
        return false;
    }
}

size_t dtypeSize(DType t)
{
    switch (t)
    {
    case DType::Bool:
    case DType::Int8:
    case DType::UInt8: return 1;
    case DType::Int16:
    case DType::UInt16:
    case DType::Float16: return 2;
    case DType::Int32:
    case DType::UInt32:
    case DType::Float32: return 4;
    case DType::Int64:
    case DType::UInt64:
    case DType::Float64: return 8;
    default: return 0;
    }
}

size_t ArrayInfo::elementCount() const
{
    size_t n = 1;
    for (size_t d : shape)
        n *= d;
    return n;
}

std::string ArrayInfo::shapeString() const
{
    std::ostringstream os;
    os << '(';
    for (size_t i = 0; i < shape.size(); ++i)
    {
        if (i > 0)
            os << ", ";
        os << shape[i];
    }
    os << ')';
    return os.str();
}

namespace
{

bool hasSuffixCi(const std::string &s, const std::string &suffix)
{
    if (s.size() < suffix.size())
        return false;
    return std::equal(suffix.rbegin(), suffix.rend(), s.rbegin(),
                      [](char a, char b)
                      { return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b)); });
}

void setError(std::string *error, const std::string &message)
{
    if (error)
        *error = message;
}

uint16_t rd16(const uint8_t *p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }

uint32_t rd32(const uint8_t *p)
{
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

uint64_t rd64(const uint8_t *p)
{
    return static_cast<uint64_t>(rd32(p)) | (static_cast<uint64_t>(rd32(p + 4)) << 32);
}

// RAII wrapper so the many early returns below cannot leak the handle.
struct FileHandle
{
    FILE *f = nullptr;
    ~FileHandle()
    {
        if (f)
            std::fclose(f);
    }
};

bool readAt(FILE *f, uint64_t offset, void *dst, size_t count)
{
    if (count == 0)
        return true;
    if (NPZ_FSEEK(f, static_cast<int64_t>(offset), SEEK_SET) != 0)
        return false;
    return std::fread(dst, 1, count, f) == count;
}

bool fileSizeOf(FILE *f, uint64_t &size)
{
    if (NPZ_FSEEK(f, 0, SEEK_END) != 0)
        return false;
    const long long end =
#if defined(_MSC_VER)
        _ftelli64(f);
#else
        ftello(f);
#endif
    if (end < 0)
        return false;
    size = static_cast<uint64_t>(end);
    return true;
}

// One readable region: a whole .npy file, or a member of a .npz archive.
struct Payload
{
    FILE *f = nullptr;
    uint64_t base = 0; // file offset of the first payload byte
    uint64_t compSize = 0;
    uint64_t uncompSize = 0;
    bool deflated = false;
};

// Inflate from the start of the member, discarding output until `offset`.
// Zip members hold a raw deflate stream, hence the negative window bits.
bool readDeflated(const Payload &p, uint64_t offset, uint64_t count, uint8_t *dst, std::string *error)
{
    z_stream zs{};
    if (inflateInit2(&zs, -MAX_WBITS) != Z_OK)
    {
        setError(error, "zlib: inflateInit2 failed");
        return false;
    }

    std::vector<uint8_t> inBuf(1 << 16);
    std::vector<uint8_t> outBuf(1 << 16);
    uint64_t consumedIn = 0;
    uint64_t produced = 0;
    uint64_t written = 0;
    int status = Z_OK;

    while (written < count)
    {
        if (zs.avail_in == 0)
        {
            const uint64_t remaining = p.compSize - consumedIn;
            const size_t want = static_cast<size_t>(std::min<uint64_t>(inBuf.size(), remaining));
            if (want == 0)
                break;
            if (!readAt(p.f, p.base + consumedIn, inBuf.data(), want))
            {
                inflateEnd(&zs);
                setError(error, "failed to read compressed data");
                return false;
            }
            consumedIn += want;
            zs.next_in = inBuf.data();
            zs.avail_in = static_cast<uInt>(want);
        }

        zs.next_out = outBuf.data();
        zs.avail_out = static_cast<uInt>(outBuf.size());
        status = inflate(&zs, Z_NO_FLUSH);
        if (status != Z_OK && status != Z_STREAM_END && status != Z_BUF_ERROR)
        {
            inflateEnd(&zs);
            setError(error, "zlib: inflate failed on compressed entry");
            return false;
        }

        const size_t got = outBuf.size() - zs.avail_out;
        if (got > 0)
        {
            const uint64_t chunkEnd = produced + got;
            if (chunkEnd > offset)
            {
                const uint64_t from = std::max<uint64_t>(offset, produced);
                const uint64_t to = std::min<uint64_t>(offset + count, chunkEnd);
                if (to > from)
                {
                    std::memcpy(dst + (from - offset), outBuf.data() + (from - produced),
                                static_cast<size_t>(to - from));
                    written += to - from;
                }
            }
            produced = chunkEnd;
        }

        if (status == Z_STREAM_END)
            break;
        if (got == 0 && zs.avail_in == 0 && consumedIn >= p.compSize)
            break;
    }

    inflateEnd(&zs);
    if (written < count)
    {
        setError(error, "compressed entry ended before the requested range");
        return false;
    }
    return true;
}

bool readPayload(const Payload &p, uint64_t offset, uint64_t count, uint8_t *dst, std::string *error)
{
    if (count == 0)
        return true;
    if (offset + count > p.uncompSize)
    {
        setError(error, "read past the end of the array data");
        return false;
    }
    if (!p.deflated)
    {
        if (!readAt(p.f, p.base + offset, dst, static_cast<size_t>(count)))
        {
            setError(error, "failed to read array data");
            return false;
        }
        return true;
    }
    return readDeflated(p, offset, count, dst, error);
}

struct ZipEntry
{
    std::string name;
    uint16_t method = 0;
    uint64_t compSize = 0;
    uint64_t uncompSize = 0;
    uint64_t localHeaderOffset = 0;
};

constexpr uint32_t kEocdSig = 0x06054b50;
constexpr uint32_t kZip64LocatorSig = 0x07064b50;
constexpr uint32_t kZip64EocdSig = 0x06064b50;
constexpr uint32_t kCentralSig = 0x02014b50;
constexpr uint32_t kLocalSig = 0x04034b50;

// Locate the central directory, following the ZIP64 records when present.
// ZIP64 matters here: a multi-class softmax volume easily exceeds 4 GB.
bool findCentralDirectory(FILE *f, uint64_t fileSize, uint64_t &cdOffset, uint64_t &numEntries, std::string *error)
{
    const uint64_t tailSize = std::min<uint64_t>(fileSize, 66u * 1024u);
    if (tailSize < 22)
    {
        setError(error, "file is too small to be a .npz archive");
        return false;
    }
    std::vector<uint8_t> tail(static_cast<size_t>(tailSize));
    const uint64_t tailStart = fileSize - tailSize;
    if (!readAt(f, tailStart, tail.data(), tail.size()))
    {
        setError(error, "failed to read the archive trailer");
        return false;
    }

    int64_t eocdRel = -1;
    for (int64_t i = static_cast<int64_t>(tail.size()) - 22; i >= 0; --i)
    {
        if (rd32(tail.data() + i) == kEocdSig)
        {
            eocdRel = i;
            break;
        }
    }
    if (eocdRel < 0)
    {
        setError(error, "not a zip archive (no end-of-central-directory record)");
        return false;
    }

    const uint8_t *eocd = tail.data() + eocdRel;
    numEntries = rd16(eocd + 10);
    cdOffset = rd32(eocd + 16);

    // A ZIP64 locator sits immediately before the EOCD when 32-bit fields overflowed.
    if (eocdRel >= 20 && rd32(tail.data() + eocdRel - 20) == kZip64LocatorSig)
    {
        const uint64_t z64Offset = rd64(tail.data() + eocdRel - 20 + 8);
        uint8_t z64[56];
        if (readAt(f, z64Offset, z64, sizeof(z64)) && rd32(z64) == kZip64EocdSig)
        {
            numEntries = rd64(z64 + 32);
            cdOffset = rd64(z64 + 48);
        }
    }

    if (cdOffset >= fileSize)
    {
        setError(error, "central directory offset lies outside the file");
        return false;
    }
    return true;
}

bool readCentralDirectory(FILE *f, uint64_t fileSize, std::vector<ZipEntry> &entries, std::string *error)
{
    uint64_t cdOffset = 0;
    uint64_t numEntries = 0;
    if (!findCentralDirectory(f, fileSize, cdOffset, numEntries, error))
        return false;

    uint64_t cursor = cdOffset;
    for (uint64_t i = 0; i < numEntries; ++i)
    {
        uint8_t header[46];
        if (!readAt(f, cursor, header, sizeof(header)) || rd32(header) != kCentralSig)
        {
            setError(error, "malformed central directory entry");
            return false;
        }
        const uint16_t nameLen = rd16(header + 28);
        const uint16_t extraLen = rd16(header + 30);
        const uint16_t commentLen = rd16(header + 32);

        std::string name(nameLen, '\0');
        if (nameLen > 0 && !readAt(f, cursor + 46, name.data(), nameLen))
        {
            setError(error, "failed to read an entry name");
            return false;
        }

        ZipEntry entry;
        entry.name = name;
        entry.method = rd16(header + 10);
        entry.compSize = rd32(header + 20);
        entry.uncompSize = rd32(header + 24);
        entry.localHeaderOffset = rd32(header + 42);

        // ZIP64 extra field: only the fields stored as 0xFFFFFFFF are present, in this order.
        if (extraLen > 0)
        {
            std::vector<uint8_t> extra(extraLen);
            if (!readAt(f, cursor + 46 + nameLen, extra.data(), extra.size()))
            {
                setError(error, "failed to read an entry extra field");
                return false;
            }
            size_t p = 0;
            while (p + 4 <= extra.size())
            {
                const uint16_t id = rd16(extra.data() + p);
                const uint16_t len = rd16(extra.data() + p + 2);
                if (p + 4 + len > extra.size())
                    break;
                if (id == 0x0001)
                {
                    size_t q = p + 4;
                    if (entry.uncompSize == 0xFFFFFFFFu && q + 8 <= p + 4 + len)
                    {
                        entry.uncompSize = rd64(extra.data() + q);
                        q += 8;
                    }
                    if (entry.compSize == 0xFFFFFFFFu && q + 8 <= p + 4 + len)
                    {
                        entry.compSize = rd64(extra.data() + q);
                        q += 8;
                    }
                    if (entry.localHeaderOffset == 0xFFFFFFFFu && q + 8 <= p + 4 + len)
                        entry.localHeaderOffset = rd64(extra.data() + q);
                    break;
                }
                p += 4 + len;
            }
        }

        entries.push_back(std::move(entry));
        cursor += 46 + nameLen + extraLen + commentLen;
    }
    return true;
}

// The local header repeats the name/extra lengths, and its extra field may
// differ in length from the central one, so the data offset must be read here.
bool resolvePayload(FILE *f, const ZipEntry &entry, Payload &payload, std::string *error)
{
    uint8_t header[30];
    if (!readAt(f, entry.localHeaderOffset, header, sizeof(header)) || rd32(header) != kLocalSig)
    {
        setError(error, "malformed local header for '" + entry.name + "'");
        return false;
    }
    if (entry.method != 0 && entry.method != 8)
    {
        setError(error, "unsupported zip compression method for '" + entry.name + "'");
        return false;
    }
    const uint16_t nameLen = rd16(header + 26);
    const uint16_t extraLen = rd16(header + 28);
    payload.f = f;
    payload.base = entry.localHeaderOffset + 30 + nameLen + extraLen;
    payload.compSize = entry.compSize;
    payload.uncompSize = entry.uncompSize;
    payload.deflated = (entry.method == 8);
    return true;
}

DType parseDescr(const std::string &descr, bool &bigEndian, std::string *error)
{
    bigEndian = false;
    std::string body = descr;
    if (!body.empty() && (body[0] == '<' || body[0] == '>' || body[0] == '|' || body[0] == '='))
    {
        bigEndian = (body[0] == '>');
        body.erase(0, 1);
    }
    if (body == "b1") return DType::Bool;
    if (body == "i1") return DType::Int8;
    if (body == "u1") return DType::UInt8;
    if (body == "i2") return DType::Int16;
    if (body == "u2") return DType::UInt16;
    if (body == "i4") return DType::Int32;
    if (body == "u4") return DType::UInt32;
    if (body == "i8") return DType::Int64;
    if (body == "u8") return DType::UInt64;
    if (body == "f2") return DType::Float16;
    if (body == "f4") return DType::Float32;
    if (body == "f8") return DType::Float64;
    setError(error, "unsupported numpy dtype '" + descr + "' (structured, complex and string arrays are not images)");
    return DType::Unknown;
}

bool parseHeaderDict(const std::string &header, ArrayInfo &info, bool &bigEndian, std::string *error)
{
    const size_t descrKey = header.find("'descr'");
    if (descrKey == std::string::npos)
    {
        setError(error, "numpy header has no 'descr' field");
        return false;
    }
    const size_t descrOpen = header.find('\'', header.find(':', descrKey));
    const size_t descrClose = (descrOpen == std::string::npos) ? std::string::npos : header.find('\'', descrOpen + 1);
    if (descrClose == std::string::npos)
    {
        setError(error, "numpy header has a malformed 'descr' field");
        return false;
    }
    info.dtype = parseDescr(header.substr(descrOpen + 1, descrClose - descrOpen - 1), bigEndian, error);
    if (info.dtype == DType::Unknown)
        return false;

    const size_t orderKey = header.find("'fortran_order'");
    if (orderKey != std::string::npos)
    {
        const size_t comma = header.find(',', orderKey);
        const std::string value = header.substr(orderKey, (comma == std::string::npos) ? std::string::npos : comma - orderKey);
        info.fortranOrder = value.find("True") != std::string::npos;
    }

    const size_t shapeKey = header.find("'shape'");
    if (shapeKey == std::string::npos)
    {
        setError(error, "numpy header has no 'shape' field");
        return false;
    }
    const size_t open = header.find('(', shapeKey);
    const size_t close = (open == std::string::npos) ? std::string::npos : header.find(')', open);
    if (close == std::string::npos)
    {
        setError(error, "numpy header has a malformed 'shape' field");
        return false;
    }
    info.shape.clear();
    std::string dims = header.substr(open + 1, close - open - 1);
    std::istringstream ds(dims);
    std::string token;
    while (std::getline(ds, token, ','))
    {
        const size_t first = token.find_first_not_of(" \t");
        if (first == std::string::npos)
            continue;
        info.shape.push_back(static_cast<size_t>(std::strtoull(token.c_str() + first, nullptr, 10)));
    }
    return true;
}

bool parseNpyHeader(const Payload &p, ArrayInfo &info, bool &bigEndian, uint64_t &dataOffset, std::string *error)
{
    uint8_t magic[12];
    if (!readPayload(p, 0, 10, magic, error))
        return false;
    if (std::memcmp(magic, "\x93NUMPY", 6) != 0)
    {
        setError(error, "entry is not a .npy stream");
        return false;
    }

    uint64_t headerLen = 0;
    uint64_t headerStart = 0;
    const uint8_t major = magic[6];
    if (major == 1)
    {
        headerLen = rd16(magic + 8);
        headerStart = 10;
    }
    else if (major == 2 || major == 3)
    {
        if (!readPayload(p, 8, 4, magic + 8, error))
            return false;
        headerLen = rd32(magic + 8);
        headerStart = 12;
    }
    else
    {
        setError(error, "unsupported .npy format version");
        return false;
    }

    std::string header(static_cast<size_t>(headerLen), '\0');
    if (!readPayload(p, headerStart, headerLen, reinterpret_cast<uint8_t *>(header.data()), error))
        return false;
    dataOffset = headerStart + headerLen;
    return parseHeaderDict(header, info, bigEndian, error);
}

float halfToFloat(uint16_t h)
{
    const uint32_t sign = static_cast<uint32_t>(h & 0x8000u) << 16;
    const uint32_t exponent = (h >> 10) & 0x1Fu;
    uint32_t mantissa = h & 0x3FFu;
    uint32_t bits = 0;
    if (exponent == 0)
    {
        if (mantissa != 0)
        {
            // Subnormal half: renormalise into a float32 exponent.
            int shift = 0;
            while ((mantissa & 0x400u) == 0)
            {
                mantissa <<= 1;
                ++shift;
            }
            mantissa &= 0x3FFu;
            bits = sign | (static_cast<uint32_t>(127 - 15 - shift) << 23) | (mantissa << 13);
        }
        else
        {
            bits = sign;
        }
    }
    else if (exponent == 0x1Fu)
    {
        bits = sign | 0x7F800000u | (mantissa << 13);
    }
    else
    {
        bits = sign | ((exponent + 127 - 15) << 23) | (mantissa << 13);
    }
    float value = 0.0f;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

template <typename T>
void convertTyped(const uint8_t *src, size_t count, bool swap, float *dst)
{
    constexpr size_t S = sizeof(T);
    for (size_t i = 0; i < count; ++i)
    {
        T value;
        if (swap)
        {
            uint8_t tmp[S];
            for (size_t b = 0; b < S; ++b)
                tmp[b] = src[i * S + (S - 1 - b)];
            std::memcpy(&value, tmp, S);
        }
        else
        {
            std::memcpy(&value, src + i * S, S);
        }
        dst[i] = static_cast<float>(value);
    }
}

void convertRaw(const uint8_t *src, size_t count, DType dtype, bool bigEndian, float *dst)
{
    const bool swap = bigEndian && dtypeSize(dtype) > 1;
    switch (dtype)
    {
    case DType::Bool:
        for (size_t i = 0; i < count; ++i)
            dst[i] = src[i] != 0 ? 1.0f : 0.0f;
        break;
    case DType::Int8: convertTyped<int8_t>(src, count, false, dst); break;
    case DType::UInt8: convertTyped<uint8_t>(src, count, false, dst); break;
    case DType::Int16: convertTyped<int16_t>(src, count, swap, dst); break;
    case DType::UInt16: convertTyped<uint16_t>(src, count, swap, dst); break;
    case DType::Int32: convertTyped<int32_t>(src, count, swap, dst); break;
    case DType::UInt32: convertTyped<uint32_t>(src, count, swap, dst); break;
    case DType::Int64: convertTyped<int64_t>(src, count, swap, dst); break;
    case DType::UInt64: convertTyped<uint64_t>(src, count, swap, dst); break;
    case DType::Float32: convertTyped<float>(src, count, swap, dst); break;
    case DType::Float64: convertTyped<double>(src, count, swap, dst); break;
    case DType::Float16:
        for (size_t i = 0; i < count; ++i)
        {
            uint16_t raw;
            if (swap)
            {
                const uint8_t tmp[2] = {src[i * 2 + 1], src[i * 2]};
                std::memcpy(&raw, tmp, 2);
            }
            else
            {
                std::memcpy(&raw, src + i * 2, 2);
            }
            dst[i] = halfToFloat(raw);
        }
        break;
    default:
        break;
    }
}

std::string entryArrayName(const std::string &zipName)
{
    return hasSuffixCi(zipName, ".npy") ? zipName.substr(0, zipName.size() - 4) : zipName;
}

// Open one array for reading: resolves the container kind, the member, and the
// numpy header, leaving `payload`/`dataOffset` positioned at the raw elements.
bool openArray(const std::string &path, const std::string &arrayName, FileHandle &handle,
               Payload &payload, ArrayInfo &info, bool &bigEndian, uint64_t &dataOffset,
               std::string *error)
{
    handle.f = std::fopen(path.c_str(), "rb");
    if (!handle.f)
    {
        setError(error, "could not open '" + path + "'");
        return false;
    }
    uint64_t fileSize = 0;
    if (!fileSizeOf(handle.f, fileSize))
    {
        setError(error, "could not determine the size of '" + path + "'");
        return false;
    }

    if (isNpyPath(path))
    {
        payload.f = handle.f;
        payload.base = 0;
        payload.compSize = fileSize;
        payload.uncompSize = fileSize;
        payload.deflated = false;
        info.name.clear();
        return parseNpyHeader(payload, info, bigEndian, dataOffset, error);
    }

    std::vector<ZipEntry> entries;
    if (!readCentralDirectory(handle.f, fileSize, entries, error))
        return false;

    const ZipEntry *chosen = nullptr;
    for (const ZipEntry &e : entries)
    {
        if (arrayName.empty() || entryArrayName(e.name) == arrayName || e.name == arrayName)
        {
            chosen = &e;
            break;
        }
    }
    if (!chosen)
    {
        setError(error, "array '" + arrayName + "' is not in the archive");
        return false;
    }
    if (!resolvePayload(handle.f, *chosen, payload, error))
        return false;
    info.name = entryArrayName(chosen->name);
    return parseNpyHeader(payload, info, bigEndian, dataOffset, error);
}

} // namespace

bool isNpzPath(const std::string &path) { return hasSuffixCi(path, ".npz"); }
bool isNpyPath(const std::string &path) { return hasSuffixCi(path, ".npy"); }

bool inspect(const std::string &path, std::vector<ArrayInfo> &out, std::string *error)
{
    out.clear();
    FileHandle handle;
    handle.f = std::fopen(path.c_str(), "rb");
    if (!handle.f)
    {
        setError(error, "could not open '" + path + "'");
        return false;
    }
    uint64_t fileSize = 0;
    if (!fileSizeOf(handle.f, fileSize))
    {
        setError(error, "could not determine the size of '" + path + "'");
        return false;
    }

    if (isNpyPath(path))
    {
        Payload payload;
        payload.f = handle.f;
        payload.base = 0;
        payload.compSize = fileSize;
        payload.uncompSize = fileSize;
        ArrayInfo info;
        bool bigEndian = false;
        uint64_t dataOffset = 0;
        if (!parseNpyHeader(payload, info, bigEndian, dataOffset, error))
            return false;
        out.push_back(std::move(info));
        return true;
    }

    std::vector<ZipEntry> entries;
    if (!readCentralDirectory(handle.f, fileSize, entries, error))
        return false;

    for (const ZipEntry &entry : entries)
    {
        Payload payload;
        if (!resolvePayload(handle.f, entry, payload, nullptr))
            continue; // skip members we cannot decode instead of failing the whole archive
        ArrayInfo info;
        bool bigEndian = false;
        uint64_t dataOffset = 0;
        if (!parseNpyHeader(payload, info, bigEndian, dataOffset, nullptr))
            continue;
        info.name = entryArrayName(entry.name);
        out.push_back(std::move(info));
    }
    if (out.empty())
    {
        setError(error, "no readable numpy arrays found in '" + path + "'");
        return false;
    }
    return true;
}

bool readAsFloat(const std::string &path, const std::string &arrayName,
                 size_t offset, size_t count,
                 std::vector<float> &out, std::string *error)
{
    FileHandle handle;
    Payload payload;
    ArrayInfo info;
    bool bigEndian = false;
    uint64_t dataOffset = 0;
    if (!openArray(path, arrayName, handle, payload, info, bigEndian, dataOffset, error))
        return false;
    if (info.fortranOrder)
    {
        setError(error, "array '" + info.name + "' is Fortran-ordered; read it whole instead");
        return false;
    }

    const size_t total = info.elementCount();
    if (offset > total)
    {
        setError(error, "requested range starts past the end of the array");
        return false;
    }
    if (count == 0)
        count = total - offset;
    if (offset + count > total)
    {
        setError(error, "requested range extends past the end of the array");
        return false;
    }

    const size_t elemSize = dtypeSize(info.dtype);
    std::vector<uint8_t> raw(count * elemSize);
    if (!readPayload(payload, dataOffset + static_cast<uint64_t>(offset) * elemSize,
                     static_cast<uint64_t>(count) * elemSize, raw.data(), error))
        return false;

    out.resize(count);
    convertRaw(raw.data(), count, info.dtype, bigEndian, out.data());
    return true;
}

bool readAllAsFloat(const std::string &path, const std::string &arrayName,
                    ArrayInfo *info, std::vector<float> &out, std::string *error)
{
    FileHandle handle;
    Payload payload;
    ArrayInfo local;
    bool bigEndian = false;
    uint64_t dataOffset = 0;
    if (!openArray(path, arrayName, handle, payload, local, bigEndian, dataOffset, error))
        return false;

    const size_t total = local.elementCount();
    const size_t elemSize = dtypeSize(local.dtype);
    std::vector<uint8_t> raw(total * elemSize);
    if (!readPayload(payload, dataOffset, static_cast<uint64_t>(total) * elemSize, raw.data(), error))
        return false;

    std::vector<float> values(total);
    convertRaw(raw.data(), total, local.dtype, bigEndian, values.data());
    raw.clear();
    raw.shrink_to_fit();

    if (local.fortranOrder && local.shape.size() > 1)
    {
        // Walk C-order positions and gather from the column-major buffer.
        const size_t nd = local.shape.size();
        std::vector<size_t> stride(nd);
        size_t s = 1;
        for (size_t a = 0; a < nd; ++a)
        {
            stride[a] = s;
            s *= local.shape[a];
        }
        out.assign(total, 0.0f);
        std::vector<size_t> idx(nd, 0);
        for (size_t c = 0; c < total; ++c)
        {
            size_t f = 0;
            for (size_t a = 0; a < nd; ++a)
                f += idx[a] * stride[a];
            out[c] = values[f];
            for (size_t a = nd; a-- > 0;)
            {
                if (++idx[a] < local.shape[a])
                    break;
                idx[a] = 0;
            }
        }
    }
    else
    {
        out = std::move(values);
    }

    if (info)
        *info = local;
    return true;
}

} // namespace npz
