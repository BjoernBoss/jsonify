#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>
#include <variant>
#include <stdexcept>
#include <iostream>

namespace json {
	class Value;
	using Arr = std::vector<json::Value>;
	using Obj = std::map<std::wstring, json::Value>;
	using Str = std::wstring;
	using UNum = uint64_t;
	using INum = int64_t;
	using Real = double;
	using Bool = bool;
	struct Null {};

	class JsonTypeException : public std::runtime_error {
	public:
		JsonTypeException(const std::string& s) : runtime_error(s) {}
	};
	class JsonRangeException : public std::runtime_error {
	public:
		JsonRangeException(const std::string& s) : runtime_error(s) {}
	};
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
		using ValueParent = std::variant<json::Null, json::Arr, json::Obj, json::Str, json::UNum, json::INum, json::Real, json::Bool>;
	}

	class Value : private detail::ValueParent {
	public:
		Value() : detail::ValueParent(json::Null()) {}
		Value(const json::Value&) = default;
		Value(json::Value&&) = default;

		Value(const json::Null& n) : detail::ValueParent(n) {}
		Value(json::Null&& n) : detail::ValueParent(std::move(n)) {}

		Value(const json::Arr& a) : detail::ValueParent(a) {}
		Value(json::Arr&& a) noexcept : detail::ValueParent(std::move(a)) {}

		Value(const json::Obj& o) : detail::ValueParent(o) {}
		Value(json::Obj&& o) noexcept : detail::ValueParent(std::move(o)) {}

		Value(const json::Str& s) : detail::ValueParent(s) {}
		Value(json::Str&& s) noexcept : detail::ValueParent(std::move(s)) {}

		Value(const wchar_t* s) : detail::ValueParent(std::move(std::wstring(s))) {}
		Value(json::UNum n) noexcept : detail::ValueParent(n) {}
		Value(json::INum n) noexcept : detail::ValueParent(n) {}
		Value(int n) noexcept {
			if (n < 0)
				static_cast<detail::ValueParent&>(*this) = json::INum(n);
			else
				static_cast<detail::ValueParent&>(*this) = json::UNum(n);
		}
		Value(json::Real n) noexcept : detail::ValueParent(n) {}
		Value(json::Bool b) noexcept : detail::ValueParent(b) {}

	public:
		json::Value& operator=(const json::Value&) = default;
		json::Value& operator=(json::Value&&) = default;
		bool operator==(const json::Value& v) const {
			if (std::holds_alternative<json::Bool>(*this)) {
				if (!std::holds_alternative<json::Bool>(v))
					return false;
				return (std::get<json::Bool>(*this) == std::get<json::Bool>(v));
			}
			if (std::holds_alternative<json::Obj>(*this)) {
				if (!std::holds_alternative<json::Obj>(v))
					return false;
				return (std::get<json::Obj>(*this) == std::get<json::Obj>(v));
			}
			if (std::holds_alternative<json::Arr>(*this)) {
				if (!std::holds_alternative<json::Arr>(v))
					return false;
				return (std::get<json::Arr>(*this) == std::get<json::Arr>(v));
			}
			if (std::holds_alternative<json::Str>(*this)) {
				if (!std::holds_alternative<json::Str>(v))
					return false;
				return (std::get<json::Str>(*this) == std::get<json::Str>(v));
			}
			if (std::holds_alternative<json::Null>(*this))
				return std::holds_alternative<json::Null>(v);

			if (std::holds_alternative<json::Real>(*this)) {
				if (std::holds_alternative<json::Real>(v))
					return (std::get<json::Real>(*this) == std::get<json::Real>(v));
				if (std::holds_alternative<json::INum>(v))
					return (std::get<json::Real>(*this) == std::get<json::INum>(v));
				if (std::holds_alternative<json::UNum>(v))
					return (std::get<json::Real>(*this) == std::get<json::UNum>(v));
			}
			else if (std::holds_alternative<json::INum>(*this)) {
				if (std::holds_alternative<json::Real>(v))
					return (std::get<json::INum>(*this) == std::get<json::Real>(v));
				if (std::holds_alternative<json::INum>(v))
					return (std::get<json::INum>(*this) == std::get<json::INum>(v));
				if (std::holds_alternative<json::UNum>(v))
					return (std::get<json::INum>(*this) == std::get<json::UNum>(v));
			}
			else if (std::holds_alternative<json::UNum>(*this)) {
				if (std::holds_alternative<json::Real>(v))
					return (std::get<json::UNum>(*this) == std::get<json::Real>(v));
				if (std::holds_alternative<json::INum>(v))
					return (std::get<json::UNum>(*this) == std::get<json::INum>(v));
				if (std::holds_alternative<json::UNum>(v))
					return (std::get<json::UNum>(*this) == std::get<json::UNum>(v));
			}
			return false;
		}
		bool operator!=(const json::Value& v) const {
			return !(*this == v);
		}

	private:
		void fEnsureType(json::Type t) {
			switch (t) {
			case json::Type::array:
				if (!std::holds_alternative<json::Arr>(*this))
					static_cast<detail::ValueParent&>(*this) = json::Arr();
				break;
			case json::Type::object:
				if (!std::holds_alternative<json::Obj>(*this))
					static_cast<detail::ValueParent&>(*this) = json::Obj();
				break;
			case json::Type::string:
				if (!std::holds_alternative<json::Str>(*this))
					static_cast<detail::ValueParent&>(*this) = json::Str();
				break;
			case json::Type::unumber:
				if (!std::holds_alternative<json::UNum>(*this)) {
					if (std::holds_alternative<json::INum>(*this) && std::get<json::INum>(*this) >= 0)
						static_cast<detail::ValueParent&>(*this) = json::UNum(std::get<json::INum>(*this));
					else
						static_cast<detail::ValueParent&>(*this) = json::UNum();
				}
				break;
			case json::Type::inumber:
				if (!std::holds_alternative<json::INum>(*this)) {
					if (std::holds_alternative<json::UNum>(*this))
						static_cast<detail::ValueParent&>(*this) = json::INum(std::get<json::UNum>(*this));
					else
						static_cast<detail::ValueParent&>(*this) = json::INum();
				}
				break;
			case json::Type::real:
				if (!std::holds_alternative<json::Real>(*this)) {
					if (std::holds_alternative<json::UNum>(*this))
						static_cast<detail::ValueParent&>(*this) = json::Real(std::get<json::UNum>(*this));
					else if (std::holds_alternative<json::INum>(*this))
						static_cast<detail::ValueParent&>(*this) = json::Real(std::get<json::INum>(*this));
					else
						static_cast<detail::ValueParent&>(*this) = json::Real();
				}
				break;
			case json::Type::boolean:
				if (!std::holds_alternative<json::Bool>(*this))
					static_cast<detail::ValueParent&>(*this) = json::Bool();
				break;
			default:
				if (!std::holds_alternative<json::Null>(*this))
					static_cast<detail::ValueParent&>(*this) = json::Null();
			}
		}
		bool fConvertable(json::Type t) const {
			switch (t) {
			case json::Type::array:
				return std::holds_alternative<json::Arr>(*this);
			case json::Type::object:
				return std::holds_alternative<json::Obj>(*this);
			case json::Type::string:
				return std::holds_alternative<json::Str>(*this);
			case json::Type::unumber:
				if (std::holds_alternative<json::UNum>(*this))
					return true;
				if (std::holds_alternative<json::INum>(*this) && std::get<json::INum>(*this) >= 0)
					return true;
				return false;
			case json::Type::inumber:
				if (std::holds_alternative<json::INum>(*this))
					return true;
				if (std::holds_alternative<json::UNum>(*this))
					return true;
				return false;
			case json::Type::real:
				if (std::holds_alternative<json::Real>(*this))
					return true;
				if (std::holds_alternative<json::INum>(*this))
					return true;
				if (std::holds_alternative<json::UNum>(*this))
					return true;
				return false;
			case json::Type::boolean:
				return std::holds_alternative<json::Bool>(*this);
			default:
				return std::holds_alternative<json::Null>(*this);
			}
		}

	public:
		bool isNull() const {
			return std::holds_alternative<json::Null>(*this);
		}
		bool isBoolean() const {
			return std::holds_alternative<json::Bool>(*this);
		}
		bool isStr() const {
			return std::holds_alternative<json::Str>(*this);
		}
		bool isUNum() const {
			if (std::holds_alternative<json::UNum>(*this))
				return true;
			return (std::holds_alternative<json::INum>(*this) && std::get<json::INum>(*this) >= 0);
		}
		bool isINum() const {
			return std::holds_alternative<json::UNum>(*this) || std::holds_alternative<json::INum>(*this);
		}
		bool isReal() const {
			return std::holds_alternative<json::UNum>(*this) || std::holds_alternative<json::INum>(*this) || std::holds_alternative<json::Real>(*this);
		}
		bool isObj() const {
			return std::holds_alternative<json::Obj>(*this);
		}
		bool isArr() const {
			return std::holds_alternative<json::Arr>(*this);
		}
		bool is(json::Type t) const {
			return fConvertable(t);
		}
		json::Type type() const {
			if (std::holds_alternative<json::Bool>(*this))
				return json::Type::boolean;
			if (std::holds_alternative<json::Str>(*this))
				return json::Type::string;
			if (std::holds_alternative<json::Obj>(*this))
				return json::Type::object;
			if (std::holds_alternative<json::Arr>(*this))
				return json::Type::array;
			if (std::holds_alternative<json::Real>(*this))
				return json::Type::real;
			if (std::holds_alternative<json::UNum>(*this))
				return json::Type::unumber;
			if (std::holds_alternative<json::INum>(*this))
				return json::Type::inumber;
			return json::Type::null;
		}

	public:
		json::Bool& boolean() {
			fEnsureType(json::Type::boolean);
			return std::get<json::Bool>(*this);
		}
		json::Str& str() {
			fEnsureType(Type::string);
			return std::get<json::Str>(*this);
		}
		json::UNum& unum() {
			fEnsureType(Type::unumber);
			return std::get<json::UNum>(*this);
		}
		json::INum& inum() {
			fEnsureType(Type::inumber);
			return std::get<json::INum>(*this);
		}
		json::Real& real() {
			fEnsureType(Type::real);
			return std::get<json::Real>(*this);
		}
		json::Arr& arr() {
			fEnsureType(Type::array);
			return std::get<json::Arr>(*this);
		}
		json::Obj& obj() {
			fEnsureType(Type::object);
			return std::get<json::Obj>(*this);
		}

		json::Bool boolean() const {
			if (!std::holds_alternative<json::Bool>(*this))
				throw JsonTypeException("json::Value is not a bool");
			return std::get<json::Bool>(*this);
		}
		const json::Str& str() const {
			if (!std::holds_alternative<json::Str>(*this))
				throw JsonTypeException("json::Value is not a string");
			return std::get<json::Str>(*this);
		}
		json::UNum unum() const {
			if (std::holds_alternative<json::INum>(*this) && std::get<json::INum>(*this) >= 0)
				return json::UNum(std::get<json::INum>(*this));
			if (std::holds_alternative<json::Real>(*this) && std::get<json::Real>(*this) >= 0)
				return json::UNum(std::get<json::Real>(*this));

			if (!std::holds_alternative<json::UNum>(*this))
				throw JsonTypeException("json::Value is not an unsigned-number");
			return std::get<json::UNum>(*this);
		}
		json::INum inum() const {
			if (std::holds_alternative<json::UNum>(*this))
				return json::INum(std::get<json::UNum>(*this));
			if (std::holds_alternative<json::Real>(*this))
				return json::INum(std::get<json::Real>(*this));

			if (!std::holds_alternative<json::INum>(*this))
				throw JsonTypeException("json::Value is not a signed-number");
			return std::get<json::INum>(*this);
		}
		json::Real real() const {
			if (std::holds_alternative<json::UNum>(*this))
				return json::Real(std::get<json::UNum>(*this));
			if (std::holds_alternative<json::INum>(*this))
				return json::Real(std::get<json::INum>(*this));

			if (!std::holds_alternative<json::Real>(*this))
				throw JsonTypeException("json::Value is not a real");
			return std::get<json::Real>(*this);
		}
		const json::Arr& arr() const {
			if (!std::holds_alternative<json::Arr>(*this))
				throw JsonTypeException("json::Value is not a array");
			return std::get<json::Arr>(*this);
		}
		const json::Obj& obj() const {
			if (!std::holds_alternative<json::Obj>(*this))
				throw JsonTypeException("json::Value is not an object");
			return std::get<json::Obj>(*this);
		}

		/* operations shared between array/string/objects (depends on type, zero for non-container types) */
		size_t size() const {
			if (std::holds_alternative<json::Arr>(*this))
				return std::get<json::Arr>(*this).size();
			if (std::holds_alternative<json::Obj>(*this))
				return std::get<json::Obj>(*this).size();
			if (std::holds_alternative<json::Str>(*this))
				return std::get<json::Str>(*this).size();
			return 0;
		}
		size_t size(json::Type t) const {
			if (t == Type::array && std::holds_alternative<json::Arr>(*this))
				return std::get<json::Arr>(*this).size();
			else if (t == Type::object && std::holds_alternative<json::Obj>(*this))
				return std::get<json::Obj>(*this).size();
			else if (t == Type::string && std::holds_alternative<json::Str>(*this))
				return std::get<json::Str>(*this).size();
			return 0;
		}
		bool empty() const {
			if (std::holds_alternative<json::Arr>(*this))
				return std::get<json::Arr>(*this).empty();
			if (std::holds_alternative<json::Obj>(*this))
				return std::get<json::Obj>(*this).empty();
			if (std::holds_alternative<json::Str>(*this))
				return std::get<json::Str>(*this).empty();
			return true;
		}
		bool empty(json::Type t) const {
			if (t == Type::array && std::holds_alternative<json::Arr>(*this))
				return std::get<json::Arr>(*this).empty();
			else if (t == Type::object && std::holds_alternative<json::Obj>(*this))
				return std::get<json::Obj>(*this).empty();
			else if (t == Type::string && std::holds_alternative<json::Str>(*this))
				return std::get<json::Str>(*this).empty();
			return true;
		}

		const json::Value& operator[](const std::wstring& k) const {
			if (!std::holds_alternative<json::Obj>(*this))
				throw JsonTypeException("json::Value is not a object");
			const json::Obj& obj = std::get<json::Obj>(*this);
			auto it = obj.find(k);
			if (it == obj.end())
				throw JsonRangeException("Key not in object");
			return it->second;
		}
		json::Value& operator[](const std::wstring& k) {
			fEnsureType(Type::object);
			return std::get<json::Obj>(*this)[k];
		}
		void erase(const std::wstring& k) {
			fEnsureType(Type::object);
			std::get<json::Obj>(*this).erase(k);
		}
		bool contains(const std::wstring& k) const {
			if (!std::holds_alternative<json::Obj>(*this))
				return false;
			const json::Obj& obj = std::get<json::Obj>(*this);
			return obj.find(k) != obj.end();
		}
		bool contains(const std::wstring& k, json::Type t) const {
			if (!std::holds_alternative<json::Obj>(*this))
				return false;
			const json::Obj& obj = std::get<json::Obj>(*this);
			auto it = obj.find(k);
			return (it != obj.end() && it->second.fConvertable(t));
		}
		bool typedObject(json::Type t) const {
			if (!std::holds_alternative<json::Obj>(*this))
				return false;
			const json::Obj& obj = std::get<json::Obj>(*this);

			for (const auto& val : obj) {
				if (!val.second.fConvertable(t))
					return false;
			}
			return true;
		}

		const json::Value& operator[](size_t i) const {
			if (!std::holds_alternative<json::Arr>(*this))
				throw JsonTypeException("json::Value is not a array");
			const json::Arr& arr = std::get<json::Arr>(*this);
			if (i >= arr.size())
				throw JsonRangeException("Array index out of range");
			return arr[i];
		}
		json::Value& operator[](size_t i) {
			fEnsureType(Type::array);
			json::Arr& arr = std::get<json::Arr>(*this);
			if (i >= arr.size())
				throw JsonRangeException("Array index out of range");
			return arr[i];
		}
		void push(const json::Value& v) {
			fEnsureType(Type::array);
			std::get<json::Arr>(*this).push_back(v);
		}
		void pop() {
			fEnsureType(Type::array);
			json::Arr& arr = std::get<json::Arr>(*this);
			if (!arr.empty())
				arr.pop_back();
		}
		void resize(size_t sz) {
			fEnsureType(Type::array);
			std::get<json::Arr>(*this).resize(sz);
		}
		bool has(size_t i) const {
			if (!std::holds_alternative<json::Arr>(*this))
				return false;
			const json::Arr& arr = std::get<json::Arr>(*this);
			return (i < arr.size());
		}
		bool has(size_t i, json::Type t) const {
			if (!std::holds_alternative<json::Arr>(*this))
				return false;
			const json::Arr& arr = std::get<json::Arr>(*this);
			return (i < arr.size() && arr[i].fConvertable(t));
		}
		bool typedArray(json::Type t) const {
			if (!std::holds_alternative<json::Arr>(*this))
				return false;
			const json::Arr& arr = std::get<json::Arr>(*this);

			for (size_t i = 0; i < arr.size(); ++i) {
				if (!arr[i].fConvertable(t))
					return false;
			}
			return true;
		}
	};

	/*
	 *	indent empty => no linebreaks and compact structure
	 *
	 *	- Output json conform
	 *	- Strings are parsed as utf16/utf32
	 */
	std::string Serialize(const json::Value& v, const std::string& indent = "\t");

	/*
	 *	- Input expected as utf8
	 *	- Escape sequence [u] treated as utf16 encoding and transcoded to uft16/utf32
	 *	- Strings are encoded as uft16/utf32
	 *	- Consume entire string/stream else fail
	 *	- In objects with multiple identical keys, the last occurring value will be returned
	 */
	std::pair<json::Value, bool> Deserialize(const std::string_view& s);
	std::pair<json::Value, bool> Deserialize(std::istream& s);
}
