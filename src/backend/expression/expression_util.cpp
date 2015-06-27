/*-------------------------------------------------------------------------
 *
 * expression_util.cpp
 * file description
 *
 * Copyright(c) 2015, CMU
 *
 * /n-store/src/expression/expression_util.cpp
 *
 *-------------------------------------------------------------------------
 */

#include "backend/expression/expression_util.h"

#include "backend/common/value_factory.h"
#include "backend/common/exception.h"
#include "backend/common/logger.h"
#include "backend/expression/abstract_expression.h"
#include <cassert>
#include <sstream>
#include <cstdlib>
#include <stdexcept>

#include "backend/expression/expression.h"
#include <json_spirit.h>

namespace peloton {
namespace expression {

// Function static helper templated functions to vivify an optimal
//    comparison class.
AbstractExpression* GetGeneral(ExpressionType c, AbstractExpression *l, AbstractExpression *r) {
  assert (l);
  assert (r);
  switch (c) {
    case (EXPRESSION_TYPE_COMPARE_EQ):
        return new ComparisonExpression<CmpEq>(c, l, r);
    case (EXPRESSION_TYPE_COMPARE_NE):
        return new ComparisonExpression<CmpNe>(c, l, r);
    case (EXPRESSION_TYPE_COMPARE_LT):
        return new ComparisonExpression<CmpLt>(c, l, r);
    case (EXPRESSION_TYPE_COMPARE_GT):
        return new ComparisonExpression<CmpGt>(c, l, r);
    case (EXPRESSION_TYPE_COMPARE_LTE):
        return new ComparisonExpression<CmpLte>(c, l, r);
    case (EXPRESSION_TYPE_COMPARE_GTE):
        return new ComparisonExpression<CmpGte>(c, l, r);
    default:
      char message[256];
      sprintf(message, "Invalid ExpressionType '%s' called"
              " for ComparisonExpression", GetTypeName(c).c_str());
      throw ExpressionException(message);
  }
}


template <typename L, typename R>
AbstractExpression* GetMoreSpecialized(ExpressionType c, L* l, R* r)
{
  assert (l);
  assert (r);
  switch (c) {
    case (EXPRESSION_TYPE_COMPARE_EQ):
        return new InlinedComparisonExpression<CmpEq, L, R>(c, l, r);
    case (EXPRESSION_TYPE_COMPARE_NE):
        return new InlinedComparisonExpression<CmpNe, L, R>(c, l, r);
    case (EXPRESSION_TYPE_COMPARE_LT):
        return new InlinedComparisonExpression<CmpLt, L, R>(c, l, r);
    case (EXPRESSION_TYPE_COMPARE_GT):
        return new InlinedComparisonExpression<CmpGt, L, R>(c, l, r);
    case (EXPRESSION_TYPE_COMPARE_LTE):
        return new InlinedComparisonExpression<CmpLte, L, R>(c, l, r);
    case (EXPRESSION_TYPE_COMPARE_GTE):
        return new InlinedComparisonExpression<CmpGte, L, R>(c, l, r);
    default:
      char message[256];
      sprintf(message, "Invalid ExpressionType '%s' called for"
              " ComparisonExpression", GetTypeName(c).c_str());
      throw ExpressionException(message);
  }
}

// convert the enumerated value type into a concrete c type for the
// comparison helper templates.
AbstractExpression *ComparisonFactory(ExpressionType c, AbstractExpression *lc, AbstractExpression *rc) {
  assert(lc);

  // more specialization available?
  ConstantValueExpression *l_const =
      dynamic_cast<ConstantValueExpression*>(lc);

  ConstantValueExpression *r_const =
      dynamic_cast<ConstantValueExpression*>(rc);

  TupleValueExpression *l_tuple =
      dynamic_cast<TupleValueExpression*>(lc);

  TupleValueExpression *r_tuple =
      dynamic_cast<TupleValueExpression*>(rc);

  // this will inline getValue(), hooray!
  if (l_const != nullptr && r_const != nullptr) { // CONST-CONST can it happen?
    return GetMoreSpecialized<ConstantValueExpression,
        ConstantValueExpression>(c, l_const, r_const);
  } else if (l_const != nullptr && r_tuple != nullptr) { // CONST-TUPLE
    return GetMoreSpecialized<ConstantValueExpression,
        TupleValueExpression>(c, l_const, r_tuple);
  } else if (l_tuple != nullptr && r_const != nullptr) { // TUPLE-CONST
    return GetMoreSpecialized<TupleValueExpression,
        ConstantValueExpression >(c, l_tuple, r_const);
  } else if (l_tuple != nullptr && r_tuple != nullptr) { // TUPLE-TUPLE
    return GetMoreSpecialized<TupleValueExpression,
        TupleValueExpression>(c, l_tuple, r_tuple);
  }

  //okay, still getTypedValue is beneficial.
  return GetGeneral(c, lc, rc);
}

// convert the enumerated value type into a concrete c type for the
//  operator expression templated ctors
AbstractExpression *OperatorFactory(ExpressionType et, AbstractExpression *lc, AbstractExpression *rc) {
  AbstractExpression *ret = nullptr;

  switch(et) {
    case (EXPRESSION_TYPE_OPERATOR_PLUS):
        ret = new OperatorExpression<OpPlus>(et, lc, rc);
    break;

    case (EXPRESSION_TYPE_OPERATOR_MINUS):
        ret = new OperatorExpression<OpMinus>(et, lc, rc);
    break;

    case (EXPRESSION_TYPE_OPERATOR_MULTIPLY):
        ret = new OperatorExpression<OpMultiply>(et, lc, rc);
    break;

    case (EXPRESSION_TYPE_OPERATOR_DIVIDE):
        ret = new OperatorExpression<OpDivide>(et, lc, rc);
    break;

    case (EXPRESSION_TYPE_OPERATOR_NOT):
        ret = new OperatorUnaryNotExpression(lc);
    break;

    case (EXPRESSION_TYPE_OPERATOR_UNARY_MINUS):
        ret = new OperatorUnaryMinusExpression(lc);
    break;

    case (EXPRESSION_TYPE_OPERATOR_MOD):
        throw ExpressionException("Mod operator is not yet supported.");

    case (EXPRESSION_TYPE_OPERATOR_CONCAT):
        throw ExpressionException("Concat operator not yet supported.");

    case (EXPRESSION_TYPE_OPERATOR_CAST):
        throw ExpressionException("Cast operator not yet supported.");

    default:
      throw ExpressionException("operator ctor helper out of sync");
  }
  return ret;
}

// convert the enumerated value type into a concrete c type for
// constant value expressions templated ctors
AbstractExpression *ConstantValueFactory(json_spirit::Object &obj,
                     __attribute__((unused)) ValueType vt,
                     __attribute__((unused)) ExpressionType et,
                     __attribute__((unused)) AbstractExpression *lc,
                     __attribute__((unused)) AbstractExpression *rc) {
  // read before ctor - can then instantiate fully init'd obj.
  Value newvalue;
  json_spirit::Value valueValue = json_spirit::find_value( obj, "VALUE");
  if (valueValue == json_spirit::Value::null) {
    throw ExpressionException("constantValueFactory: Could not find"
        " VALUE value");
  }

  if (valueValue.type() == json_spirit::str_type)
  {
    std::string nullcheck = valueValue.get_str();
    if (nullcheck == "nullptr")
    {
      newvalue = Value::GetNullValue(vt);
      return ConstantValueFactory(newvalue);
    }
  }

  switch (vt) {
    case VALUE_TYPE_INVALID:
      throw ExpressionException("constantValueFactory: Value type should"
          " never be VALUE_TYPE_INVALID");
    case VALUE_TYPE_NULL:
      throw ExpressionException("constantValueFactory: And they should be"
          " never be this either! VALUE_TYPE_nullptr");
    case VALUE_TYPE_TINYINT:
      newvalue = ValueFactory::GetTinyIntValue(static_cast<int8_t>(valueValue.get_int64()));
      break;
    case VALUE_TYPE_SMALLINT:
      newvalue = ValueFactory::GetSmallIntValue(static_cast<int16_t>(valueValue.get_int64()));
      break;
    case VALUE_TYPE_INTEGER:
      newvalue = ValueFactory::GetIntegerValue(static_cast<int32_t>(valueValue.get_int64()));
      break;
    case VALUE_TYPE_BIGINT:
      newvalue = ValueFactory::GetBigIntValue(static_cast<int64_t>(valueValue.get_int64()));
      break;
    case VALUE_TYPE_DOUBLE:
      newvalue = ValueFactory::GetDoubleValue(static_cast<double>(valueValue.get_real()));
      break;
    case VALUE_TYPE_VARCHAR:
      newvalue = ValueFactory::GetStringValue(valueValue.get_str());
      break;
    case VALUE_TYPE_VARBINARY:
      // uses hex encoding
      newvalue = ValueFactory::GetBinaryValue(valueValue.get_str());
      break;
    case VALUE_TYPE_TIMESTAMP:
      newvalue = ValueFactory::GetTimestampValue(static_cast<int64_t>(valueValue.get_int64()));
      break;
    case VALUE_TYPE_DECIMAL:
      newvalue = ValueFactory::GetDecimalValueFromString(valueValue.get_str());
      break;
    default:
      throw ExpressionException("constantValueFactory: Unrecognized value"
          " type");
  }

  return ConstantValueFactory(newvalue);
}

// provide an interface for creating constant value expressions that
// is more useful to testcases */
AbstractExpression *ConstantValueFactory(const Value &newvalue){
  return new ConstantValueExpression(newvalue);
}

// convert the enumerated value type into a concrete c type for
// parameter value expression templated ctors */
AbstractExpression* ParameterValueFactory(json_spirit::Object &obj,
                      __attribute__((unused)) ExpressionType et,
                      __attribute__((unused)) AbstractExpression *lc,
                      __attribute__((unused)) AbstractExpression *rc) {
  // read before ctor - can then instantiate fully init'd obj.
  json_spirit::Value paramIdxValue = json_spirit::find_value( obj, "PARAM_IDX");
  if (paramIdxValue == json_spirit::Value::null) {
    throw ExpressionException("parameterValueFactory: Could not find"
        " PARAM_IDX value");
  }

  int param_idx = paramIdxValue.get_int();
  assert (param_idx >= 0);
  return ParameterValueFactory(param_idx);
}

AbstractExpression * ParameterValueFactory(int idx) {
  return new ParameterValueExpression(idx);
}

AbstractExpression *TupleValueFactory(int tuple_idx, int value_idx) {
  return new TupleValueExpression(tuple_idx, value_idx, "DUMMY", "DUMMY");
}

// convert the enumerated value type into a concrete c type for
// tuple value expression templated ctors
AbstractExpression* TupleValueFactory(json_spirit::Object &obj,
                                      __attribute__((unused)) ExpressionType et,
                                      __attribute__((unused)) AbstractExpression *lc,
                                      __attribute__((unused)) AbstractExpression *rc) {
  // read the tuple value expression specific data
  json_spirit::Value valueIdxValue =
      json_spirit::find_value( obj, "COLUMN_IDX");

  json_spirit::Value tableName =
      json_spirit::find_value(obj, "TABLE_NAME");

  json_spirit::Value columnName =
      json_spirit::find_value(obj, "COLUMN_NAME");

  // verify input
  if (valueIdxValue == json_spirit::Value::null) {
    throw ExpressionException("tupleValueFactory: Could not find"
        " COLUMN_IDX value");
  }
  if (valueIdxValue.get_int() < 0) {
    throw ExpressionException("tupleValueFactory: invalid column_idx.");
  }

  if (tableName == json_spirit::Value::null) {
    throw ExpressionException("tupleValueFactory: no table name in TVE");
  }

  if (columnName == json_spirit::Value::null) {
    throw ExpressionException("tupleValueFactory: no column name in"
        " TVE");
  }

  // FIXME: Shouldn't this vary between 0 and 1 depending on left or right tuple ?
  int tuple_idx = 0;

  return new TupleValueExpression(tuple_idx, valueIdxValue.get_int(),
                                  tableName.get_str(), columnName.get_str());
}

AbstractExpression *ConjunctionFactory(ExpressionType et, AbstractExpression *lc, AbstractExpression *rc) {
  switch (et) {
    case (EXPRESSION_TYPE_CONJUNCTION_AND):
        return new ConjunctionExpression<ConjunctionAnd>(et, lc, rc);
    case (EXPRESSION_TYPE_CONJUNCTION_OR):
        return new ConjunctionExpression<ConjunctionOr>(et, lc, rc);
    default:
      return nullptr;
  }
}


// Given an expression type and a valuetype, find the best
// templated ctor to invoke. Several helpers, above, aid in this
// pursuit. Each instantiated expression must consume any
// class-specific serialization from serialize_io.
AbstractExpression *ExpressionFactory(json_spirit::Object &obj,
                                      ExpressionType et, ValueType vt,
                                      __attribute__((unused)) int vs,
                                      AbstractExpression* lc,
                                      AbstractExpression* rc) {

  LOG_TRACE("expressionFactory request: " <<
                GetTypeName(et) << " " << et <<
                GetTypeName(vt) << " " << vt << " " <<
                vs << " " << "left : " << lc << "right : " << rc);

  AbstractExpression *ret = nullptr;

  switch (et) {

    // Operators
    case (EXPRESSION_TYPE_OPERATOR_PLUS):
    case (EXPRESSION_TYPE_OPERATOR_MINUS):
    case (EXPRESSION_TYPE_OPERATOR_MULTIPLY):
    case (EXPRESSION_TYPE_OPERATOR_DIVIDE):
    case (EXPRESSION_TYPE_OPERATOR_CONCAT):
    case (EXPRESSION_TYPE_OPERATOR_MOD):
    case (EXPRESSION_TYPE_OPERATOR_CAST):
    case (EXPRESSION_TYPE_OPERATOR_NOT):
    ret = OperatorFactory(et, lc, rc);
    break;

    // Comparisons
    case (EXPRESSION_TYPE_COMPARE_EQ):
    case (EXPRESSION_TYPE_COMPARE_NE):
    case (EXPRESSION_TYPE_COMPARE_LT):
    case (EXPRESSION_TYPE_COMPARE_GT):
    case (EXPRESSION_TYPE_COMPARE_LTE):
    case (EXPRESSION_TYPE_COMPARE_GTE):
    case (EXPRESSION_TYPE_COMPARE_LIKE):
    ret = ComparisonFactory( et, lc, rc);
    break;

    // Conjunctions
    case (EXPRESSION_TYPE_CONJUNCTION_AND):
    case (EXPRESSION_TYPE_CONJUNCTION_OR):
    ret = ConjunctionFactory(et, lc, rc);
    break;

    // Constant Values, parameters, tuples
    case (EXPRESSION_TYPE_VALUE_CONSTANT):
        ret = ConstantValueFactory(obj, vt, et, lc, rc);
    break;

    case (EXPRESSION_TYPE_VALUE_PARAMETER):
        ret = ParameterValueFactory(obj, et, lc, rc);
    break;

    case (EXPRESSION_TYPE_VALUE_TUPLE):
        ret = TupleValueFactory(obj, et, lc, rc);
    break;

    case (EXPRESSION_TYPE_VALUE_TUPLE_ADDRESS):
        ret = new TupleAddressExpression();
    break;

    // must handle all known expressions in this factory
    default:
      char message[256];
      sprintf(message, "Invalid ExpressionType '%s' requested from factory",
              GetTypeName(et).c_str());
      throw ExpressionException(message);
  }

  // written thusly to ease testing/inspecting return content.
  LOG_TRACE("Created " << GetTypeName(et) <<  " expression  : " << ret);
  return ret;
}

boost::shared_array<int> ConvertIfAllTupleValues(const std::vector<AbstractExpression*> &expressions) {
  size_t cnt = expressions.size();
  boost::shared_array<int> ret(new int[cnt]);

  for (uint32_t i = 0; i < cnt; ++i) {
    TupleValueExpressionMarker* casted=
        dynamic_cast<TupleValueExpressionMarker*>(expressions[i]);
    if (casted == nullptr) {
      return boost::shared_array<int>();
    }
    ret[i] = casted->GetColumnId();
  }
  return ret;
}

boost::shared_array<int> ConvertIfAllParameterValues(const std::vector<AbstractExpression*> &expressions){
  size_t cnt = expressions.size();
  boost::shared_array<int> ret(new int[cnt]);

  for (uint32_t i = 0; i < cnt; ++i) {
    ParameterValueExpressionMarker *casted =
        dynamic_cast<ParameterValueExpressionMarker*>(expressions[i]);
    if (casted == nullptr) {
      return boost::shared_array<int>();
    }
    ret[i] = casted->GetParameterId();
  }
  return ret;
}


// return a descriptive string for each typename. could just
//    as easily be a lookup table
std::string GetTypeName(ExpressionType type) {
  std::string ret;
  switch (type) {
    case (EXPRESSION_TYPE_OPERATOR_PLUS):
        ret = "OPERATOR_PLUS";
    break;
    case (EXPRESSION_TYPE_OPERATOR_MINUS):
        ret = "OPERATOR_MINUS";
    break;
    case (EXPRESSION_TYPE_OPERATOR_MULTIPLY):
        ret = "OPERATOR_MULTIPLY";
    break;
    case (EXPRESSION_TYPE_OPERATOR_DIVIDE):
        ret = "OPERATOR_DIVIDE";
    break;
    case (EXPRESSION_TYPE_OPERATOR_CONCAT):
        ret = "OPERATOR_CONCAT";
    break;
    case (EXPRESSION_TYPE_OPERATOR_MOD):
        ret = "OPERATOR_MOD";
    break;
    case (EXPRESSION_TYPE_OPERATOR_CAST):
        ret = "OPERATOR_CAST";
    break;
    case (EXPRESSION_TYPE_OPERATOR_NOT):
        ret = "OPERATOR_NOT";
    break;
    case (EXPRESSION_TYPE_COMPARE_EQ):
        ret = "COMPARE_EQUAL";
    break;
    case (EXPRESSION_TYPE_COMPARE_NE):
        ret = "COMPARE_NOTEQUAL";
    break;
    case (EXPRESSION_TYPE_COMPARE_LT):
        ret = "COMPARE_LESSTHAN";
    break;
    case (EXPRESSION_TYPE_COMPARE_GT):
        ret = "COMPARE_GREATERTHAN";
    break;
    case (EXPRESSION_TYPE_COMPARE_LTE):
        ret = "COMPARE_LESSTHANOREQUALTO";
    break;
    case (EXPRESSION_TYPE_COMPARE_GTE):
        ret = "COMPARE_GREATERTHANOREQUALTO";
    break;
    case (EXPRESSION_TYPE_COMPARE_LIKE):
        ret = "COMPARE_LIKE";
    break;
    case (EXPRESSION_TYPE_CONJUNCTION_AND):
        ret = "CONJUNCTION_AND";
    break;
    case (EXPRESSION_TYPE_CONJUNCTION_OR):
        ret = "CONJUNCTION_OR";
    break;
    case (EXPRESSION_TYPE_VALUE_CONSTANT):
        ret = "VALUE_CONSTANT";
    break;
    case (EXPRESSION_TYPE_VALUE_PARAMETER):
        ret = "VALUE_PARAMETER";
    break;
    case (EXPRESSION_TYPE_VALUE_TUPLE):
        ret = "VALUE_TUPLE";
    break;
    case (EXPRESSION_TYPE_VALUE_TUPLE_ADDRESS):
        ret = "VALUE_TUPLE_ADDRESS";
    break;
    case (EXPRESSION_TYPE_VALUE_NULL):
        ret = "VALUE_NULL";
    break;
    case (EXPRESSION_TYPE_AGGREGATE_COUNT):
        ret = "AGGREGATE_COUNT";
    break;
    case (EXPRESSION_TYPE_AGGREGATE_COUNT_STAR):
        ret = "AGGREGATE_COUNT_STAR";
    break;
    case (EXPRESSION_TYPE_AGGREGATE_SUM):
        ret = "AGGREGATE_SUM";
    break;
    case (EXPRESSION_TYPE_AGGREGATE_MIN):
        ret = "AGGREGATE_MIN";
    break;
    case (EXPRESSION_TYPE_AGGREGATE_MAX):
        ret = "AGGREGATE_MAX";
    break;
    case (EXPRESSION_TYPE_AGGREGATE_AVG):
        ret = "AGGREGATE_AVG";
    break;
    case (EXPRESSION_TYPE_INVALID):
        ret = "INVALID";
    break;
    default: {
      char buffer[32];
      sprintf(buffer, "UNKNOWN[%d]", type);
      ret = buffer;
    }
  }
  return (ret);
}

} // End expression namespace
} // End peloton namespace

