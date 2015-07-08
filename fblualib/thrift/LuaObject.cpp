/**
 * Copyright 2014 Facebook
 * @author Tudor Bosman (tudorb@fb.com)
 */

#include <fblualib/thrift/LuaObject.h>

namespace fblualib { namespace thrift {

LuaObjectType getType(const LuaPrimitiveObject& pobj,
                      const LuaRefList& refs) {
  if (pobj.isNil) return LuaObjectType::NIL;
  if (pobj.__isset.doubleVal) return LuaObjectType::DOUBLE;
  if (pobj.__isset.boolVal) return LuaObjectType::BOOL;
  if (pobj.__isset.stringVal) return LuaObjectType::STRING;

  if (UNLIKELY(!pobj.__isset.refVal)) {
    throw std::invalid_argument("Invalid LuaObject");
  }
  auto& ref = refs.at(pobj.refVal);
  if (ref.__isset.stringVal) return LuaObjectType::STRING;
  if (ref.__isset.tableVal) return LuaObjectType::TABLE;
  if (ref.__isset.functionVal) return LuaObjectType::FUNCTION;
  if (ref.__isset.tensorVal) return LuaObjectType::TENSOR;
  if (ref.__isset.storageVal) return LuaObjectType::STORAGE;
  if (ref.__isset.envLocation) return LuaObjectType::EXTERNAL;
  if (ref.__isset.customUserDataVal) return LuaObjectType::USERDATA;

  throw std::invalid_argument("Invalid LuaObject");
}

bool isNil(const LuaPrimitiveObject& pobj) {
  return pobj.isNil;
}

bool asBool(const LuaPrimitiveObject& pobj) {
  // The only falsey things are nil and false
  return !(pobj.isNil || (pobj.__isset.boolVal && !pobj.boolVal));
}

double getDouble(const LuaPrimitiveObject& pobj) {
  if (pobj.__isset.doubleVal) return pobj.doubleVal;
  throw std::invalid_argument("LuaObject of wrong type");
}

bool getBool(const LuaPrimitiveObject& pobj) {
  if (pobj.__isset.boolVal) return pobj.boolVal;
  throw std::invalid_argument("LuaObject of wrong type");
}

namespace {
const LuaRefObject& getRef(const LuaPrimitiveObject& pobj,
                           const LuaRefList& refs) {
  if (UNLIKELY(!pobj.__isset.refVal)) {
    throw std::invalid_argument("LuaObject of wrong type");
  }
  return refs.at(pobj.refVal);
}
}  // namespace

folly::StringPiece getString(const LuaPrimitiveObject& pobj,
                             const LuaRefList& refs) {
  if (pobj.__isset.stringVal) return pobj.stringVal;
  auto& ref = getRef(pobj, refs);
  if (ref.__isset.stringVal) return ref.stringVal;
  throw std::invalid_argument("LuaObject of wrong type");
}

thpp::ThriftTensorDataType getTensorType(const LuaPrimitiveObject& pobj,
                                         const LuaRefList& refs) {
  auto& ref = getRef(pobj, refs);
  if (ref.__isset.tensorVal) return ref.tensorVal.dataType;
  throw std::invalid_argument("LuaObject of wrong type");
}

template <class T>
thpp::Tensor<T> getTensor(const LuaPrimitiveObject& pobj,
                          const LuaRefList& refs,
                          thpp::SharingMode sharing) {
  auto& ref = getRef(pobj, refs);
  if (!ref.__isset.tensorVal) {
    throw std::invalid_argument("LuaObject of wrong type");
  }
  auto tensor = thpp::Tensor<T>(ref.tensorVal, sharing);
  return tensor;
}

#define X(T) \
  template thpp::Tensor<T> getTensor(const LuaPrimitiveObject&, \
                                     const LuaRefList&, thpp::SharingMode);
X(unsigned char)
X(int32_t)
X(int64_t)
X(float)
X(double)
#undef X

LuaPrimitiveObject makePrimitive() {
  LuaPrimitiveObject pobj;
  pobj.isNil = true;
  return pobj;
}

LuaPrimitiveObject makePrimitive(double val) {
  LuaPrimitiveObject pobj;
  pobj.__isset.doubleVal = true;
  pobj.doubleVal = val;
  return pobj;
}

LuaPrimitiveObject makePrimitive(bool val) {
  LuaPrimitiveObject pobj;
  pobj.__isset.boolVal = true;
  pobj.boolVal = val;
  return pobj;
}

LuaPrimitiveObject makePrimitive(folly::StringPiece val) {
  LuaPrimitiveObject pobj;
  pobj.__isset.stringVal = true;
  pobj.stringVal.assign(val.data(), val.size());
  return pobj;
}

namespace {
const LuaTable& getTable(const LuaPrimitiveObject& pobj,
                         const LuaRefList& refs) {
  auto& ref = getRef(pobj, refs);
  if (!ref.__isset.tableVal) {
    throw std::invalid_argument("LuaObject of wrong type");
  }
  return ref.tableVal;
}
}  // namespace

bool isList(const LuaPrimitiveObject& pobj, const LuaRefList& refs) {
  auto& table = getTable(pobj, refs);
  return !(table.__isset.stringKeys ||
           table.__isset.intKeys ||
           table.__isset.trueKey ||
           table.__isset.falseKey ||
           table.__isset.otherKeys);
}

size_t listSize(const LuaPrimitiveObject& pobj, const LuaRefList& refs) {
  return getTable(pobj, refs).listKeys.size();
}

namespace {

LuaPrimitiveObject appendRef(LuaRefObject&& ref, LuaRefList& refs) {
  LuaPrimitiveObject r;
  r.__isset.refVal = true;
  r.refVal = refs.size();
  refs.push_back(std::move(ref));
  return r;
}

}  // namespace

template <class T>
LuaPrimitiveObject append(const thpp::Tensor<T>& val, LuaRefList& refs,
                          thpp::SharingMode sharing) {
  LuaRefObject ref;
  ref.__isset.tensorVal = true;
  val.serialize(ref.tensorVal, thpp::ThriftTensorEndianness::NATIVE, sharing);

  return appendRef(std::move(ref), refs);
}

#define X(T) template LuaPrimitiveObject append(const thpp::Tensor<T>&, \
                                                LuaRefList&, thpp::SharingMode);
X(unsigned char)
X(int32_t)
X(int64_t)
X(float)
X(double)
#undef X

TableIterator tableBegin(const LuaPrimitiveObject& pobj,
                         const LuaRefList& refs) {
  return TableIterator(getTable(pobj, refs));
}

TableIterator::TableIterator(const LuaTable& table)
  : table_(&table),
    state_(LIST_KEYS),
    index_(0),
    dirty_(true) {
  skipEmpty();
}

TableIterator::TableIterator()
  : table_(nullptr),
    state_(END),
    index_(0),
    dirty_(true) { }

void TableIterator::increment() {
  value_ = std::pair<LuaPrimitiveObject, LuaPrimitiveObject>();
  dirty_ = true;
  switch (state_) {
  case LIST_KEYS:
  case OTHER_KEYS:
    ++index_;
    break;
  case STRING_KEYS:
    ++stringKeysIterator_;
    break;
  case INT_KEYS:
    ++intKeysIterator_;
    break;
  case TRUE_KEY:
    state_ = FALSE_KEY;
    break;
  case FALSE_KEY:
    state_ = OTHER_KEYS;
    index_ = 0;
    break;
  case END:
    break;
  }
  skipEmpty();
}

bool TableIterator::equal(const TableIterator& other) const {
  if (state_ != other.state_) {
    return false;
  }
  switch (state_) {
  case LIST_KEYS:
  case OTHER_KEYS:
    return index_ == other.index_;
  case STRING_KEYS:
    return stringKeysIterator_ == other.stringKeysIterator_;
  case INT_KEYS:
    return intKeysIterator_ == other.intKeysIterator_;
  case TRUE_KEY:
  case FALSE_KEY:
  case END:
    break;
  }
  return true;
}

const std::pair<LuaPrimitiveObject, LuaPrimitiveObject>&
  TableIterator::dereference() const {
  if (dirty_) {
    switch (state_) {
    case LIST_KEYS:
      value_.first = makePrimitive(double(index_ + 1));
      value_.second = table_->listKeys[index_];
      break;
    case STRING_KEYS:
      value_.first = makePrimitive(
          folly::StringPiece(stringKeysIterator_->first));
      value_.second = stringKeysIterator_->second;
      break;
    case INT_KEYS:
      value_.first = makePrimitive(double(intKeysIterator_->first));
      value_.second = intKeysIterator_->second;
      break;
    case TRUE_KEY:
      value_.first = makePrimitive(true);
      value_.second = table_->trueKey;
      break;
    case FALSE_KEY:
      value_.first = makePrimitive(false);
      value_.second = table_->falseKey;
      break;
    case OTHER_KEYS:
      value_.first = table_->otherKeys[index_].key;
      value_.second = table_->otherKeys[index_].value;
      break;
    case END:
      LOG(FATAL) << "Past the end";
      break;
    }
    dirty_ = false;
  }
  return value_;
}

void TableIterator::skipEmpty() {
  switch (state_) {
  case LIST_KEYS:
    if (index_ != table_->listKeys.size()) {
      break;
    }
    state_ = STRING_KEYS;
    stringKeysIterator_ = table_->stringKeys.cbegin();
    // fallthrough
  case STRING_KEYS:
    if (stringKeysIterator_ != table_->stringKeys.cend()) {
      break;
    }
    state_ = INT_KEYS;
    intKeysIterator_ = table_->intKeys.cbegin();
    // fallthrough
  case INT_KEYS:
    if (intKeysIterator_ != table_->intKeys.cend()) {
      break;
    }
    state_ = TRUE_KEY;
    // fallthrough
  case TRUE_KEY:
    if (table_->__isset.trueKey) {
      break;
    }
    state_ = FALSE_KEY;
    // fallthrough
  case FALSE_KEY:
    if (table_->__isset.falseKey) {
      break;
    }
    state_ = OTHER_KEYS;
    index_ = 0;
    // fallthrough
  case OTHER_KEYS:
    if (index_ != table_->otherKeys.size()) {
      break;
    }
    state_ = END;
    // fallthrough
  case END:
    break;
  }
}

ListIterator listBegin(const LuaPrimitiveObject& pobj,
                       const LuaRefList& refs) {
  return getTable(pobj, refs).listKeys.cbegin();
}

ListIterator listEnd(const LuaPrimitiveObject& pobj,
                     const LuaRefList& refs) {
  return getTable(pobj, refs).listKeys.cend();
}

namespace detail {

LuaVersionInfo cppVersionInfo() {
  LuaVersionInfo info;
  // bytecodeVersion unset ==> no bytecode allowed
  info.interpreterVersion = "fblualib/thrift C++ library";
  return info;
}

}  // namespace detail

}}  // namespaces
