#pragma once

#include <cstdint>
#include <variant>

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
		using Primitive = std::variant<json::Null, json::UNum, json::INum, json::Real, json::Bool>;
	}
}
