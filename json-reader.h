/* SPDX-License-Identifier: BSD-3-Clause */
/* Copyright (c) 2024-2025 Bjoern Boss Henrichsen */
#pragma once

#include "json-common.h"
#include "json-deserializer.h"
#include "json-value.h"

namespace json {
	namespace detail {
		struct ReadAnyType {};
	}

	/* check if the given type is a valid reader-stream */
	template <class Type>
	concept IsReadType = str::IsStream<Type> || std::is_same_v<Type, detail::ReadAnyType>;

	template <json::IsReadType StreamType, str::CodeError Error = str::CodeError::replace>
	class Reader;
	template <json::IsReadType StreamType, str::CodeError Error = str::CodeError::replace>
	class ArrReader;
	template <json::IsReadType StreamType, str::CodeError Error = str::CodeError::replace>
	class ObjReader;

	namespace detail {
		template <class StreamType, str::CodeError Error>
		class ReaderState;

		using StrReader = std::shared_ptr<json::Str>;

		template <class StreamType, str::CodeError Error>
		struct ArrReference {
			std::shared_ptr<detail::ReaderState<StreamType, Error>> state;
			size_t stamp = 0;
		};

		template <class StreamType, str::CodeError Error>
		struct ObjReference {
			std::shared_ptr<detail::ReaderState<StreamType, Error>> state;
			size_t stamp = 0;
		};

		/* json-null first to default-construct as null */
		template <class StreamType, str::CodeError Error>
		using ReaderParent = std::variant<json::NullType, json::UNum, json::INum, json::Real, json::Bool, detail::StrReader, detail::ArrReference<StreamType, Error>, detail::ObjReference<StreamType, Error>>;

		template <class StreamType, str::CodeError Error>
		class ReaderState {
		public:
			struct Instance {
				/* explicit construction necessary, as default-constructor is private and cannot be accessed by std::pair */
				std::pair<json::Str, json::Reader<StreamType, Error>> value = { L"", {} };
				std::unique_ptr<Instance> self;
				bool object = false;
				bool opened = false;
			};

		private:
			using ActStream = std::conditional_t<std::is_same_v<StreamType, detail::ReadAnyType>, std::unique_ptr<str::InheritStream>, StreamType>;

		private:
			detail::Deserializer<ActStream, Error> pDeserializer;
			std::vector<Instance*> pActive;
			size_t pNextStamp = 0;

		public:
			template <class Type>
			constexpr ReaderState(Type&& stream) : pDeserializer{ std::forward<Type>(stream) } {}
			constexpr ~ReaderState() {
				/* close all opened objects (will ensure the entire json is parsed properly) */
				while (!pActive.empty())
					while (fReadNextValue(nullptr)) {}
			}

