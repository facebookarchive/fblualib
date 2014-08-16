/**
 * Copyright 2014 Facebook
 * @author Tudor Bosman (tudorb@fb.com)
 */

#include <lua.hpp>
#include <random>
#include <thread>
#include <vector>

#include <fblualib/LuaUtils.h>
#include <folly/MPMCPipeline.h>
#include <folly/Memory.h>
#include <folly/Random.h>
#include <thpp/Tensor.h>

using namespace thpp;
using namespace fblualib;

namespace {

// I'm too lazy to create per-type metatables :)
class AsyncRNGBase {
 public:
  virtual ~AsyncRNGBase() { }
  virtual void start() = 0;
  virtual int pushBatch(lua_State* L, size_t n) = 0;
};

template <class T>
class AsyncRNG : public AsyncRNGBase {
 public:
  typedef std::function<void(Tensor<T>&)> Generator;
  AsyncRNG(size_t numThreads, size_t chunkSize, Generator generator);
  ~AsyncRNG();

  void start() override;
  void getBatch(size_t n, std::vector<Tensor<T>>& out);
  int pushBatch(lua_State* L, size_t n) override;

 private:
  void terminate();
  void dequeueLoop();

  struct Response {
    Tensor<T> randomNumbers;
  };

  const size_t numThreads_;
  const size_t chunkSize_;
  bool started_ = false;
  Generator generator_;

  folly::MPMCPipeline<bool, Response> pipeline_;
  std::vector<std::thread> threads_;

