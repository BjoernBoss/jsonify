#pragma once

#include <str-tools/str-conv.h>

#include <cstdint>
#include <variant>
#include <concepts>
#include <string>

namespace json {
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
		/* json-null first to default-construct as null */
		template <class AType, class SType, class OType>
		using JsonTypes = std::variant<json::Null, json::UNum, json::INum, json::Real, json::Bool, AType, SType, OType>;
	}

	template <class Type>
	concept IsString = stc::IsString<Type>;

	template <class Type>
	concept IsValue = requires(const Type t, size_t n) {
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
		{ t.arr() };
		{ *t.arr().begin() } -> std::convertible_to<const Type>;
		{ *t.arr().end() } -> std::convertible_to<const Type>;

		{ t.isObj() } -> std::same_as<bool>;
		{ t.obj() };
		{ t.obj().begin()->first } -> json::IsString;
		{ t.obj().begin()->second } -> std::convertible_to<const Type>;
		{ t.obj().end()->first } -> json::IsString;
		{ t.obj().end()->second } -> std::convertible_to<const Type>;
	};

	template <class Type>
	concept IsPrimitive = std::same_as<std::decay_t<Type>, json::Null> || std::integral<std::decay_t<Type>> || std::floating_point<std::decay_t<Type>>;

	template <class Type>
	concept IsArray = requires(const Type t, size_t n) {
		{ *t.begin() } -> json::IsValue;
		{ *t.end() } -> json::IsValue;
	};

	template <class Type>
	concept IsObject = requires(const Type t) {
		{ t.begin()->first } -> json::IsString;
		{ t.begin()->second } -> json::IsValue;
		{ t.end()->first } -> json::IsString;
		{ t.end()->second } -> json::IsValue;
	};

	template <class Type>
	concept IsJson = json::IsString<Type> || json::IsValue<Type> || json::IsPrimitive<Type> || json::IsObject<Type> || json::IsArray<Type>;
}