		private:
			json::Reader<StreamType, Error> fValue(const std::shared_ptr<detail::ReaderState<StreamType, Error>>& _this) {
				switch (pDeserializer.peekOrOpenNext()) {
				case json::Type::unumber:
				case json::Type::inumber:
				case json::Type::real: {
					detail::NumberValue num = pDeserializer.readNumber();
					if (std::holds_alternative<json::INum>(num))
						return json::Reader<StreamType, Error>{ detail::ReaderParent<StreamType, Error>{ std::get<json::INum>(num) } };
					if (std::holds_alternative<json::UNum>(num))
						return json::Reader<StreamType, Error>{ detail::ReaderParent<StreamType, Error>{ std::get<json::UNum>(num) } };
					return json::Reader<StreamType, Error>{ detail::ReaderParent<StreamType, Error>{ std::get<json::Real>(num) } };
				}
				case json::Type::boolean:
					return json::Reader<StreamType, Error>{ detail::ReaderParent<StreamType, Error>{ pDeserializer.readBoolean() } };
				case json::Type::string: {
					detail::StrReader str = std::make_shared<json::Str>();
					pDeserializer.readString(*str, false);
					return json::Reader<StreamType, Error>{ detail::ReaderParent<StreamType, Error>{ str } };
				}
				case json::Type::array: {
					std::unique_ptr<Instance> inst = std::make_unique<Instance>();
					pActive.push_back(inst.get());
					inst->object = false;
					inst->self = std::move(inst);
					return json::Reader<StreamType, Error>{ detail::ReaderParent<StreamType, Error>{ detail::ArrReference<StreamType, Error>{ _this, ++pNextStamp } } };
				}
				case json::Type::object: {
					std::unique_ptr<Instance> inst = std::make_unique<Instance>();
					pActive.push_back(inst.get());
					inst->object = true;
					inst->self = std::move(inst);
					return json::Reader<StreamType, Error>{ detail::ReaderParent<StreamType, Error>{ detail::ObjReference<StreamType, Error>{ _this, ++pNextStamp } } };
				}
				case json::Type::null:
				default:
					return json::Reader<StreamType, Error>{ detail::ReaderParent<StreamType, Error>{ pDeserializer.readNull() } };
				}
			}
			constexpr bool fReadNextValue(const std::shared_ptr<detail::ReaderState<StreamType, Error>>& _this) {
				/* cache the instance-pointer as reading the next value might push to the active-stack */
				Instance* inst = pActive.back();

				/* check if this is the first value being read, in which case no separator is required,
				*	or if no value is found anymore, in which case the instance can be released */
				if (inst->opened ? pDeserializer.closeElseSeparator(inst->object) : pDeserializer.checkIsEmpty(inst->object)) {
					inst->self.reset();
					pActive.pop_back();

					/* mark the active-state as changed and check if the end has been reached and a valid json-end has been found */
					++pNextStamp;
					if (pActive.empty())
						pDeserializer.checkDone();
					return false;
				}

				/* mark the next value as not being the first anymore */
				inst->opened = true;

				/* check if a key needs to be read */
				if (inst->object) {
					inst->value.first.clear();
					pDeserializer.readString(inst->value.first, true);
				}

				/* read the next value */
				inst->value.second = fValue(_this);
				return true;
			}

		public:
			constexpr bool next(const std::shared_ptr<detail::ReaderState<StreamType, Error>>& _this, Instance* instance) {
				/* check if the instance is still in the active stack */
				size_t index = pActive.size();
				while (index > 0 && pActive[index - 1] != instance)
					--index;
				if (index == 0)
					throw json::ReaderException(L"Reader is not in an active state");

				/* close all other open objects until the current object has been reached */
				while (pActive.size() > index) {
					while (fReadNextValue(_this)) {}
				}

				/* check if another value is encountered and read it */
				return fReadNextValue(_this);
			}
			constexpr json::Reader<StreamType, Error> initValue(const std::shared_ptr<detail::ReaderState<StreamType, Error>>& _this) {
				json::Reader<StreamType, Error> value = fValue(_this);

				/* check if this is not an opened value and should therefore have consumed the entire source */
				if (pActive.empty())
					pDeserializer.checkDone();
				return value;
			}
			constexpr std::unique_ptr<Instance> open(size_t stamp, bool object) {
				/* check if the object is the next in line and has not yet been fetched */
				if (stamp != pNextStamp || pActive.back()->self.get() == 0)
					throw json::ReaderException(object ? L"Object has already been opened for reading" : L"Array has already been opened for reading");

				/* fetch the instance-pointer */
				std::unique_ptr<Instance> inst;
				pActive.back()->self.swap(inst);
				return std::move(inst);
			}
			constexpr void close(const std::shared_ptr<detail::ReaderState<StreamType, Error>>& _this, Instance* instance) {
				/* check if the instance is still in the active stack */
				size_t index = pActive.size();
				while (index > 0 && pActive[index - 1] != instance)
					--index;
				if (index == 0)
					return;

				/* close all other open objects including this object */
				while (pActive.size() >= index) {
					while (fReadNextValue(_this)) {}
				}
			}
		};
	}

