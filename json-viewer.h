/* SPDX-License-Identifier: BSD-3-Clause */
/* Copyright (c) 2024-2025 Bjoern Boss Henrichsen */
#pragma once

#include "json-common.h"
#include "json-deserializer.h"
#include "json-value.h"

namespace json {
	class Viewer;
	class ArrViewer;
	class ObjViewer;

	namespace detail {
		struct StrViewObject {
			size_t offset = 0;
			size_t length = 0;
		};
		struct ArrViewObject {
			size_t offset = 0;
			size_t size = 0;
		};
		struct ObjViewObject {
			size_t offset = 0;
			size_t keysAndValues = 0;
		};

		using ViewEntry = std::variant<detail::StrViewObject, detail::ArrViewObject, detail::ObjViewObject, json::Null, json::UNum, json::INum, json::Real, json::Bool>;

		struct ViewState {
		public:
			std::vector<detail::ViewEntry> entries;
			json::Str strings;

		public:
			constexpr json::StrView str(size_t i) const {
				const detail::StrViewObject& value = std::get<detail::StrViewObject>(entries[i]);
				return json::StrView{ strings.data() + value.offset, value.length };
			}
		};

		template <class StreamType, str::CodeError Error>
		class ViewDeserializer {
		private:
			detail::Deserializer<StreamType, Error> pDeserializer;

		private:
			constexpr detail::ObjViewObject fObject(detail::ViewState& state) {
				detail::ObjViewObject out{};
				if (pDeserializer.checkIsEmpty(true))
					return out;
				std::vector<detail::ViewEntry> list;

				/* read the value and check if the end has been reached */
				do {
					/* read the key */
					detail::StrViewObject key = { state.strings.size(), 0 };
					pDeserializer.readString(state.strings, true);
					key.length = state.strings.size() - key.offset;
					list.emplace_back(key);

					/* read the corresponding value */
					list.emplace_back(fValue(state));
				} while (!pDeserializer.closeElseSeparator(true));

				/* update the actual array and write the state out */
				out.offset = state.entries.size();
				out.keysAndValues = list.size();
				state.entries.insert(state.entries.end(), list.begin(), list.end());
				return out;
			}
			constexpr detail::ArrViewObject fArray(detail::ViewState& state) {
				detail::ArrViewObject out{};
				if (pDeserializer.checkIsEmpty(false))
					return out;
				std::vector<detail::ViewEntry> list;

				/* read the value and check if the end has been reached */
				do {
					list.emplace_back(fValue(state));
				} while (!pDeserializer.closeElseSeparator(false));

				/* update the actual array and write the state out */
				out.offset = state.entries.size();
				out.size = list.size();
				state.entries.insert(state.entries.end(), list.begin(), list.end());
				return out;
			}
			constexpr detail::ViewEntry fValue(detail::ViewState& state) {
				switch (pDeserializer.peekOrOpenNext()) {
				case json::Type::string: {
					size_t start = state.strings.size();
					pDeserializer.readString(state.strings, false);
					return detail::StrViewObject{ start, state.strings.size() - start };
				}
				case json::Type::object:
					return fObject(state);
				case json::Type::array:
					return fArray(state);
				case json::Type::boolean:
					return pDeserializer.readBoolean();
				case json::Type::inumber:
				case json::Type::unumber:
				case json::Type::real: {
					detail::NumberValue value = pDeserializer.readNumber();
					if (std::holds_alternative<json::INum>(value))
						return std::get<json::INum>(value);
					else if (std::holds_alternative<json::UNum>(value))
						return std::get<json::UNum>(value);
					return std::get<json::Real>(value);
				}
				case json::Type::null:
				default:
					return pDeserializer.readNull();
				}
			}

		public:
			constexpr ViewDeserializer(auto&& stream, detail::ViewState& out) : pDeserializer{ std::forward<StreamType>(stream) } {
				out.entries.emplace_back();
				out.entries[0] = fValue(out);
				pDeserializer.checkDone();
			}
		};

		struct ViewAccess {
			static json::Viewer Make(const std::shared_ptr<detail::ViewState>& state, size_t index);
		};

