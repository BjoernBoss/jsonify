#pragma once

#include <unicode-string/str.h>

#include <cinttypes>
#include <variant>
#include <concepts>
#include <type_traits>
#include <string>
#include <utility>
#include <limits>
#include <stdexcept>

namespace json {
	/* primitive json-types */
	using UNum = uint64_t;
	using INum = int64_t;
	using Real = long double;
	using Bool = bool;
	struct Null {};

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

	namespace detail {
		template <class>
		struct IsPair { static constexpr bool value = false; };
		template <class A, class B>
		struct IsPair<std::pair<A, B>> { static constexpr bool value = true; };
		template <class T>
		concept IsNotPair = !detail::IsPair<std::remove_cvref_t<T>>::value;
	}

	/* check if the type is a primitive json-value [null, bool, real, number] */
	template <class Type>
	concept IsPrimitive = std::same_as<std::remove_cvref_t<Type>, json::Null> || std::integral<std::remove_cvref_t<Type>> || std::floating_point<std::remove_cvref_t<Type>>;

	/* check if the type can be used as json-string, which must be any string-type by str::AnyStr */
	template <class Type>
	concept IsString = str::AnyStr<Type>;

	/* check if the type can be used as json-object, which is an iterator of pairs of strings and other values [values must implement json::IsJson] */
	template <class Type>
	concept IsObject = requires(const Type t) {
		{ t.begin()->first } -> json::IsString;
		{ t.begin()->second };
		{ *t.end() } -> std::same_as<decltype(*t.begin())>;
	};

	/* check if the type can be used as json-array, which is an iterator of values [values must implement json::IsJson] */
	template <class Type>
	concept IsArray = !json::IsString<Type> && requires(const Type t) {
		{ *t.begin() } -> detail::IsNotPair;
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