	/* [json::IsJson] json-reader of type [value], which can be used to read the current value (array or object-readers can only be opened once)
	*	Note: This is a light-weight object, which can just be copied around, as it keeps a reference to the actual state */
	template <json::IsReadType StreamType, str::CodeError Error>
	class Reader : private detail::ReaderParent<StreamType, Error> {
		friend class detail::ReaderState<StreamType, Error>;
	public:
		constexpr Reader(json::Reader<StreamType, Error>&&) = default;
		constexpr Reader(const json::Reader<StreamType, Error>&) = default;
		constexpr json::Reader<StreamType, Error>& operator=(json::Reader<StreamType, Error>&&) = default;
		constexpr json::Reader<StreamType, Error>& operator=(const json::Reader<StreamType, Error>&) = default;

	private:
		constexpr Reader() : detail::ReaderParent<StreamType, Error>{ json::Null } {}
		constexpr Reader(const detail::ReaderParent<StreamType, Error>& v) : detail::ReaderParent<StreamType, Error>{ v } {}

	public:
		constexpr bool isNull() const {
			return std::holds_alternative<json::NullType>(*this);
		}
		constexpr bool isBoolean() const {
			return std::holds_alternative<json::Bool>(*this);
		}
		constexpr bool isStr() const {
			return std::holds_alternative<detail::StrReader>(*this);
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
			return std::holds_alternative<detail::ObjReference<StreamType, Error>>(*this);
		}
		constexpr bool isArr() const {
			return std::holds_alternative<detail::ArrReference<StreamType, Error>>(*this);
		}
		constexpr bool is(json::Type t) const {
			switch (t) {
			case json::Type::array:
				return std::holds_alternative<detail::ArrReference<StreamType, Error>>(*this);
			case json::Type::object:
				return std::holds_alternative<detail::ObjReference<StreamType, Error>>(*this);
			case json::Type::string:
				return std::holds_alternative<detail::StrReader>(*this);
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
				return std::holds_alternative<json::NullType>(*this);
			}
		}
		constexpr json::Type type() const {
			if (std::holds_alternative<json::Bool>(*this))
				return json::Type::boolean;
			if (std::holds_alternative<detail::StrReader>(*this))
				return json::Type::string;
			if (std::holds_alternative<detail::ObjReference<StreamType, Error>>(*this))
				return json::Type::object;
			if (std::holds_alternative<detail::ArrReference<StreamType, Error>>(*this))
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
				throw json::TypeException(L"json::Reader is not a bool");
			return std::get<json::Bool>(*this);
		}
		constexpr const json::Str& str() const {
			if (!std::holds_alternative<detail::StrReader>(*this))
				throw json::TypeException(L"json::Reader is not a string");
			return *std::get<detail::StrReader>(*this);
		}
		constexpr json::UNum unum() const {
			if (std::holds_alternative<json::INum>(*this) && std::get<json::INum>(*this) >= 0)
				return json::UNum(std::get<json::INum>(*this));
			if (std::holds_alternative<json::Real>(*this) && std::get<json::Real>(*this) >= 0)
				return json::UNum(std::get<json::Real>(*this));

			if (!std::holds_alternative<json::UNum>(*this))
				throw json::TypeException(L"json::Reader is not an unsigned-number");
			return std::get<json::UNum>(*this);
		}
		constexpr json::INum inum() const {
			if (std::holds_alternative<json::UNum>(*this))
				return json::INum(std::get<json::UNum>(*this));
			if (std::holds_alternative<json::Real>(*this))
				return json::INum(std::get<json::Real>(*this));

			if (!std::holds_alternative<json::INum>(*this))
				throw json::TypeException(L"json::Reader is not a signed-number");
			return std::get<json::INum>(*this);
		}
		constexpr json::Real real() const {
			if (std::holds_alternative<json::UNum>(*this))
				return json::Real(std::get<json::UNum>(*this));
			if (std::holds_alternative<json::INum>(*this))
				return json::Real(std::get<json::INum>(*this));

			if (!std::holds_alternative<json::Real>(*this))
				throw json::TypeException(L"json::Reader is not a real");
			return std::get<json::Real>(*this);
		}
		constexpr json::ArrReader<StreamType, Error> arr() const {
			if (!std::holds_alternative<detail::ArrReference<StreamType, Error>>(*this))
				throw json::TypeException(L"json::Reader is not an array");
			const detail::ArrReference<StreamType, Error>& arr = std::get<detail::ArrReference<StreamType, Error>>(*this);
			return json::ArrReader<StreamType, Error>{ arr.state, std::move(arr.state->open(arr.stamp, false)) };
		}
		constexpr json::ObjReader<StreamType, Error> obj() const {
			if (!std::holds_alternative<detail::ObjReference<StreamType, Error>>(*this))
				throw json::TypeException(L"json::Reader is not an object");
			const detail::ObjReference<StreamType, Error>& arr = std::get<detail::ObjReference<StreamType, Error>>(*this);
			return json::ObjReader<StreamType, Error>{ arr.state, std::move(arr.state->open(arr.stamp, true)) };
		}