		static constexpr bool ViewConvertible(json::Type t, const detail::ViewEntry& value) {
			switch (t) {
			case json::Type::array:
				return std::holds_alternative<detail::ArrViewObject>(value);
			case json::Type::object:
				return std::holds_alternative<detail::ObjViewObject>(value);
			case json::Type::string:
				return std::holds_alternative<detail::StrViewObject>(value);
			case json::Type::unumber:
				if (std::holds_alternative<json::UNum>(value))
					return true;
				if (std::holds_alternative<json::INum>(value) && std::get<json::INum>(value) >= 0)
					return true;
				return false;
			case json::Type::inumber:
				if (std::holds_alternative<json::INum>(value))
					return true;
				if (std::holds_alternative<json::UNum>(value))
					return true;
				return false;
			case json::Type::real:
				if (std::holds_alternative<json::Real>(value))
					return true;
				if (std::holds_alternative<json::INum>(value))
					return true;
				if (std::holds_alternative<json::UNum>(value))
					return true;
				return false;
			case json::Type::boolean:
				return std::holds_alternative<json::Bool>(value);
			default:
				return std::holds_alternative<json::Null>(value);
			}
		}

		static constexpr bool ViewObjLookup(json::StrView k, detail::ObjViewObject obj, size_t& lastKey, const std::shared_ptr<detail::ViewState>& state) {
			/* check if the last search matches the key */
			if (lastKey < obj.keysAndValues && state->str(obj.offset + lastKey) == k)
				return true;

			/* iterate over the values and look for the key */
			for (lastKey = 0; lastKey < obj.keysAndValues; lastKey += 2) {
				if (state->str(obj.offset + lastKey) == k)
					return true;
			}
			return false;
		}
	}

	/* [json::IsJson] json-view of type [value], which can be used to read the current value
	*	- missing object-keys will return a constant json::Null
	*	- caches string key lookups for faster multi-accessing
	*	Note: This is a light-weight object, which can just be copied around, as it keeps a reference to the actual state */
	class Viewer : private detail::ViewEntry {
		friend struct detail::ViewAccess;
	private:
		std::shared_ptr<detail::ViewState> pState;
		mutable size_t pLastKey = 0;

	public:
		constexpr Viewer() : detail::ViewEntry{ json::Null() } {}
		Viewer(json::Viewer&&) = default;
		Viewer(const json::Viewer&) = default;
		json::Viewer& operator=(json::Viewer&&) = default;
		json::Viewer& operator=(const json::Viewer&) = default;

	private:
		Viewer(const std::shared_ptr<detail::ViewState>& state, size_t index) : pState{ state }, detail::ViewEntry{ state->entries[index] } {
			if (std::holds_alternative<detail::ObjViewObject>(*this))
				pLastKey = std::get<detail::ObjViewObject>(*this).keysAndValues;
		}

