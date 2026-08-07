#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Minimal reader for numpy .npy / .npz containers. Uses only zlib, no numpy runtime.
namespace npz
{

enum class DType
{
    Unknown,
    Bool,
    Int8,
    UInt8,
    Int16,
    UInt16,
    Int32,
    UInt32,
    Int64,
    UInt64,
    Float16,
    Float32,
    Float64
};

const char *dtypeName(DType t);
bool dtypeIsInteger(DType t);
size_t dtypeSize(DType t);

struct ArrayInfo
{
    std::string name;          // archive entry name without ".npy" (empty for a bare .npy)
    std::vector<size_t> shape; // numpy shape, slowest-varying axis first
    DType dtype = DType::Unknown;
    bool fortranOrder = false;

    size_t elementCount() const;
    std::string shapeString() const; // e.g. "(2, 256, 256, 128)"
};

// True when the path ends in .npz or .npy (case-insensitive).
bool isNpzPath(const std::string &path);
bool isNpyPath(const std::string &path);

// List the arrays held by `path` (.npz archive or single .npy file).
bool inspect(const std::string &path, std::vector<ArrayInfo> &out, std::string *error);

// Read `count` elements starting at C-order element `offset`, converted to float.
// `count == 0` reads to the end. Rejects Fortran-ordered arrays: their element
// order differs from the C-order indexing this range assumes.
bool readAsFloat(const std::string &path, const std::string &arrayName,
                 size_t offset, size_t count,
                 std::vector<float> &out, std::string *error);

// Read the whole array as float, transposing Fortran-ordered data into C order.
bool readAllAsFloat(const std::string &path, const std::string &arrayName,
                    ArrayInfo *info, std::vector<float> &out, std::string *error);

} // namespace npz
