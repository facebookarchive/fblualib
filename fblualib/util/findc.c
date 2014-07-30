/*
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include <luaT.h>
#include <lauxlib.h>
#include <luaconf.h>
#include <lua.h>
#include <lualib.h>
#include <TH/TH.h>


static int findc(lua_State *L){
  const void* torch_ByteTensor_id = luaT_typenameid(L, "torch.ByteTensor");
  const void* torch_LongTensor_id = luaT_typenameid(L, "torch.LongTensor");
  THLongTensor *output_ptr = luaT_checkudata(L, 1, torch_LongTensor_id);
  THByteTensor *input_ptr = luaT_checkudata(L, 2, torch_ByteTensor_id);
  long *output = THLongTensor_data(output_ptr);
  unsigned char *input = THByteTensor_data(input_ptr);
  luaL_argcheck(L, output_ptr->nDimension == 1, 1,
      "first argument (output) should be a 1d LongTensor");
  luaL_argcheck(L, input_ptr->nDimension == 1, 2,
      "second argument (input) should be a 1d ByteTensor");
  long M  = input_ptr->size[0];
  long tM = output_ptr->size[0];
  long istride = input_ptr->stride[0];
  long ostride = output_ptr->stride[0];
  luaL_argcheck(L, tM == M, 1,"output vector should be same length as input");
  long ii;
  long count=0;
  for (ii=0;ii<M;++ii){
    if (input[istride*ii] != 0){
      output[ostride*count] = (long) ii+1;
      ++count;
    }
  }
  lua_pushnumber(L, count);
  return 1;
}


int luaopen_findc(lua_State *L){
  lua_register( L, "findc", findc);
  return 0;
}