	public:
		constexpr bool isNull() const {
			return std::holds_alternative<json::Null>(*this);
		}
		constexpr bool isBoolean() const {
			return std::holds_alternative<json::Bool>(*this);
		}
		constexpr bool isStr() const {
			return std::holds_alternative<detail::StrViewObject>(*this);
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
			return std::holds_alternative<detail::ObjViewObject>(*this);
		}
		constexpr bool isArr() const {
			return std::holds_alternative<detail::ArrViewObject>(*this);
		}
		constexpr bool is(json::Type t) const {
			return detail::ViewConvertible(t, *this);
		}
		constexpr json::Type type() const {
			if (std::holds_alternative<json::Bool>(*this))
				return json::Type::boolean;
			if (std::holds_alternative<detail::StrViewObject>(*this))
				return json::Type::string;
			if (std::holds_alternative<detail::ObjViewObject>(*this))
				return json::Type::object;
			if (std::holds_alternative<detail::ArrViewObject>(*this))
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
		constexpr json::Bool boolean() const {
			if (!std::holds_alternative<json::Bool>(*this))
				throw json::TypeException(L"json::Viewer is not a bool");
			return std::get<json::Bool>(*this);
		}
		constexpr json::StrView str() const {
			if (!std::holds_alternative<detail::StrViewObject>(*this))
				throw json::TypeException(L"json::Viewer is not a string");

			detail::StrViewObject str = std::get<detail::StrViewObject>(*this);
			return json::StrView{ pState->strings.data() + str.offset, str.length };
		}
		constexpr json::UNum unum() const {
			if (std::holds_alternative<json::INum>(*this) && std::get<json::INum>(*this) >= 0)
				return json::UNum(std::get<json::INum>(*this));
			if (std::holds_alternative<json::Real>(*this) && std::get<json::Real>(*this) >= 0)
				return json::UNum(std::get<json::Real>(*this));

			if (!std::holds_alternative<json::UNum>(*this))
				throw json::TypeException(L"json::Viewer is not an unsigned-number");
			return std::get<json::UNum>(*this);
		}
		constexpr json::INum inum() const {
			if (std::holds_alternative<json::UNum>(*this))
				return json::INum(std::get<json::UNum>(*this));
			if (std::holds_alternative<json::Real>(*this))
				return json::INum(std::get<json::Real>(*this));

			if (!std::holds_alternative<json::INum>(*this))
				throw json::TypeException(L"json::Viewer is not a signed-number");
			return std::get<json::INum>(*this);
		}
		constexpr json::Real real() const {
			if (std::holds_alternative<json::UNum>(*this))
				return json::Real(std::get<json::UNum>(*this));
			if (std::holds_alternative<json::INum>(*this))
				return json::Real(std::get<json::INum>(*this));

			if (!std::holds_alternative<json::Real>(*this))
				throw json::TypeException(L"json::Viewer is not a real");
			return std::get<json::Real>(*this);
		}
		json::ArrViewer arr() const;
		json::ObjViewer obj() const;

	public:
		/* construct a json::Value from this object */
		inline json::Value value() const;

	public:
		/* operations shared between array/string/objects (depends on type, zero for non-container types) */
		constexpr size_t size() const {
			if (std::holds_alternative<detail::ArrViewObject>(*this))
				return std::get<detail::ArrViewObject>(*this).size;
			if (std::holds_alternative<detail::ObjViewObject>(*this))
				return std::get<detail::ObjViewObject>(*this).keysAndValues / 2;
			if (std::holds_alternative<detail::StrViewObject>(*this))
				return std::get<detail::StrViewObject>(*this).length;
			return 0;
		}
		constexpr size_t size(json::Type t) const {
			if (t == json::Type::array && std::holds_alternative<detail::ArrViewObject>(*this))
				return std::get<detail::ArrViewObject>(*this).size;
			else if (t == json::Type::object && std::holds_alternative<detail::ObjViewObject>(*this))
				return std::get<detail::ObjViewObject>(*this).keysAndValues / 2;
			else if (t == json::Type::string && std::holds_alternative<detail::StrViewObject>(*this))
				return std::get<detail::StrViewObject>(*this).length;
			return 0;
		}
		constexpr bool empty() const {
			if (std::holds_alternative<detail::ArrViewObject>(*this))
				return (std::get<detail::ArrViewObject>(*this).size == 0);
			if (std::holds_alternative<detail::ObjViewObject>(*this))
				return (std::get<detail::ObjViewObject>(*this).keysAndValues == 0);
			if (std::holds_alternative<detail::StrViewObject>(*this))
				return (std::get<detail::StrViewObject>(*this).length == 0);
			return true;
		}
		constexpr bool empty(json::Type t) const {
			if (t == json::Type::array && std::holds_alternative<detail::ArrViewObject>(*this))
				return (std::get<detail::ArrViewObject>(*this).size == 0);
			else if (t == json::Type::object && std::holds_alternative<detail::ObjViewObject>(*this))
				return (std::get<detail::ObjViewObject>(*this).keysAndValues == 0);
			else if (t == json::Type::string && std::holds_alternative<detail::StrViewObject>(*this))
				return (std::get<detail::StrViewObject>(*this).length == 0);
			return true;
		}

		json::Viewer operator[](json::StrView k) const {
			return this->at(k);
		}
		json::Viewer at(json::StrView k) const {
			static json::Viewer nullValue{};

			if (!std::holds_alternative<detail::ObjViewObject>(*this))
				throw json::TypeException(L"json::Viewer is not a object");
			detail::ObjViewObject obj = std::get<detail::ObjViewObject>(*this);

			if (detail::ViewObjLookup(k, obj, pLastKey, pState))
				return json::Viewer{ pState, obj.offset + pLastKey + 1 };
			return nullValue;
		}
		constexpr bool contains(json::StrView k) const {
			if (!std::holds_alternative<detail::ObjViewObject>(*this))
				return false;
			detail::ObjViewObject obj = std::get<detail::ObjViewObject>(*this);

			return detail::ViewObjLookup(k, obj, pLastKey, pState);
		}
		constexpr bool contains(json::StrView k, json::Type t) const {
			if (!std::holds_alternative<detail::ObjViewObject>(*this))
				return false;
			detail::ObjViewObject obj = std::get<detail::ObjViewObject>(*this);

			if (detail::ViewObjLookup(k, obj, pLastKey, pState))
				return detail::ViewConvertible(t, pState->entries[obj.offset + pLastKey + 1]);
			return false;
		}
		constexpr bool typedObject(json::Type t) const {
			if (!std::holds_alternative<detail::ObjViewObject>(*this))
				return false;
			detail::ObjViewObject obj = std::get<detail::ObjViewObject>(*this);

			for (size_t i = 0; i < obj.keysAndValues; i += 2) {
				if (!detail::ViewConvertible(t, pState->entries[obj.offset + i + 1]))
					return false;
			}
			return true;
		}

		json::Viewer operator[](size_t i) const {
			return this->at(i);
		}
		json::Viewer at(size_t i) const {
			if (!std::holds_alternative<detail::ArrViewObject>(*this))
				throw json::TypeException(L"json::Viewer is not a array");
			detail::ArrViewObject arr = std::get<detail::ArrViewObject>(*this);

			if (i >= arr.size)
				throw json::RangeException(L"Array index out of range");
			return json::Viewer{ pState, arr.offset + i };
		}
		constexpr bool has(size_t i) const {
			if (!std::holds_alternative<detail::ArrViewObject>(*this))
				return false;
			detail::ArrViewObject arr = std::get<detail::ArrViewObject>(*this);

			return (i < arr.size);
		}
		constexpr bool has(size_t i, json::Type t) const {
			if (!std::holds_alternative<detail::ArrViewObject>(*this))
				return false;
			detail::ArrViewObject arr = std::get<detail::ArrViewObject>(*this);

			return (i < arr.size && detail::ViewConvertible(t, pState->entries[arr.offset + i]));
		}
		constexpr bool typedArray(json::Type t) const {
			if (!std::holds_alternative<detail::ArrViewObject>(*this))
				return false;
			detail::ArrViewObject arr = std::get<detail::ArrViewObject>(*this);

			for (size_t i = 0; i < arr.size; ++i) {
				if (!detail::ViewConvertible(t, pState->entries[arr.offset + i]))
					return false;
			}
			return true;
		}
	};

