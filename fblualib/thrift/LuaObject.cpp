/**
 * Copyright 2014 Facebook
 * @author Tudor Bosman (tudorb@fb.com)
 */

#include <fblualib/thrift/LuaObject.h>

namespace fblualib { namespace thrift {

LuaObjectType getType(const LuaObject& obj) {
  auto& pobj = obj.value;
  if (pobj.isNil) return LuaObjectType::NIL;
  if (pobj.__isset.doubleVal) return LuaObjectType::DOUBLE;
  if (pobj.__isset.boolVal) return LuaObjectType::BOOL;
  if (pobj.__isset.stringVal) return LuaObjectType::STRING;

  if (UNLIKELY(!pobj.__isset.refVal)) {
    throw std::invalid_argument("Invalid LuaObject");
  }
  auto& ref = obj.refs.at(pobj.refVal);
  if (ref.__isset.stringVal) return LuaObjectType::STRING;
  if (ref.__isset.tableVal) return LuaObjectType::TABLE;
  if (ref.__isset.functionVal) return LuaObjectType::FUNCTION;
  if (ref.__isset.tensorVal) return LuaObjectType::TENSOR;
  if (ref.__isset.storageVal) return LuaObjectType::STORAGE;

  throw std::invalid_argument("Invalid LuaObject");
}

bool isNil(const LuaObject& obj) {
  return obj.value.isNil;
}

bool asBool(const LuaObject& obj) {
  // The only falsey things are nil and false
  auto& pobj = obj.value;
  return !(pobj.isNil || (pobj.__isset.boolVal && !pobj.boolVal));
}

double getDouble(const LuaObject& obj) {
  auto& pobj = obj.value;
  if (pobj.__isset.doubleVal) return pobj.doubleVal;
  throw std::invalid_argument("LuaObject of wrong type");
}

bool getBool(const LuaObject& obj) {
  auto& pobj = obj.value;
  if (pobj.__isset.boolVal) return pobj.boolVal;
  throw std::invalid_argument("LuaObject of wrong type");
}

namespace {
const LuaRefObject& getRef(const LuaObject& obj) {
  if (UNLIKELY(!obj.value.__isset.refVal)) {
    throw std::invalid_argument("LuaObject of wrong type");
  }
  return obj.refs.at(obj.value.refVal);
}
LuaRefObject& getRef(LuaObject& obj) {
  if (UNLIKELY(!obj.value.__isset.refVal)) {
    throw std::invalid_argument("LuaObject of wrong type");
  }
  return obj.refs.at(obj.value.refVal);
}
}  // namespace

folly::StringPiece getString(const LuaObject& obj) {
  auto& pobj = obj.value;
  if (pobj.__isset.stringVal) return pobj.stringVal;
  auto& ref = getRef(obj);
  if (ref.__isset.stringVal) return ref.stringVal;
  throw std::invalid_argument("LuaObject of wrong type");
}

thpp::ThriftTensorDataType getTensorType(const LuaObject& obj) {
  auto& ref = getRef(obj);
  if (ref.__isset.tensorVal) return ref.tensorVal.dataType;
  throw std::invalid_argument("LuaObject of wrong type");
}

template <class T>
thpp::Tensor<T> getTensor(LuaObject&& obj) {
  auto& ref = getRef(obj);
  if (!ref.__isset.tensorVal) {
    throw std::invalid_argument("LuaObject of wrong type");
  }
  auto tensor = thpp::Tensor<T>(std::move(ref.tensorVal));
  obj = LuaObject();
  return tensor;
}

#define X(T) template thpp::Tensor<T> getTensor(LuaObject&&);
X(unsigned char)
X(int32_t)
X(int64_t)
X(float)
X(double)
#undef X

LuaObject make() {
  LuaObject obj;
  obj.value.isNil = true;
  return obj;
}

LuaObject make(double val) {
  LuaObject obj;
  obj.value.__isset.doubleVal = true;
  obj.value.doubleVal = val;
  return obj;
}

LuaObject make(bool val) {
  LuaObject obj;
  obj.value.__isset.boolVal = true;
  obj.value.boolVal = val;
  return obj;
}

LuaObject make(folly::StringPiece val) {
  LuaObject obj;
  obj.value.__isset.stringVal = true;
  obj.value.stringVal.assign(val.data(), val.size());
  return obj;
}

template <class T>
LuaObject make(thpp::Tensor<T>& val) {
  LuaRefObject ref;
  ref.__isset.tensorVal = true;
  val.serialize(ref.tensorVal);

  LuaObject obj;
  obj.refs.push_back(std::move(ref));
  obj.value.__isset.refVal = true;
  obj.value.refVal = 0;
  return obj;
}

#define X(T) template LuaObject make(thpp::Tensor<T>&);
X(unsigned char)
X(int32_t)
X(int64_t)
X(float)
X(double)
#undef X

namespace detail {

LuaVersionInfo cppVersionInfo() {
  LuaVersionInfo info;
  // bytecodeVersion unset ==> no bytecode allowed
  info.interpreterVersion = "fblualib/thrift C++ library";
  return info;
}

}  // namespace detail

}}  // namespaces