	public:
		/* construct a json::Value from this object */
		json::Value value() const {
			return json::Value{ *this };
		}
	};

	/* [json::IsJson] json-reader of type [array], which can be used to read the corresponding array value
	*	(values can only be read once, and any unread child-values will be discarded upon reading the next value)
	*	Note: Although this is a light-weight object, it can only be moved around, as it references the current progress of the reading */
	template <json::IsReadType StreamType, str::CodeError Error>
	class ArrReader {
		friend class json::Reader<StreamType, Error>;
	public:
		struct iterator {
			friend class json::ArrReader<StreamType, Error>;
		public:
			using iterator_category = std::forward_iterator_tag;
			using value_type = const json::Reader<StreamType, Error>;
			using difference_type = std::ptrdiff_t;
			using pointer = value_type*;
			using reference = value_type&;

		private:
			const ArrReader<StreamType, Error>* pSelf = 0;
			bool pEnd = false;

		public:
			constexpr iterator() = default;

		private:
			constexpr iterator(const ArrReader<StreamType, Error>& self, bool end = false) : pSelf{ &self }, pEnd{ end } {}

		public:
			constexpr reference operator*() const {
				return pSelf->get();
			}
			constexpr pointer operator->() const {
				return &pSelf->get();
			}
			constexpr iterator& operator++() {
				pSelf->next();
				return *this;
			}
			constexpr iterator operator++(int) {
				pSelf->next();
				return *this;
			}
			constexpr bool operator==(const iterator& it) const {
				if (pSelf == 0 || it.pSelf == 0)
					return (pSelf == it.pSelf);
				return (pSelf->pInstance == it.pSelf->pInstance && (pEnd == it.pEnd || pSelf->closed()));
			}
			constexpr bool operator!=(const iterator& it) const {
				return !(*this == it);
			}
		};
		using const_iterator = iterator;

	private:
		mutable std::shared_ptr<detail::ReaderState<StreamType, Error>> pState;
		std::unique_ptr<typename detail::ReaderState<StreamType, Error>::Instance> pInstance;

	public:
		constexpr ArrReader() = delete;
		constexpr ArrReader(json::ArrReader<StreamType, Error>&&) = default;
		constexpr ArrReader(const json::ArrReader<StreamType, Error>&) = delete;
		constexpr json::ArrReader<StreamType, Error>& operator=(json::ArrReader<StreamType, Error>&&) = default;
		constexpr json::ArrReader<StreamType, Error>& operator=(const json::ArrReader<StreamType, Error>&) = delete;
		constexpr ~ArrReader() {
			if (pState.get() != 0)
				pState->close(pState, pInstance.get());
		}

	private:
		constexpr bool fNext() const {
			if (pState->next(pState, pInstance.get()))
				return true;
			pState.reset();
			return false;
		}