	/* [json::IsJson] json-view of type [array], which can be used to read the corresponding array value
	*	Note: This is a light-weight object, which can just be copied around, as it keeps a reference to the actual state */
	class ArrViewer {
		friend class json::Viewer;
	public:
		struct iterator {
			friend class json::ArrViewer;
		public:
			using iterator_category = std::bidirectional_iterator_tag;
			using value_type = const json::Viewer;
			using difference_type = std::ptrdiff_t;
			using pointer = value_type*;
			using reference = value_type&;

		private:
			std::shared_ptr<detail::ViewState> pState;
			size_t pIndex = 0;
			mutable json::Viewer pTemp;
			mutable bool pSet = false;

		public:
			constexpr iterator() = default;

		private:
			iterator(const std::shared_ptr<detail::ViewState>& state, size_t index) : pState{ state }, pIndex{ index } {}

		private:
			reference fGet() const {
				if (!pSet)
					pTemp = detail::ViewAccess::Make(pState, pIndex);
				pSet = true;
				return pTemp;
			}

		public:
			reference operator*() const {
				return fGet();
			}
			pointer operator->() const {
				return &fGet();
			}
			constexpr iterator& operator++() {
				++pIndex;
				pSet = false;
				return *this;
			}
			constexpr iterator& operator--() {
				--pIndex;
				pSet = false;
				return *this;
			}
			iterator operator++(int) {
				return iterator{ pState, pIndex + 1 };
			}
			iterator operator--(int) {
				return iterator{ pState, pIndex - 1 };
			}
			constexpr bool operator==(const iterator& it) const {
				return (pIndex == it.pIndex);
			}
			constexpr bool operator!=(const iterator& it) const {
				return !(*this == it);
			}
		};
		using const_iterator = iterator;

