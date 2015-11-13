/*
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include <boost/range/adaptor/reversed.hpp>
#include <lua.hpp>
#include <matio.h>

#include <fblualib/LuaUtils.h>
#include <folly/Range.h>
#include <folly/ScopeGuard.h>
#include <thpp/Tensor.h>

using namespace thpp;
using namespace fblualib;

namespace {

// Push a tensor (of type T) on the Lua stack, obtained from the Matlab
// variable var
template <class T>
void pushTensor(lua_State* L, mat_t* fp, matvar_t* var) {
  auto range = folly::range(var->dims, var->dims + var->rank);
  auto revRange = boost::adaptors::reverse(range);
  Tensor<T> tensor(LongStorage(revRange.begin(), revRange.end()));

  int r = Mat_VarReadDataLinear(fp, var, static_cast<void*>(tensor.data()),
                                0, 1, tensor.size());
  if (r != 0) {
    luaL_error(L, "Variable %s: unable to read Matlab data", var->name);
  }

  // Matlab is column-major, Lua is row-major. Transpose, and make a copy
  // to give contiguous output to Lua.
  tensor.transpose();
  tensor.force(Tensor<T>::CONTIGUOUS);

  luaPushTensor(L, std::move(tensor));
}

// Load tensors from one Matlab file
int load(lua_State* L) {
  auto path = luaGetStringChecked(L, 1).str();

  auto fp = Mat_Open(path.c_str(), MAT_ACC_RDONLY);
  if (!fp) {
    return luaL_error(L, "Unable to open Matlab file %s", path.c_str());
  }
  SCOPE_FAIL {
    Mat_Close(fp);  // ignore error, we're throwing an exception
  };

  // Create table to hold loaded variables
  lua_newtable(L);
  int tableIdx = lua_gettop(L);

  // We need to read info for all variables first, as matio relies on
  // not changing the file pointer between calls to Mat_VarReadNextInfo, and
  // data-reading functions do that.

  {
    std::vector<matvar_t*> vars;
    SCOPE_EXIT {
      for (auto var : vars) {
        Mat_VarFree(var);
      }
    };

    // Read info for all variables
    matvar_t* var;
    while ((var = Mat_VarReadNextInfo(fp)) != nullptr) {
      vars.push_back(var);
    }

    for (auto var : vars) {
      lua_pushstring(L, var->name);

      // Dispatch by type
      switch (var->class_type) {
#define X(tname, type) \
      case MAT_C_##tname: pushTensor<type>(L, fp, var); break;
      X(DOUBLE, double)
      X(SINGLE, float)
      X(UINT8, uint8_t)
      X(INT16, int16_t)
      X(INT32, int32_t)
      X(INT64, int64_t)
#undef X
      default:
        return luaL_error(L, "Variable %s: unsupported Matlab class %d",
                          var->name,
                          int(var->class_type));
      }

      // Add to table
      lua_rawset(L, tableIdx);
    }
  }

  if (Mat_Close(fp) != 0) {
    luaL_error(L, "Unable to close Matlab file");
  }

  return 1;
}

// Save one tensor to Matlab file
template <class T>
void saveTensor(lua_State* L,
                const Tensor<T>& srcTensor, mat_t* fp,
                const char* name,
                matio_classes classType,
                matio_types dataType) {
  // Sanity check
  DCHECK_EQ(sizeof(T), Mat_SizeOf(dataType));
  DCHECK_EQ(sizeof(T), Mat_SizeOfClass(classType));

  // Matlab is column-major, Lua is row-major. Transpose (likely making a
  // copy).
  //
  // We need the tensor to be contiguous; matio doesn't support our
  // stride gymnastics.
  Tensor<T> tensor(srcTensor);
  tensor.transpose();
  tensor.force(Tensor<T>::CONTIGUOUS);

  auto sizes = tensor.sizes();
  auto revSizes = boost::adaptors::reverse(sizes);
  std::vector<size_t> sizesSizeT(revSizes.begin(), revSizes.end());

  auto var = Mat_VarCreate(
      name,
      classType,
      dataType,
      sizesSizeT.size(),
      sizesSizeT.data(),
      static_cast<void*>(tensor.data()),
      MAT_F_DONT_COPY_DATA);
  CHECK(var);

  SCOPE_EXIT {
    Mat_VarFree(var);
  };

  if (Mat_VarWrite(fp, var, MAT_COMPRESSION_NONE) != 0) {
    luaL_error(L, "Unable to write tensor");
  }
}

// Save tensor at index idx on the Lua stack to the Matlab file under
// the given name
void saveTensorAtIndex(lua_State* L, int idx, mat_t* fp, const char* name) {
  // Dispatch by type
#define X(tname, type) { \
    auto tensor = luaGetTensor<type>(L, idx); \
    if (tensor) { \
      saveTensor(L, **tensor, fp, name, MAT_C_##tname, MAT_T_##tname); \
      return; \
    } \
  }
  X(DOUBLE, double)
  X(SINGLE, float)
  X(UINT8, uint8_t)
  X(INT16, int16_t)
  X(INT32, int32_t)
  X(INT64, int64_t)
#undef X
  luaL_error(L, "Not a tensor");
}

// Create a Matlab file from the path stored at pathIdx on the Lua stack
// with the output version stored at verIdx on the Lua stack
mat_t* createMatlabFile(lua_State* L, int pathIdx, int verIdx) {
  auto path = luaGetStringChecked(L, pathIdx).str();

  mat_ft version = MAT_FT_DEFAULT;

  if (!lua_isnil(L, verIdx)) {
    auto verStr = luaGetStringChecked(L, verIdx);
    if (!verStr.empty()) {
      if (verStr == "7.3") {
        version = MAT_FT_MAT73;
      } else if (verStr == "5") {
        version = MAT_FT_MAT5;
      } else if (verStr == "4") {
        version = MAT_FT_MAT4;
      } else {
        luaL_error(L, "Unsupported Matlab version %s", verStr.str().c_str());
      }
    }
  }

  auto fp = Mat_CreateVer(path.c_str(), nullptr, version);
  if (!fp) {
    luaL_error(L, "Unable to create Matlab file %s", path.c_str());
  }

  return fp;
}

// Save tensor: save(path, tensor [, version])
int save(lua_State* L) {
  auto fp = createMatlabFile(L, 1, 3);
  SCOPE_FAIL {
    Mat_Close(fp);  // ignore error, we're throwing an exception
  };

  // save tensor in a dummy var named x
  saveTensorAtIndex(L, 2, fp, "x");

  if (Mat_Close(fp) != 0) {
    return luaL_error(L, "Unable to close Matlab file");
  }

  return 0;
}

// Save all tensors in a table: save(path, table [, tensor])
int saveTable(lua_State* L) {
  auto fp = createMatlabFile(L, 1, 3);
  SCOPE_FAIL {
    Mat_Close(fp);  // ignore error, we're throwing an exception
  };

  // Table at index 2
  lua_pushnil(L);  // first key
  while (lua_next(L, 2) != 0) {
    // key at index -2, value at index -1
    auto name = luaGetStringChecked(L, -2, /*strict*/ true).str();
    saveTensorAtIndex(L, -1, fp, name.c_str());
    lua_pop(L, 1);  // pop value (tensor)
  }

  if (Mat_Close(fp) != 0) {
    return luaL_error(L, "Unable to close Matlab file");
  }

  return 0;
}

const struct luaL_reg matlab[] = {
  {"load", load},
  {"saveTensor", save},
  {"saveTable", saveTable},
  {nullptr, nullptr},  // sentinel
};

}  // namespace

extern "C" int LUAOPEN(lua_State* L) {
  lua_newtable(L);
  luaL_register(L, nullptr, matlab);
  return 1;
}