	private:
		constexpr ArrReader(const std::shared_ptr<detail::ReaderState<StreamType, Error>>& state, std::unique_ptr<typename detail::ReaderState<StreamType, Error>::Instance>&& inst) : pState{ state }, pInstance{ std::move(inst) } {
			fNext();
		}

	public:
		/* fetch an iterator to this array (advancing the iterator is equivalent to calling next() on this object) */
		constexpr iterator begin() const {
			return iterator{ *this, false };
		}

		/* fetch the end iterator to this array (will be equal to any other iterator of this object when closed() return true) */
		constexpr iterator end() const {
			return iterator{ *this, true };
		}

		/* check if a next value exists in the array and prepare it for reading */
		constexpr bool next() const {
			return fNext();
		}

		/* check if the end has been reached and no more valid value can therefore be read */
		constexpr bool closed() const {
			return (pState.get() == 0);
		}

		/* close the current object and thereby skip to the end of this array */
		constexpr void close() const {
			if (pState.get() != 0)
				pState->close(pState, pInstance.get());
			pState.reset();
		}

		/* read the current value (only if the reader is not marked as closed()) */
		constexpr const json::Reader<StreamType, Error>& get() const {
			return pInstance->value.second;
		}
	};

	/* [json::IsJson] json-reader of type [object], which can be used to read the corresponding object values
	*	(values can only be read once, and any unread child-values will be discarded upon reading the next value)
	*	Note: Although this is a light-weight object, it can only be moved around, as it references the current progress of the reading */
	template <json::IsReadType StreamType, str::CodeError Error>
	class ObjReader {
		friend class json::Reader<StreamType, Error>;
	public:
		struct iterator {
			friend class json::ObjReader<StreamType, Error>;
		public:
			using iterator_category = std::forward_iterator_tag;
			using value_type = const std::pair<json::Str, json::Reader<StreamType, Error>>;
			using difference_type = std::ptrdiff_t;
			using pointer = value_type*;
			using reference = value_type&;

		private:
			const ObjReader<StreamType, Error>* pSelf = 0;
			bool pEnd = false;

		public:
			constexpr iterator() = default;

		private:
			constexpr iterator(const ObjReader<StreamType, Error>& self, bool end = false) : pSelf{ &self }, pEnd{ end } {}

		public:
			constexpr reference operator*() const {
				return pSelf->get();
			}
			constexpr pointer operator->() const {
				return &pSelf->get();
			}
			constexpr iterator& operator++() {
				pSelf->next();
				return *this;
			}
			constexpr iterator operator++(int) {
				pSelf->next();
				return *this;
			}
			constexpr bool operator==(const iterator& it) const {
				if (pSelf == 0 || it.pSelf == 0)
					return (pSelf == it.pSelf);
				return (pSelf->pInstance == it.pSelf->pInstance && (pEnd == it.pEnd || pSelf->closed()));
			}
			constexpr bool operator!=(const iterator& it) const {
				return !(*this == it);
			}
		};
		using const_iterator = iterator;

	private:
		mutable std::shared_ptr<detail::ReaderState<StreamType, Error>> pState;
		std::unique_ptr<typename detail::ReaderState<StreamType, Error>::Instance> pInstance;

	public:
		constexpr ObjReader() = delete;
		constexpr ObjReader(json::ObjReader<StreamType, Error>&&) = default;
		constexpr ObjReader(const json::ObjReader<StreamType, Error>&) = delete;
		constexpr json::ObjReader<StreamType, Error>& operator=(json::ObjReader<StreamType, Error>&&) = default;
		constexpr json::ObjReader<StreamType, Error>& operator=(const json::ObjReader<StreamType, Error>&) = delete;
		constexpr ~ObjReader() {
			if (pState.get() != 0)
				pState->close(pState, pInstance.get());
		}

	private:
		constexpr bool fNext() const {
			if (pState->next(pState, pInstance.get()))
				return true;
			pState.reset();
			return false;
		}

