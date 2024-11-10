/* SPDX-License-Identifier: BSD-3-Clause */
/* Copyright (c) 2024 Bjoern Boss Henrichsen */
#pragma once

#include "json-common.h"

#include <unordered_map>
#include <vector>

namespace json {
	class Value;

	/* json::Arr is a std::vector of json::Values */
	using Arr = std::vector<json::Value>;

	/* json::Obj is a std::unordered_map of json::Str to json::Values */
	using Obj = std::unordered_map<json::Str, json::Value>;

	namespace detail {
		using StrPtr = std::unique_ptr<json::Str>;
		using ArrPtr = std::unique_ptr<json::Arr>;
		using ObjPtr = std::unique_ptr<json::Obj>;

		/* json-null first to default-construct as null */
		using ValueParent = std::variant<json::Null, json::UNum, json::INum, json::Real, json::Bool, detail::ArrPtr, detail::StrPtr, detail::ObjPtr>;
	}

	/* [json::IsJson] representation of a single json value of any kind
	*	- uses json::Null/json::UNum/json::INum/json::Real/json::Bool/json::Arr/json::Str/json::Obj
	*	- user-friendly and will convert to requested type whenever possible
	*	- missing object-keys will either insert json::Null or return a constant json::Null
	*	- uses str::err::DefChar for any string transcoding while assigning strings */
	class Value : private detail::ValueParent {
	public:
		constexpr Value() : detail::ValueParent{ json::Null() } {}
		constexpr Value(json::Value&&) = default;
		constexpr Value(const json::Value& v) : detail::ValueParent{ json::Null() } {
			*this = v;
		}
		constexpr Value(const json::Arr& v) : detail::ValueParent{ std::make_unique<json::Arr>(v) } {}
		constexpr Value(json::Arr&& v) : detail::ValueParent{ std::make_unique<json::Arr>(std::move(v)) } {}
		constexpr Value(const json::Obj& v) : detail::ValueParent{ std::make_unique<json::Obj>(v) } {}
		constexpr Value(json::Obj&& v) : detail::ValueParent{ std::make_unique<json::Obj>(std::move(v)) } {}
		constexpr Value(const json::Str& v) : detail::ValueParent{ std::make_unique<json::Str>(v) } {}
		constexpr Value(json::Str&& v) : detail::ValueParent{ std::make_unique<json::Str>(std::move(v)) } {}
		constexpr Value(const json::IsJson auto& v) : detail::ValueParent{ json::Null() } {
			using Type = decltype(v);
			fAssignValue<Type>(std::forward<Type>(v));
		}

