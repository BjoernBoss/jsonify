/* SPDX-License-Identifier: BSD-3-Clause */
/* Copyright (c) 2024-2026 Bjoern Boss Henrichsen */
#pragma once

#include <ustring/ustring.h>

#include <cinttypes>
#include <variant>
#include <concepts>
#include <type_traits>
#include <string>
#include <utility>
#include <limits>
#include <memory>
#include <iterator>
#include <vector>

namespace json {
	/* primitive json-types */
	using UNum = uint64_t;
	using INum = int64_t;
	using Real = double;
	using Bool = bool;
	using Str = std::string;
	using StrView = std::string_view;
	struct NullType {};

	/* quick-access to null */
	constexpr json::NullType Null = json::NullType{};

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

	/* exception thrown when using the library in an invalid way */
	struct Exception : public str::ch::BuildException {
		template <class... Args>
		constexpr Exception(const Args&... args) : str::ch::BuildException{ args... } {}
	};

	/* exception thrown when accessing a constant json::Value as a certain type, which it is not */
	struct TypeException : public json::Exception {
		template <class... Args>
		constexpr TypeException(const Args&... args) : json::Exception{ args... } {}
	};

	/* exception thrown when accessing an out-of-range index for array-like accesses */
	struct RangeException : public json::Exception {
		template <class... Args>
		constexpr RangeException(const Args&... args) : json::Exception{ args... } {}
	};

	/* exception thrown when an already closed builder-object is being set again */
	struct BuilderException : public json::Exception {
		template <class... Args>
		constexpr BuilderException(const Args&... args) : json::Exception{ args... } {}
	};

	/* exception thrown when an already closed reader-object is being read again */
	struct ReaderException : public json::Exception {
		template <class... Args>
		constexpr ReaderException(const Args&... args) : json::Exception{ args... } {}
	};

	/* exception thrown when decoding or parsing of a json-string fails */
	struct DeserializeException : public json::Exception {
		template <class... Args>
		constexpr DeserializeException(const Args&... args) : json::Exception{ args... } {}
	};

	/* check if the type is a primitive json-value [null, bool, real, number] */
	template <class Type>
	concept IsPrimitive = std::same_as<std::remove_cvref_t<Type>, json::NullType> || std::integral<std::remove_cvref_t<Type>> || std::floating_point<std::remove_cvref_t<Type>>;

	/* check if the type can be used as json-string, which must be any string-type by str::IsStr */
	template <class Type>
	concept IsString = str::IsStr<Type>;

	namespace detail {
		template <class Type> struct IsObjIt { static constexpr bool value = false; };
		template <json::IsString A, class B> struct IsObjIt<std::pair<A, B>> { static constexpr bool value = true; };
		template <class Type>
		concept IsObjItConcept = detail::IsObjIt<std::remove_cvref_t<Type>>::value;

		/*
		*	why this weird design instead of just testing for it being an iterator?
		*		because em++ v3.1.40 cannot evaluate { *t.begin() } for json::Value::operator=,
		*		as it tries to instantiate the std::map value_type and fails, because the type is incomplete,
		*		when matching operator=(json::IsJson auto) with the type being json::Value - somehow skipping
		*		operator=(json::Value&) because why use SFINAE...
		*/
		template <class Type>
		concept IsObject = requires(const Type t) {
			typename Type::const_iterator;
			typename Type::const_iterator::value_type;
			{ t.begin() } -> std::convertible_to<typename Type::const_iterator>;
			{ t.end() } -> std::convertible_to<decltype(t.begin())>;
			{ std::declval<typename Type::const_iterator::value_type>() } -> detail::IsObjItConcept;
			//{ *t.begin() } -> detail::IsPair;
		};

		template <class Type> struct IsArrIt { static constexpr bool value = true; };
		template <class A, class B> struct IsArrIt<std::pair<A, B>> { static constexpr bool value = false; };
		template <class Type>
		concept IsArrItConcept = detail::IsArrIt<std::remove_cvref_t<Type>>::value;

		template <class Type>
		concept IsArray = requires(const Type t) {
			typename Type::const_iterator;
			{ t.begin() } -> std::convertible_to<typename Type::const_iterator>;
			{ t.end() } -> std::convertible_to<decltype(t.begin())>;
			{ *t.begin() } -> detail::IsArrItConcept;
		};
	}

	/* check if the type can be used as json-object, which is an iterator of pairs of strings and other values [values must implement json::IsJson] */
	template <class Type>
	concept IsObject = detail::IsObject<std::remove_cvref_t<Type>>;

	/* check if the type can be used as json-array, which is an iterator of values [values must implement json::IsJson] */
	template <class Type>
	concept IsArray = !json::IsString<Type> && detail::IsArray<std::remove_cvref_t<Type>>;

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