	private:
		constexpr ObjReader(const std::shared_ptr<detail::ReaderState<StreamType, Error>>& state, std::unique_ptr<typename detail::ReaderState<StreamType, Error>::Instance>&& inst) : pState{ state }, pInstance{ std::move(inst) } {
			fNext();
		}

	public:
		/* fetch an iterator to this object (advancing the iterator is equivalent to calling next() on this object) */
		constexpr iterator begin() const {
			return iterator{ *this, false };
		}

		/* fetch the end iterator to this object (will be equal to any other iterator of this object when closed() return true) */
		constexpr iterator end() const {
			return iterator{ *this, true };
		}

		/* check if a next value exists in the object and prepare it for reading */
		constexpr bool next() const {
			return fNext();
		}

		/* check if the end has been reached and no more valid value can therefore be read */
		constexpr bool closed() const {
			return (pState.get() == 0);
		}

		/* close the current object and thereby skip to the end of this object */
		constexpr void close() const {
			if (pState.get() != 0)
				pState->close(pState, pInstance.get());
			pState.reset();
		}

		/* read the current key (only if the reader is not marked as closed()) */
		constexpr const json::Str& key() const {
			return pInstance->key;
		}

		/* read the current value (only if the reader is not marked as closed()) */
		constexpr const json::Reader<StreamType, Error>& value() const {
			return pInstance->value;
		}

		/* read the current value (only if the reader is not marked as closed()) */
		constexpr const std::pair<json::Str, json::Reader<StreamType, Error>>& get() const {
			return pInstance->value;
		}
	};

	/* construct a json value-reader from the given stream and ensure that the entire stream is a single valid
	*	json-value and parse and validate the json along reading it instead of parsing it in its entirety beforehand
	*	Note: Must not outlive the stream as it may store a reference to it */
	template <str::IsStream StreamType, str::CodeError Error = str::CodeError::replace>
	constexpr json::Reader<std::remove_reference_t<StreamType>, Error> Read(StreamType&& stream) {
		using ActStream = std::remove_reference_t<StreamType>;

		/* setup the first state and fetch the initial value */
		auto state = std::make_shared<detail::ReaderState<ActStream, Error>>(std::forward<StreamType>(stream));
		return state->initValue(state);
	}

	/* same as json::Reader, but uses inheritance to hide the underlying stream-type
	*	Note: This is a light-weight object, which can just be copied around, as it keeps a reference to the actual state */
	using AnyReader = json::Reader<detail::ReadAnyType, str::CodeError::replace>;

	/* same as json::ObjReader, but uses inheritance to hide the underlying stream-type
	*	Note: Although this is a light-weight object, it can only be moved around, as it references the current progress of the reading */
	using AnyObjReader = json::ObjReader<detail::ReadAnyType, str::CodeError::replace>;

	/* same as json::ArrReader, but uses inheritance to hide the underlying stream-type
	*	Note: Although this is a light-weight object, it can only be moved around, as it references the current progress of the reading */
	using AnyArrReader = json::ArrReader<detail::ReadAnyType, str::CodeError::replace>;

	/* construct a json any-value-reader from the given stream, which hides the actual stream-type
	*	by using inheritance internally, and is otherwise equivalent to json::Read (uses Error = str::CodeError::replace)
	*	Note: Must not outlive the sink as it stores a reference to it */
	template <str::IsStream StreamType>
	json::AnyReader ReadAny(StreamType&& stream) {
		using ActStream = std::remove_reference_t<StreamType>;

		/* wrap the stream to be held by the reader */
		std::unique_ptr<str::InheritStream> readStream = std::make_unique<str::StreamImplementation<ActStream>>(std::forward<StreamType>(stream));

		/* setup the first state and fetch the initial value */
		auto state = std::make_shared<detail::ReaderState<detail::ReadAnyType, str::CodeError::replace>>(std::move(readStream));
		return state->initValue(state);
	}
}