	private:
		std::shared_ptr<detail::ViewState> pState;
		detail::ArrViewObject pSelf;

	public:
		constexpr ArrViewer() = delete;
		ArrViewer(json::ArrViewer&&) = default;
		ArrViewer(const json::ArrViewer&) = default;
		json::ArrViewer& operator=(json::ArrViewer&&) = default;
		json::ArrViewer& operator=(const json::ArrViewer&) = default;

	private:
		ArrViewer(const std::shared_ptr<detail::ViewState>& state, const detail::ArrViewObject& self) : pState{ state }, pSelf{ self } {}

	public:
		json::Viewer operator[](size_t index) const {
			return this->at(index);
		}

	public:
		iterator begin() const {
			return iterator{ pState, pSelf.offset };
		}
		iterator end() const {
			return iterator{ pState, pSelf.offset + pSelf.size };
		}
		constexpr size_t size() const {
			return pSelf.size;
		}
		constexpr bool empty() const {
			return (pSelf.size == 0);
		}
		constexpr bool has(size_t i) const {
			return (i < pSelf.size);
		}
		constexpr bool has(size_t i, json::Type t) const {
			return (i < pSelf.size && detail::ViewConvertible(t, pState->entries[pSelf.offset + i]));
		}
		constexpr bool typedArray(json::Type t) const {
			for (size_t i = 0; i < pSelf.size; ++i) {
				if (!detail::ViewConvertible(t, pState->entries[pSelf.offset + i]))
					return false;
			}
			return true;
		}
		json::Viewer at(size_t index) const {
			if (index >= pSelf.size)
				throw json::RangeException(L"Array index out of range");
			return detail::ViewAccess::Make(pState, pSelf.offset + index);
		}
	};

	/* [json::IsJson] json-view of type [object], which can be used to read the corresponding object value
	*	- missing object-keys will return a constant json::Null
	*	- caches string key lookups for faster multi-accessing
	*	Note: This is a light-weight object, which can just be copied around, as it keeps a reference to the actual state */
	class ObjViewer {
		friend class json::Viewer;
	public:
		struct iterator {
			friend class json::ObjViewer;
		public:
			using iterator_category = std::bidirectional_iterator_tag;
			using value_type = const std::pair<json::StrView, json::Viewer>;
			using difference_type = std::ptrdiff_t;
			using pointer = value_type*;
			using reference = value_type&;

		private:
			std::shared_ptr<detail::ViewState> pState;
			size_t pIndex = 0;
			mutable std::pair<json::StrView, json::Viewer> pTemp;
			mutable bool pSet = false;

		public:
			constexpr iterator() = default;

		private:
			iterator(const std::shared_ptr<detail::ViewState>& state, size_t index) : pState{ state }, pIndex{ index } {}

		private:
			reference fGet() const {
				if (!pSet)
					pTemp = { pState->str(pIndex), detail::ViewAccess::Make(pState, pIndex + 1) };
				pSet = true;
				return pTemp;
			}

		public:
			reference operator*() const {
				return fGet();
			}
			pointer operator->() const {
				return &fGet();
			}
			constexpr iterator& operator++() {
				pIndex += 2;
				pSet = false;
				return *this;
			}
			constexpr iterator& operator--() {
				pIndex -= 2;
				pSet = false;
				return *this;
			}
			iterator operator++(int) {
				return iterator{ pState, pIndex + 2 };
			}
			iterator operator--(int) {
				return iterator{ pState, pIndex - 2 };
			}
			constexpr bool operator==(const iterator& it) const {
				return (pIndex == it.pIndex);
			}
			constexpr bool operator!=(const iterator& it) const {
				return !(*this == it);
			}
		};
		using const_iterator = iterator;