	public:
		constexpr json::Value& operator=(json::Value&&) = default;
		constexpr json::Value& operator=(const json::Value& v) {
			if (std::holds_alternative<detail::ObjPtr>(v))
				static_cast<detail::ValueParent&>(*this) = std::make_unique<json::Obj>(*std::get<detail::ObjPtr>(v));
			else if (std::holds_alternative<detail::ArrPtr>(v))
				static_cast<detail::ValueParent&>(*this) = std::make_unique<json::Arr>(*std::get<detail::ArrPtr>(v));
			else if (std::holds_alternative<detail::StrPtr>(v))
				static_cast<detail::ValueParent&>(*this) = std::make_unique<json::Str>(*std::get<detail::StrPtr>(v));
			else if (std::holds_alternative<json::Bool>(v))
				static_cast<detail::ValueParent&>(*this) = std::get<json::Bool>(v);
			else if (std::holds_alternative<json::Null>(v))
				static_cast<detail::ValueParent&>(*this) = json::Null();
			else if (std::holds_alternative<json::Real>(v))
				static_cast<detail::ValueParent&>(*this) = std::get<json::Real>(v);
			else if (std::holds_alternative<json::INum>(v))
				static_cast<detail::ValueParent&>(*this) = std::get<json::INum>(v);
			else if (std::holds_alternative<json::UNum>(v))
				static_cast<detail::ValueParent&>(*this) = std::get<json::UNum>(v);
			return *this;
		}
		constexpr json::Value& operator=(const json::Arr& v) {
			static_cast<detail::ValueParent&>(*this) = std::make_unique<json::Arr>(v);
			return *this;
		}
		constexpr json::Value& operator=(json::Arr&& v) {
			static_cast<detail::ValueParent&>(*this) = std::make_unique<json::Arr>(std::move(v));
			return *this;
		}
		constexpr json::Value& operator=(const json::Obj& v) {
			static_cast<detail::ValueParent&>(*this) = std::make_unique<json::Obj>(v);
			return *this;
		}
		constexpr json::Value& operator=(json::Obj&& v) {
			static_cast<detail::ValueParent&>(*this) = std::make_unique<json::Obj>(std::move(v));
			return *this;
		}
		constexpr json::Value& operator=(const json::Str& v) {
			static_cast<detail::ValueParent&>(*this) = std::make_unique<json::Str>(v);
			return *this;
		}
		constexpr json::Value& operator=(json::Str&& v) {
			static_cast<detail::ValueParent&>(*this) = std::make_unique<json::Str>(std::move(v));
			return *this;
		}
		constexpr json::Value& operator=(const json::IsJson auto& v) {
			using Type = decltype(v);
			fAssignValue<Type>(std::forward<Type>(v));
			return *this;
		}
		constexpr bool operator==(const json::Value& v) const {
			if (std::holds_alternative<json::Bool>(*this)) {
				if (!std::holds_alternative<json::Bool>(v))
					return false;
				return (std::get<json::Bool>(*this) == std::get<json::Bool>(v));
			}
			if (std::holds_alternative<detail::ObjPtr>(*this)) {
				if (!std::holds_alternative<detail::ObjPtr>(v))
					return false;
				return (*std::get<detail::ObjPtr>(*this) == *std::get<detail::ObjPtr>(v));
			}
			if (std::holds_alternative<detail::ArrPtr>(*this)) {
				if (!std::holds_alternative<detail::ArrPtr>(v))
					return false;
				return (*std::get<detail::ArrPtr>(*this) == *std::get<detail::ArrPtr>(v));
			}
			if (std::holds_alternative<detail::StrPtr>(*this)) {
				if (!std::holds_alternative<detail::StrPtr>(v))
					return false;
				return (*std::get<detail::StrPtr>(*this) == *std::get<detail::StrPtr>(v));
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
		constexpr bool operator!=(const json::Value& v) const {
			return !(*this == v);
		}

	private:
		template <class Type>
		constexpr void fAssignValue(Type&& val) {
			if constexpr (json::IsObject<Type>) {
				fEnsureType(json::Type::object);
				json::Obj& obj = *std::get<detail::ObjPtr>(*this);
				for (const auto& entry : val) {
					if constexpr (std::convertible_to<decltype(entry.first), json::Str>)
						obj[entry.first] = json::Value(entry.second);
					else
						obj[json::Str{ entry.first }] = json::Value(entry.second);
				}
			}
			else if constexpr (json::IsString<Type>) {
				fEnsureType(json::Type::string);
				json::Str& str = *std::get<detail::StrPtr>(*this);
				str::TranscodeAllTo<str::err::DefChar>(str, val);
			}
			else if constexpr (json::IsArray<Type>) {
				fEnsureType(json::Type::array);
				json::Arr& arr = *std::get<detail::ArrPtr>(*this);
				for (const auto& entry : val)
					arr.push_back(json::Value(entry));
			}
			else if constexpr (json::IsValue<Type>) {
				if (val.isArr())
					fAssignValue(val.arr());
				else if (val.isObj())
					fAssignValue(val.obj());
				else if (val.isStr())
					fAssignValue(val.str());
				else if (val.isINum())
					fAssignValue(val.inum());
				else if (val.isUNum())
					fAssignValue(val.unum());
				else if (val.isReal())
					fAssignValue(val.real());
				else if (val.isBoolean())
					fAssignValue(val.boolean());
				else
					fAssignValue(json::Null());
			}
			else {
				static_assert(json::IsPrimitive<Type>);
				using VType = std::remove_cvref_t<Type>;

				if constexpr (std::same_as<VType, json::Bool>) {
					fEnsureType(json::Type::boolean);
					std::get<json::Bool>(*this) = val;
				}
				else if constexpr (std::floating_point<VType>) {
					fEnsureType(json::Type::real);
					std::get<json::Real>(*this) = val;
				}
				else if constexpr (std::is_unsigned_v<VType>) {
					fEnsureType(json::Type::unumber);
					std::get<json::UNum>(*this) = val;
				}
				else if constexpr (std::is_signed_v<VType>) {
					fEnsureType(json::Type::inumber);
					std::get<json::INum>(*this) = val;
				}
				else
					fEnsureType(json::Type::null);
			}
		}
		constexpr void fEnsureType(json::Type t) {
			switch (t) {
			case json::Type::array:
				if (!std::holds_alternative<detail::ArrPtr>(*this))
					static_cast<detail::ValueParent&>(*this) = std::make_unique<json::Arr>();
				break;
			case json::Type::object:
				if (!std::holds_alternative<detail::ObjPtr>(*this))
					static_cast<detail::ValueParent&>(*this) = std::make_unique<json::Obj>();
				break;
			case json::Type::string:
				if (!std::holds_alternative<detail::StrPtr>(*this))
					static_cast<detail::ValueParent&>(*this) = std::make_unique<json::Str>();
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
		constexpr bool fConvertable(json::Type t) const {
			switch (t) {
			case json::Type::array:
				return std::holds_alternative<detail::ArrPtr>(*this);
			case json::Type::object:
				return std::holds_alternative<detail::ObjPtr>(*this);
			case json::Type::string:
				return std::holds_alternative<detail::StrPtr>(*this);
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
		constexpr bool isNull() const {
			return std::holds_alternative<json::Null>(*this);
		}
		constexpr bool isBoolean() const {
			return std::holds_alternative<json::Bool>(*this);
		}
		constexpr bool isStr() const {
			return std::holds_alternative<detail::StrPtr>(*this);
		}
		constexpr bool isUNum() const {
			if (std::holds_alternative<json::UNum>(*this))
				return true;
			return (std::holds_alternative<json::INum>(*this) && std::get<json::INum>(*this) >= 0);
		}
		constexpr bool isINum() const {
			return std::holds_alternative<json::UNum>(*this) || std::holds_alternative<json::INum>(*this);
		}
		constexpr bool isReal() const {
			return std::holds_alternative<json::UNum>(*this) || std::holds_alternative<json::INum>(*this) || std::holds_alternative<json::Real>(*this);
		}
		constexpr bool isObj() const {
			return std::holds_alternative<detail::ObjPtr>(*this);
		}
		constexpr bool isArr() const {
			return std::holds_alternative<detail::ArrPtr>(*this);
		}
		constexpr bool is(json::Type t) const {
			return fConvertable(t);
		}
		constexpr json::Type type() const {
			if (std::holds_alternative<json::Bool>(*this))
				return json::Type::boolean;
			if (std::holds_alternative<detail::StrPtr>(*this))
				return json::Type::string;
			if (std::holds_alternative<detail::ObjPtr>(*this))
				return json::Type::object;
			if (std::holds_alternative<detail::ArrPtr>(*this))
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
		constexpr json::Bool& boolean() {
			fEnsureType(json::Type::boolean);
			return std::get<json::Bool>(*this);
		}
		json::Str& str() {
			fEnsureType(json::Type::string);
			return *std::get<detail::StrPtr>(*this);
		}
		constexpr json::UNum& unum() {
			fEnsureType(json::Type::unumber);
			return std::get<json::UNum>(*this);
		}
		constexpr json::INum& inum() {
			fEnsureType(json::Type::inumber);
			return std::get<json::INum>(*this);
		}
		constexpr json::Real& real() {
			fEnsureType(json::Type::real);
			return std::get<json::Real>(*this);
		}
		json::Arr& arr() {
			fEnsureType(json::Type::array);
			return *std::get<detail::ArrPtr>(*this);
		}
		json::Obj& obj() {
			fEnsureType(json::Type::object);
			return *std::get<detail::ObjPtr>(*this);
		}

		constexpr json::Bool boolean() const {
			if (!std::holds_alternative<json::Bool>(*this))
				throw json::TypeException(L"json::Value is not a bool");
			return std::get<json::Bool>(*this);
		}
		const json::Str& str() const {
			if (!std::holds_alternative<detail::StrPtr>(*this))
				throw json::TypeException(L"json::Value is not a string");
			return *std::get<detail::StrPtr>(*this);
		}
		constexpr json::UNum unum() const {
			if (std::holds_alternative<json::INum>(*this) && std::get<json::INum>(*this) >= 0)
				return json::UNum(std::get<json::INum>(*this));
			if (std::holds_alternative<json::Real>(*this) && std::get<json::Real>(*this) >= 0)
				return json::UNum(std::get<json::Real>(*this));

			if (!std::holds_alternative<json::UNum>(*this))
				throw json::TypeException(L"json::Value is not an unsigned-number");
			return std::get<json::UNum>(*this);
		}
		constexpr json::INum inum() const {
			if (std::holds_alternative<json::UNum>(*this))
				return json::INum(std::get<json::UNum>(*this));
			if (std::holds_alternative<json::Real>(*this))
				return json::INum(std::get<json::Real>(*this));

			if (!std::holds_alternative<json::INum>(*this))
				throw json::TypeException(L"json::Value is not a signed-number");
			return std::get<json::INum>(*this);
		}
		constexpr json::Real real() const {
			if (std::holds_alternative<json::UNum>(*this))
				return json::Real(std::get<json::UNum>(*this));
			if (std::holds_alternative<json::INum>(*this))
				return json::Real(std::get<json::INum>(*this));

			if (!std::holds_alternative<json::Real>(*this))
				throw json::TypeException(L"json::Value is not a real");
			return std::get<json::Real>(*this);
		}
		const json::Arr& arr() const {
			if (!std::holds_alternative<detail::ArrPtr>(*this))
				throw json::TypeException(L"json::Value is not an array");
			return *std::get<detail::ArrPtr>(*this);
		}
		const json::Obj& obj() const {
			if (!std::holds_alternative<detail::ObjPtr>(*this))
				throw json::TypeException(L"json::Value is not an object");
			return *std::get<detail::ObjPtr>(*this);
		}

		/* operations shared between array/string/objects (depends on type, zero for non-container types) */
		constexpr size_t size() const {
			if (std::holds_alternative<detail::ArrPtr>(*this))
				return std::get<detail::ArrPtr>(*this)->size();
			if (std::holds_alternative<detail::ObjPtr>(*this))
				return std::get<detail::ObjPtr>(*this)->size();
			if (std::holds_alternative<detail::StrPtr>(*this))
				return std::get<detail::StrPtr>(*this)->size();
			return 0;
		}
		constexpr size_t size(json::Type t) const {
			if (t == json::Type::array && std::holds_alternative<detail::ArrPtr>(*this))
				return std::get<detail::ArrPtr>(*this)->size();
			else if (t == json::Type::object && std::holds_alternative<detail::ObjPtr>(*this))
				return std::get<detail::ObjPtr>(*this)->size();
			else if (t == json::Type::string && std::holds_alternative<detail::StrPtr>(*this))
				return std::get<detail::StrPtr>(*this)->size();
			return 0;
		}
		constexpr bool empty() const {
			if (std::holds_alternative<detail::ArrPtr>(*this))
				return std::get<detail::ArrPtr>(*this)->empty();
			if (std::holds_alternative<detail::ObjPtr>(*this))
				return std::get<detail::ObjPtr>(*this)->empty();
			if (std::holds_alternative<detail::StrPtr>(*this))
				return std::get<detail::StrPtr>(*this)->empty();
			return true;
		}
		constexpr bool empty(json::Type t) const {
			if (t == json::Type::array && std::holds_alternative<detail::ArrPtr>(*this))
				return std::get<detail::ArrPtr>(*this)->empty();
			else if (t == json::Type::object && std::holds_alternative<detail::ObjPtr>(*this))
				return std::get<detail::ObjPtr>(*this)->empty();
			else if (t == json::Type::string && std::holds_alternative<detail::StrPtr>(*this))
				return std::get<detail::StrPtr>(*this)->empty();
			return true;
		}

		const json::Value& operator[](const json::Str& k) const {
			return this->at(k);
		}
		json::Value& operator[](const json::Str& k) {
			return this->at(k);
		}
		const json::Value& at(const json::Str& k) const {
			static json::Value nullValue = json::Null();

			if (!std::holds_alternative<detail::ObjPtr>(*this))
				throw json::TypeException(L"json::Value is not a object");
			const json::Obj& obj = *std::get<detail::ObjPtr>(*this);
			auto it = obj.find(k);
			if (it == obj.end())
				return nullValue;
			return it->second;
		}
		json::Value& at(const json::Str& k) {
			fEnsureType(json::Type::object);
			return (*std::get<detail::ObjPtr>(*this))[k];
		}
		void erase(const json::Str& k) {
			fEnsureType(json::Type::object);
			std::get<detail::ObjPtr>(*this)->erase(k);
		}
		constexpr bool contains(const json::Str& k) const {
			if (!std::holds_alternative<detail::ObjPtr>(*this))
				return false;
			const json::Obj& obj = *std::get<detail::ObjPtr>(*this);
			return obj.find(k) != obj.end();
		}
		bool contains(const json::Str& k, json::Type t) const {
			if (!std::holds_alternative<detail::ObjPtr>(*this))
				return false;
			const json::Obj& obj = *std::get<detail::ObjPtr>(*this);
			auto it = obj.find(k);
			return (it != obj.end() && it->second.fConvertable(t));
		}
		constexpr bool typedObject(json::Type t) const {
			if (!std::holds_alternative<detail::ObjPtr>(*this))
				return false;
			const json::Obj& obj = *std::get<detail::ObjPtr>(*this);

			for (const auto& val : obj) {
				if (!val.second.fConvertable(t))
					return false;
			}
			return true;
		}

		const json::Value& operator[](size_t i) const {
			return this->at(i);
		}
		json::Value& operator[](size_t i) {
			return this->at(i);
		}
		const json::Value& at(size_t i) const {
			if (!std::holds_alternative<detail::ArrPtr>(*this))
				throw json::TypeException(L"json::Value is not a array");
			const json::Arr& arr = *std::get<detail::ArrPtr>(*this);
			if (i >= arr.size())
				throw json::RangeException(L"Array index out of range");
			return arr[i];
		}
		json::Value& at(size_t i) {
			fEnsureType(json::Type::array);
			json::Arr& arr = *std::get<detail::ArrPtr>(*this);
			if (i >= arr.size())
				throw json::RangeException(L"Array index out of range");
			return arr[i];
		}
		constexpr void push(const json::Value& v) {
			fEnsureType(json::Type::array);
			std::get<detail::ArrPtr>(*this)->push_back(v);
		}
		void pop() {
			fEnsureType(json::Type::array);
			json::Arr& arr = *std::get<detail::ArrPtr>(*this);
			if (!arr.empty())
				arr.pop_back();
		}
		constexpr void resize(size_t sz) {
			fEnsureType(json::Type::array);
			std::get<detail::ArrPtr>(*this)->resize(sz);
		}
		constexpr bool has(size_t i) const {
			if (!std::holds_alternative<detail::ArrPtr>(*this))
				return false;
			const json::Arr& arr = *std::get<detail::ArrPtr>(*this);
			return (i < arr.size());
		}
		constexpr bool has(size_t i, json::Type t) const {
			if (!std::holds_alternative<detail::ArrPtr>(*this))
				return false;
			const json::Arr& arr = *std::get<detail::ArrPtr>(*this);
			return (i < arr.size() && arr[i].fConvertable(t));
		}
		constexpr bool typedArray(json::Type t) const {
			if (!std::holds_alternative<detail::ArrPtr>(*this))
				return false;
			const json::Arr& arr = *std::get<detail::ArrPtr>(*this);

			for (size_t i = 0; i < arr.size(); ++i) {
				if (!arr[i].fConvertable(t))
					return false;
			}
			return true;
		}
	};
}