  Response currentResponse_;
  size_t indexInCurrentChunk_ = std::numeric_limits<size_t>::max();
};

template <class T>
AsyncRNG<T>::AsyncRNG(size_t numThreads, size_t chunkSize,
                      Generator generator)
  : numThreads_(numThreads),
    chunkSize_(chunkSize),
    generator_(std::move(generator)),
    pipeline_(numThreads, numThreads) {
}

template <class T>
void AsyncRNG<T>::start() {
  CHECK(!started_);
  threads_.reserve(numThreads_);
  for (size_t i = 0; i < numThreads_; ++i) {
    threads_.emplace_back([this] { this->dequeueLoop(); });
  }

  for (size_t i = 0; i < numThreads_; ++i) {
    pipeline_.blockingWrite(true);
  }
  started_ = true;
}

template <class T>
AsyncRNG<T>::~AsyncRNG() {
  terminate();
}

template <class T>
void AsyncRNG<T>::terminate() {
  if (!started_) {
    return;
  }

  // We always keep the pipeline full.
  for (auto& t : threads_) {
    Response resp;
    pipeline_.blockingRead(resp);
  }

  for (auto& t : threads_) {
    pipeline_.blockingWrite(false);
  }

  for (auto& t : threads_) {
    Response resp;
    pipeline_.blockingRead(resp);
    DCHECK_EQ(resp.randomNumbers.size(), 0);
  }

  for (auto& t : threads_) {
    t.join();
  }
}

template <class T>
void AsyncRNG<T>::dequeueLoop() {
  bool req = true;
  while (req) {
    auto ticket = pipeline_.template blockingReadStage<0>(req);
    Response resp;

    if (req) {
      resp.randomNumbers = Tensor<T>({long(chunkSize_)});
      generator_(resp.randomNumbers);
    }

    pipeline_.template blockingWriteStage<0>(ticket, std::move(resp));
  }
}

template <class T>
void AsyncRNG<T>::getBatch(size_t size, std::vector<Tensor<T>>& out) {
  CHECK(started_);
  while (size != 0) {
    if (indexInCurrentChunk_ < chunkSize_) {
      size_t n = std::min(size, chunkSize_ - indexInCurrentChunk_);
      out.emplace_back();
      out.back().narrow(currentResponse_.randomNumbers, 0,
                        indexInCurrentChunk_, n);
      indexInCurrentChunk_ += n;
      size -= n;
    }

    if (indexInCurrentChunk_ >= chunkSize_) {
      pipeline_.blockingRead(currentResponse_);
      indexInCurrentChunk_ = 0;
      pipeline_.blockingWrite(true);
    }
  }
}

template <class T>
int AsyncRNG<T>::pushBatch(lua_State* L, size_t size) {
  std::vector<Tensor<T>> batch;
  getBatch(size, batch);

  lua_createtable(L, batch.size(), 0);
  for (size_t i = 0; i < batch.size(); ++i) {
    luaPushTensor(L, batch[i]);
    lua_rawseti(L, -2, i + 1);
  }

  return 1;
}

#define S(x) S1(x)
#define S1(x) #x

const char* const kAsyncRNGMTRegistryKey = S(LUAOPEN) ":AsyncRNGMT";

#undef S1
#undef S

// Fill a tensor given a distribution
template <class T, class Dist>
void generate(Tensor<T>& out, Dist& dist) {
  DCHECK(out.isContiguous());
  auto n = out.size();
  auto ptr = out.data();

  folly::ThreadLocalPRNG rng;
  for (size_t i = 0; i < n; ++i) {
    // Note the cast to T, so we can plug in distributions that return
    // a different type (such as bernoulli which returns bool)
    ptr[i] = T(dist(rng));
  }
}

template <class T, class Dist>
typename AsyncRNG<T>::Generator makeDist(Dist&& dist) {
  return [dist] (Tensor<T>& out) mutable { return generate(out, dist); };
}

// Shortcut to declare generator factories with a similar prototype
#define D(NAME) \
  template <class T> \
  typename AsyncRNG<T>::Generator make_##NAME(lua_State* L, int &arg)

D(uniform) {
  auto a = luaGetNumber<T>(L, arg++).value_or(0);
  auto b = luaGetNumber<T>(L, arg++).value_or(1);
  return makeDist<T>(std::uniform_real_distribution<T>(a, b));
}

D(uniform_int) {
  // Note, range is required, as we can't represent the whole range of
  // int64_t in a Lua number
  auto a = luaGetNumberChecked<int64_t>(L, arg++);
  auto b = luaGetNumberChecked<int64_t>(L, arg++);
  return makeDist<T>(std::uniform_int_distribution<int64_t>(a, b));
}

D(bernoulli) {
  auto p = luaGetNumber<T>(L, arg++).value_or(0.5);
  return makeDist<T>(std::bernoulli_distribution(p));
}

D(binomial) {
  auto t = luaGetNumber<int64_t>(L, arg++).value_or(1);
  auto p = luaGetNumber<T>(L, arg++).value_or(0.5);
  return makeDist<T>(std::binomial_distribution<int64_t>(t, p));
}

D(negative_binomial) {
  auto k = luaGetNumber<int64_t>(L, arg++).value_or(1);
  auto p = luaGetNumber<T>(L, arg++).value_or(0.5);
  return makeDist<T>(std::negative_binomial_distribution<int64_t>(k, p));
}

D(geometric) {
  auto p = luaGetNumber<T>(L, arg++).value_or(0.5);
  return makeDist<T>(std::geometric_distribution<int64_t>(p));
}

D(normal) {
  auto m = luaGetNumber<T>(L, arg++).value_or(0);
  auto s = luaGetNumber<T>(L, arg++).value_or(1);
  return makeDist<T>(std::normal_distribution<T>(m, s));
}

D(lognormal) {
  auto m = luaGetNumber<T>(L, arg++).value_or(0);
  auto s = luaGetNumber<T>(L, arg++).value_or(1);
  return makeDist<T>(std::lognormal_distribution<T>(m, s));
}

D(chi_squared) {
  auto n = luaGetNumber<T>(L, arg++).value_or(1);
  return makeDist<T>(std::chi_squared_distribution<T>(n));
}

D(cauchy) {
  auto a = luaGetNumber<T>(L, arg++).value_or(0);
  auto b = luaGetNumber<T>(L, arg++).value_or(1);
  return makeDist<T>(std::cauchy_distribution<T>(a, b));
}

D(fisher_f) {
  auto m = luaGetNumber<T>(L, arg++).value_or(1);
  auto n = luaGetNumber<T>(L, arg++).value_or(1);
  return makeDist<T>(std::fisher_f_distribution<T>(m, n));
}

D(student_t) {
  auto n = luaGetNumber<T>(L, arg++).value_or(1);
  return makeDist<T>(std::student_t_distribution<T>(n));
}

D(poisson) {
  auto m = luaGetNumber<double>(L, arg++).value_or(1);
  return makeDist<T>(std::poisson_distribution<int64_t>(m));
}

D(exponential) {
  auto l = luaGetNumber<T>(L, arg++).value_or(1);
  return makeDist<T>(std::exponential_distribution<T>(l));
}

D(gamma) {
  auto a = luaGetNumber<T>(L, arg++).value_or(1);
  auto b = luaGetNumber<T>(L, arg++).value_or(1);
  return makeDist<T>(std::gamma_distribution<T>(a, b));
}

D(weibull) {
  auto a = luaGetNumber<T>(L, arg++).value_or(1);
  auto b = luaGetNumber<T>(L, arg++).value_or(1);
  return makeDist<T>(std::weibull_distribution<T>(a, b));
}

D(extreme_value) {
  auto a = luaGetNumber<T>(L, arg++).value_or(0);
  auto b = luaGetNumber<T>(L, arg++).value_or(1);
  return makeDist<T>(std::extreme_value_distribution<T>(a, b));
}

template <class T>
void getList(lua_State* L, int index, std::vector<T>& out) {
  switch (lua_type(L, index)) {
  case LUA_TTABLE:
    break;
  case LUA_TNIL:
  case LUA_TNONE:
    return;
  default:
    luaL_argerror(L, index, "Table expected");
    return;
  }

  auto n = lua_objlen(L, index);
  out.clear();
  out.reserve(n);
  for (size_t i = 1; i <= n; ++i) {
    lua_rawgeti(L, index, i);
    out.push_back(luaGetNumberChecked<T>(L, -1));
    lua_pop(L, 1);
  }
}

D(discrete) {
  std::vector<double> weights;
  getList(L, arg++, weights);
  return makeDist<T>(std::discrete_distribution<int64_t>(
      weights.begin(), weights.end()));
}

D(piecewise_constant) {
  std::vector<T> boundaries;
  getList(L, arg++, boundaries);

  std::vector<double> weights;
  getList(L, arg++, weights);

  if (boundaries.empty()) {
    luaL_argcheck(L, weights.empty(), arg - 1,
                  "Boundaries and weights must be specified together");
    boundaries.push_back(0);
    boundaries.push_back(1);
    weights.push_back(1);
  } else {
    luaL_argcheck(L, boundaries.size() == weights.size() + 1, arg - 1,
                  "n weights required for n+1 boundaries");
  }

  return makeDist<T>(std::piecewise_constant_distribution<T>(
      boundaries.begin(),
      boundaries.end(),
      weights.begin()));
}

D(piecewise_linear) {
  std::vector<T> boundaries;
  getList(L, arg++, boundaries);

  std::vector<double> weights;
  getList(L, arg++, weights);

  if (boundaries.empty()) {
    luaL_argcheck(L, weights.empty(), arg - 1,
                  "Boundaries and weights must be specified together");
    boundaries.push_back(0);
    boundaries.push_back(1);
    weights.push_back(1);
    weights.push_back(1);
  } else {
    luaL_argcheck(L, boundaries.size() == weights.size(), arg - 1,
                  "n weights required for n boundaries");
  }

  return makeDist<T>(std::piecewise_linear_distribution<T>(
      boundaries.begin(),
      boundaries.end(),
      weights.begin()));
}

#undef D

template <class T>
using GeneratorFactory = typename AsyncRNG<T>::Generator (*)(lua_State*, int&);

// Arguments:
//   - type_name:     tensor type ("torch.FloatTensor")
//   - chunk_size:    chunk size, numbers to generate at once
//                    (default 1Mi, 1024 * 1024)
//   - params:        distribution parameters (defaults are
//                    distribution-specific; 0, 1 for default uniform
//                    distribution)
//
// The second argument may also be a config table:
// {
//   chunk_size = <chunk_size>,
//   num_threads = <num_threads>,  -- default 1
// }
//
// num_threads is the number of helper threads, each generating a chunk of
// chunk_size random numbers. The only reason to use more than one helper
// thread (rather than increasing chunk_size) is if you consume random
// numbers faster than one single thread can produce them. This is unlikely.
//
// Unless you're consuming random numbers faster than the selected number of
// threads can produce, reads of up to chunk_size * num_threads random numbers
// should return immediately.
int newRNG(lua_State* L,
           const char* distributionName,
           GeneratorFactory<float> floatGen,
           GeneratorFactory<double> doubleGen) {
  auto typeName = luaGetStringChecked(L, 1);

  size_t chunkSize = 1U << 20;  // 1 Mi
  size_t numThreads = 1;

  auto type = lua_type(L, 2);
  switch (type) {
  case LUA_TNUMBER:
    chunkSize = luaGetNumberChecked<size_t>(L, 2);
    break;
  case LUA_TTABLE:
    chunkSize =
      luaGetFieldIfNumber<size_t>(L, 2, "chunk_size").value_or(chunkSize);
    numThreads =
      luaGetFieldIfNumber<size_t>(L, 2, "num_threads").value_or(numThreads);
    break;
  case LUA_TNIL:
  case LUA_TNONE:
    break;
  default:
    return luaL_argerror(L, 2, "Invalid type, expected number or table");
  }
  luaL_argcheck(L, chunkSize > 0, 2, "Chunk size must be positive");
  luaL_argcheck(L, numThreads > 0, 2, "Number of threads must be positive");

  std::unique_ptr<AsyncRNGBase> rng;
  int arg = 3;
#define X(T) \
  if (!rng && typeName == Tensor<T>::kLuaTypeName) { \
    rng.reset(new AsyncRNG<T>(numThreads, chunkSize, T##Gen(L, arg))); \
  }
  X(float)
  X(double)
#undef X
  luaL_argcheck(L, rng != nullptr, 1, "Invalid tensor type");

  rng->start();

  auto ud = static_cast<AsyncRNGBase**>(
      lua_newuserdata(L, sizeof(AsyncRNGBase*)));
  lua_getfield(L, LUA_REGISTRYINDEX, kAsyncRNGMTRegistryKey);
  lua_setmetatable(L, -2);
  *ud = rng.release();

  return 1;
}

#define D(NAME) \
  int newRNG_##NAME(lua_State* L) { \
    return newRNG(L, #NAME, make_##NAME<float>, make_##NAME<double>); \
  }

#define EMIT_DISTRIBUTIONS \
  D(uniform) \
  D(uniform_int) \
  D(bernoulli) \
  D(binomial) \
  D(negative_binomial) \
  D(geometric) \
  D(normal) \
  D(lognormal) \
  D(chi_squared) \
  D(cauchy) \
  D(fisher_f) \
  D(student_t) \
  D(poisson) \
  D(exponential) \
  D(gamma) \
  D(weibull) \
  D(extreme_value) \
  D(discrete) \
  D(piecewise_constant) \
  D(piecewise_linear)

// Emit all newRNG_foo declarations
EMIT_DISTRIBUTIONS

#undef D

#define D(NAME) { #NAME, newRNG_##NAME},

struct luaL_reg kModuleFuncs[] = {
  EMIT_DISTRIBUTIONS
  {nullptr, nullptr},
};

#undef D
#undef EMIT_DISTRIBUTIONS

int deleteRNG(lua_State* L) {
  auto ud = static_cast<AsyncRNGBase**>(lua_touserdata(L, 1));
  delete *ud;
  return 0;
}

struct luaL_reg kAsyncRNGMTFuncs[] = {
  {"__gc", deleteRNG},
  {nullptr, nullptr},
};

int getBatch(lua_State* L) {
  auto ud = static_cast<AsyncRNGBase**>(lua_touserdata(L, 1));
  auto batchSize = luaGetNumberChecked<size_t>(L, 2);
  return (*ud)->pushBatch(L, batchSize);
}

struct luaL_reg kAsyncRNGFuncs[] = {
  {"generate", getBatch},
  {nullptr, nullptr},
};

}  // namespace

extern "C" int LUAOPEN(lua_State* L) {
  // Create metatable for AsyncRNG userdata
  lua_newtable(L);
  luaL_register(L, nullptr, kAsyncRNGMTFuncs);

  // Create __index entry in metatable (list of methods)
  lua_newtable(L);
  luaL_register(L, nullptr, kAsyncRNGFuncs);
  lua_setfield(L, -2, "__index");

  // Store the MT in the registry
  lua_setfield(L, LUA_REGISTRYINDEX, kAsyncRNGMTRegistryKey);

  // Create table of module functions
  lua_newtable(L);
  luaL_register(L, nullptr, kModuleFuncs);

  return 1;
}
