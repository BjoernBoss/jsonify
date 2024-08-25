#pragma once

#include <ustring/str.h>

#include <cinttypes>
#include <variant>
#include <concepts>
#include <type_traits>
#include <string>
#include <utility>
#include <limits>
#include <stdexcept>
#include <memory>
#include <iterator>

namespace json {
	/* primitive json-types */
	using UNum = uint64_t;
	using INum = int64_t;
	using Real = long double;
	using Bool = bool;
	struct Null {};
	using Str = std::wstring;

	enum class Type : uint8_t {
		null,
		boolean,
		unumber,
		inumber,
		real,
		string,
		array,
		object
	};

	/* exception thrown when accessing a constant json::Value as a certain type, which it is not */
	class JsonTypeException : public std::runtime_error {
	public:
		JsonTypeException(const std::string& s) : runtime_error(s) {}
	};

	/* exception thrown when accessing an out-of-range index for array-like accesses */
	class JsonRangeException : public std::runtime_error {
	public:
		JsonRangeException(const std::string& s) : runtime_error(s) {}
	};

	/* exception thrown when an already closed builder-object is being set again */
	class JsonBuilderException : public std::runtime_error {
	public:
		JsonBuilderException(const std::string& s) : runtime_error(s) {}
	};

	/* exception thrown when an already closed reader-object is being read again */
	class JsonReaderException : public std::runtime_error {
	public:
		JsonReaderException(const std::string& s) : runtime_error(s) {}
	};

	/* exception thrown when decoding or parsing of a json-string fails */
	struct JsonDeserializeException : public std::runtime_error {
		JsonDeserializeException(const std::string& s) : runtime_error(s) {}
	};

	namespace detail {
		template <class Type>
		concept IsPair = requires(const Type t) {
			{ t.first };
			{ t.second };
		};
		template <class Type>
		concept IsNotPair = !detail::IsPair<Type>;
	}

	/* check if the type is a primitive json-value [null, bool, real, number] */
	template <class Type>
	concept IsPrimitive = std::same_as<std::remove_cvref_t<Type>, json::Null> || std::integral<std::remove_cvref_t<Type>> || std::floating_point<std::remove_cvref_t<Type>>;

	/* check if the type can be used as json-string, which must be any string-type by str::IsStr */
	template <class Type>
	concept IsString = str::IsStr<Type>;

	/* check if the type can be used as json-object, which is an iterator of pairs of strings and other values [values must implement json::IsJson] */
	template <class Type>
	concept IsObject = requires(const Type t) {
		{ *t.begin() } -> detail::IsPair;
		{ t.begin()->first } -> json::IsString;
		{ ++t.begin() };
		{ *t.end() } -> std::same_as<decltype(*t.begin())>;
	};

	/* check if the type can be used as json-array, which is an iterator of values [values must implement json::IsJson] */
	template <class Type>
	concept IsArray = !json::IsString<Type> && requires(const Type t) {
		{ *t.begin() } -> detail::IsNotPair;
		{ ++t.begin() };
		{ *t.end() } -> std::same_as<decltype(*t.begin())>;
	};

	/* check if the type can be used as overall json-value, which must offer flags and corresponding values for every possible json-type */
	template <class Type>
	concept IsValue = requires(const Type t) {
		{ t.isNull() } -> std::same_as<bool>;

		{ t.isBoolean() } -> std::same_as<bool>;
		{ t.boolean() } -> std::convertible_to<json::Bool>;

		{ t.isUNum() } -> std::same_as<bool>;
		{ t.unum() } -> std::convertible_to<json::UNum>;

		{ t.isINum() } -> std::same_as<bool>;
		{ t.inum() } -> std::convertible_to<json::INum>;

		{ t.isReal() } -> std::same_as<bool>;
		{ t.real() } -> std::convertible_to<json::Real>;

		{ t.isStr() } -> std::same_as<bool>;
		{ t.str() } -> json::IsString;

		{ t.isArr() } -> std::same_as<bool>;
		{ t.arr() } -> json::IsArray;

		{ t.isObj() } -> std::same_as<bool>;
		{ t.obj() } -> json::IsObject;
	};

	/* check if the type is any valid json-value */
	template <class Type>
	concept IsJson = json::IsPrimitive<Type> || json::IsString<Type> || json::IsArray<Type> || json::IsObject<Type> || json::IsValue<Type>;
}