	private:
		std::shared_ptr<detail::ViewState> pState;
		detail::ObjViewObject pSelf;
		mutable size_t pLastKey = 0;

	public:
		constexpr ObjViewer() = delete;
		ObjViewer(json::ObjViewer&&) = default;
		ObjViewer(const json::ObjViewer&) = default;
		json::ObjViewer& operator=(json::ObjViewer&&) = default;
		json::ObjViewer& operator=(const json::ObjViewer&) = default;

	private:
		ObjViewer(const std::shared_ptr<detail::ViewState>& state, const detail::ObjViewObject& self) : pState{ state }, pSelf{ self } {
			pLastKey = pSelf.keysAndValues;
		}

	public:
		json::Viewer operator[](json::StrView k) const {
			return this->at(k);
		}

	public:
		iterator begin() const {
			return iterator{ pState, pSelf.offset };
		}
		iterator end() const {
			return iterator{ pState, pSelf.offset + pSelf.keysAndValues };
		}
		iterator find(json::StrView k) const {
			if (!detail::ViewObjLookup(k, pSelf, pLastKey, pState))
				return iterator{ pState, pSelf.offset + pSelf.keysAndValues };
			return iterator{ pState, pSelf.offset + pLastKey };
		}
		constexpr size_t size() const {
			return (pSelf.keysAndValues / 2);
		}
		constexpr bool empty() const {
			return (pSelf.keysAndValues == 0);
		}
		constexpr bool contains(json::StrView k) const {
			return detail::ViewObjLookup(k, pSelf, pLastKey, pState);
		}
		constexpr bool contains(json::StrView k, json::Type t) const {
			if (!detail::ViewObjLookup(k, pSelf, pLastKey, pState))
				return false;
			return detail::ViewConvertible(t, pState->entries[pSelf.offset + pLastKey + 1]);
		}
		constexpr bool typedObject(json::Type t) const {
			for (size_t i = 0; i < pSelf.keysAndValues; i += 2) {
				if (!detail::ViewConvertible(t, pState->entries[pSelf.offset + i + 1]))
					return false;
			}
			return true;
		}
		json::Viewer at(json::StrView k) const {
			static json::Viewer nullValue{};
			if (detail::ViewObjLookup(k, pSelf, pLastKey, pState))
				return detail::ViewAccess::Make(pState, pSelf.offset + pLastKey + 1);
			return nullValue;
		}
	};

	inline json::ArrViewer json::Viewer::arr() const {
		if (!std::holds_alternative<detail::ArrViewObject>(*this))
			throw json::TypeException(L"json::Viewer is not an array");
		return json::ArrViewer{ pState, std::get<detail::ArrViewObject>(*this) };
	}
	inline json::ObjViewer json::Viewer::obj() const {
		if (!std::holds_alternative<detail::ObjViewObject>(*this))
			throw json::TypeException(L"json::Viewer is not an object");
		return json::ObjViewer{ pState, std::get<detail::ObjViewObject>(*this) };
	}
	inline json::Value json::Viewer::value() const {
		return json::Value{ *this };
	}
	inline json::Viewer json::detail::ViewAccess::Make(const std::shared_ptr<detail::ViewState>& state, size_t index) {
		return json::Viewer{ state, index };
	}

	/* construct a json value-viewer from the given stream and ensure that the entire stream is a single valid json-value
	*	- interprets \u escape-sequences as utf-16 encoding
	*	- expects entire stream to be a single json value until the end with optional whitespace padding
	*	- for objects with multiple identical keys, all occurring keys/values will be accessible, but the first will be returned upon accesses */
	template <str::CodeError Error = str::CodeError::replace>
	json::Viewer View(str::IsStream auto&& stream) {
		using StreamType = decltype(stream);

		std::shared_ptr<detail::ViewState> state = std::make_shared<detail::ViewState>();
		detail::ViewDeserializer<std::remove_reference_t<StreamType>, Error> _deserializer{ std::forward<StreamType>(stream), *state.get() };
		return detail::ViewAccess::Make(state, 0);
	}
}
