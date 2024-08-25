#pragma once

#include "json-common.h"
#include "json-deserializer.h"
#include "json-value.h"

/*
*	check if implementation for copy-assignment of json::Reader is necessary (like for json::Value)
*	add AnyReader
*	add comments
*	add constexpr json::Value value() const to json::Reader
*	make json::Value constructible from any json::IsJson
*/


namespace json {
	template <str::IsStream StreamType, char32_t CodeError = str::err::DefChar>
	class Reader;
	template <str::IsStream StreamType, char32_t CodeError = str::err::DefChar>
	class ArrReader;
	template <str::IsStream StreamType, char32_t CodeError = str::err::DefChar>
	class ObjReader;

	namespace detail {
		template <class StreamType, char32_t CodeError>
		class ReaderState;

		using StrReader = std::shared_ptr<json::Str>;

		template <class StreamType, char32_t CodeError>
		struct ArrReference {
			std::shared_ptr<detail::ReaderState<StreamType, CodeError>> state;
			size_t stamp = 0;
		};

		template <class StreamType, char32_t CodeError>
		struct ObjReference {
			std::shared_ptr<detail::ReaderState<StreamType, CodeError>> state;
			size_t stamp = 0;
		};

		/* json-null first to default-construct as null */
		template <class StreamType, char32_t CodeError>
		using ReaderParent = std::variant<json::Null, json::UNum, json::INum, json::Real, json::Bool, detail::StrReader, detail::ArrReference<StreamType, CodeError>, detail::ObjReference<StreamType, CodeError>>;

		template <class StreamType, char32_t CodeError>
		class ReaderState {
		public:
			struct Instance {
				std::pair<json::Str, json::Reader<StreamType, CodeError>> value;
				std::unique_ptr<Instance> self;
				bool object = false;
				bool opened = false;
			};

		private:
			detail::Deserializer<StreamType, CodeError> pDeserializer;
			std::vector<Instance*> pActive;
			size_t pNextStamp = 0;

		public:
			constexpr ReaderState(StreamType&& stream) : pDeserializer{ std::forward<StreamType>(stream) } {}
			constexpr ~ReaderState() {
				/* close all opened objects (will ensure the entire json is parsed properly) */
				while (!pActive.empty())
					while (fReadNextValue(nullptr)) {}
			}

		private:
			constexpr json::Reader<StreamType, CodeError> fValue(const std::shared_ptr<detail::ReaderState<StreamType, CodeError>>& _this) {
				switch (pDeserializer.peekOrOpenNext()) {
				case json::Type::unumber:
				case json::Type::inumber:
				case json::Type::real: {
					detail::NumberValue num = pDeserializer.readNumber();
					if (std::holds_alternative<json::INum>(num))
						return json::Reader<StreamType, CodeError>{ detail::ReaderParent<StreamType, CodeError>{ std::get<json::INum>(num) } };
					if (std::holds_alternative<json::UNum>(num))
						return json::Reader<StreamType, CodeError>{ detail::ReaderParent<StreamType, CodeError>{ std::get<json::UNum>(num) } };
					return json::Reader<StreamType, CodeError>{ detail::ReaderParent<StreamType, CodeError>{ std::get<json::Real>(num) } };
				}
				case json::Type::boolean:
					return json::Reader<StreamType, CodeError>{ detail::ReaderParent<StreamType, CodeError>{ pDeserializer.readBoolean() } };
				case json::Type::string: {
					detail::StrReader str = std::make_shared<json::Str>();
					pDeserializer.readString(*str, false);
					return json::Reader<StreamType, CodeError>{ detail::ReaderParent<StreamType, CodeError>{ str } };
				}
				case json::Type::array: {
					std::unique_ptr<Instance> inst = std::make_unique<Instance>();
					pActive.push_back(inst.get());
					inst->object = false;
					inst->self = std::move(inst);
					return json::Reader<StreamType, CodeError>{ detail::ReaderParent<StreamType, CodeError>{ detail::ArrReference<StreamType, CodeError>{ _this, ++pNextStamp } } };
				}
				case json::Type::object: {
					std::unique_ptr<Instance> inst = std::make_unique<Instance>();
					pActive.push_back(inst.get());
					inst->object = true;
					inst->self = std::move(inst);
					return json::Reader<StreamType, CodeError>{ detail::ReaderParent<StreamType, CodeError>{ detail::ObjReference<StreamType, CodeError>{ _this, ++pNextStamp } } };
				}
				case json::Type::null:
				default:
					return json::Reader<StreamType, CodeError>{ detail::ReaderParent<StreamType, CodeError>{ pDeserializer.readNull() } };
				}
			}
			constexpr bool fReadNextValue(const std::shared_ptr<detail::ReaderState<StreamType, CodeError>>& _this) {
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
			constexpr bool next(const std::shared_ptr<detail::ReaderState<StreamType, CodeError>>& _this, Instance* instance) {
				/* check if the instance is still in the active stack */
				size_t index = pActive.size();
				while (index > 0 && pActive[index - 1] != instance)
					--index;
				if (index == 0)
					throw json::JsonReaderException("Reader is not in an active state");

				/* close all other open objects until the current object has been reached */
				while (pActive.size() > index) {
					while (fReadNextValue(_this)) {}
				}

				/* check if another value is encountered and read it */
				return fReadNextValue(_this);
			}
			constexpr json::Reader<StreamType, CodeError> initValue(const std::shared_ptr<detail::ReaderState<StreamType, CodeError>>& _this) {
				json::Reader<StreamType, CodeError> value = fValue(_this);

				/* check if this is not an opened value and should therefore have consumed the entire source */
				if (pActive.empty())
					pDeserializer.checkDone();
				return value;
			}
			constexpr std::unique_ptr<Instance> open(size_t stamp, bool object) {
				/* check if the object is the next in line and has not yet been fetched */
				if (stamp != pNextStamp || pActive.back()->self.get() == 0)
					throw json::JsonReaderException(object ? "Object has already been opened for reading" : "Array has already been opened for reading");

				/* fetch the instance-pointer */
				std::unique_ptr<Instance> inst;
				pActive.back()->self.swap(inst);
				return std::move(inst);
			}
		};
	}

	template <str::IsStream StreamType, char32_t CodeError>
	class Reader : private detail::ReaderParent<StreamType, CodeError> {
		friend class detail::ReaderState<StreamType, CodeError>;
	public:
		constexpr Reader() : detail::ReaderParent<StreamType, CodeError>{ json::Null() } {}
		constexpr Reader(json::Reader<StreamType, CodeError>&&) = default;
		constexpr Reader(const json::Reader<StreamType, CodeError>&) = default;
		constexpr json::Reader<StreamType, CodeError>& operator=(json::Reader<StreamType, CodeError>&&) = default;
		constexpr json::Reader<StreamType, CodeError>& operator=(const json::Reader<StreamType, CodeError>&) = default;

	private:
		constexpr Reader(const detail::ReaderParent<StreamType, CodeError>& v) : detail::ReaderParent<StreamType, CodeError>{ v } {}

	public:
		constexpr bool isNull() const {
			return std::holds_alternative<json::Null>(*this);
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
			return std::holds_alternative<detail::ObjReference<StreamType, CodeError>>(*this);
		}
		constexpr bool isArr() const {
			return std::holds_alternative<detail::ArrReference<StreamType, CodeError>>(*this);
		}
		constexpr bool is(json::Type t) const {
			switch (t) {
			case json::Type::array:
				return std::holds_alternative<detail::ArrReference<StreamType, CodeError>>(*this);
			case json::Type::object:
				return std::holds_alternative<detail::ObjReference<StreamType, CodeError>>(*this);
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
				return std::holds_alternative<json::Null>(*this);
			}
		}
		constexpr json::Type type() const {
			if (std::holds_alternative<json::Bool>(*this))
				return json::Type::boolean;
			if (std::holds_alternative<detail::StrReader>(*this))
				return json::Type::string;
			if (std::holds_alternative<detail::ObjReference<StreamType, CodeError>>(*this))
				return json::Type::object;
			if (std::holds_alternative<detail::ArrReference<StreamType, CodeError>>(*this))
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
				throw JsonTypeException("json::Reader is not a bool");
			return std::get<json::Bool>(*this);
		}
		constexpr const json::Str& str() const {
			if (!std::holds_alternative<detail::StrReader>(*this))
				throw JsonTypeException("json::Reader is not a string");
			return *std::get<detail::StrReader>(*this);
		}
		constexpr json::UNum unum() const {
			if (std::holds_alternative<json::INum>(*this) && std::get<json::INum>(*this) >= 0)
				return json::UNum(std::get<json::INum>(*this));
			if (std::holds_alternative<json::Real>(*this) && std::get<json::Real>(*this) >= 0)
				return json::UNum(std::get<json::Real>(*this));

			if (!std::holds_alternative<json::UNum>(*this))
				throw JsonTypeException("json::Reader is not an unsigned-number");
			return std::get<json::UNum>(*this);
		}
		constexpr json::INum inum() const {
			if (std::holds_alternative<json::UNum>(*this))
				return json::INum(std::get<json::UNum>(*this));
			if (std::holds_alternative<json::Real>(*this))
				return json::INum(std::get<json::Real>(*this));

			if (!std::holds_alternative<json::INum>(*this))
				throw JsonTypeException("json::Reader is not a signed-number");
			return std::get<json::INum>(*this);
		}
		constexpr json::Real real() const {
			if (std::holds_alternative<json::UNum>(*this))
				return json::Real(std::get<json::UNum>(*this));
			if (std::holds_alternative<json::INum>(*this))
				return json::Real(std::get<json::INum>(*this));

			if (!std::holds_alternative<json::Real>(*this))
				throw JsonTypeException("json::Reader is not a real");
			return std::get<json::Real>(*this);
		}
		constexpr json::ArrReader<StreamType, CodeError> arr() const {
			if (!std::holds_alternative<detail::ArrReference<StreamType, CodeError>>(*this))
				throw JsonTypeException("json::Reader is not an array");
			const detail::ArrReference<StreamType, CodeError>& arr = std::get<detail::ArrReference<StreamType, CodeError>>(*this);
			return json::ArrReader<StreamType, CodeError>{ arr.state, std::move(arr.state->open(arr.stamp, false)) };
		}
		constexpr json::ObjReader<StreamType, CodeError> obj() const {
			if (!std::holds_alternative<detail::ObjReference<StreamType, CodeError>>(*this))
				throw JsonTypeException("json::Reader is not an object");
			const detail::ObjReference<StreamType, CodeError>& arr = std::get<detail::ObjReference<StreamType, CodeError>>(*this);
			return json::ObjReader<StreamType, CodeError>{ arr.state, std::move(arr.state->open(arr.stamp, true)) };
		}

	public:
		constexpr json::Value value() const {
			return json::Value(*this);
		}
	};

	template <str::IsStream StreamType, char32_t CodeError>
	class ArrReader {
		friend class json::Reader<StreamType, CodeError>;
	public:
		struct iterator {
		public:
			using iterator_category = std::input_iterator_tag;
			using value_type = const json::Reader<StreamType, CodeError>;
			using pointer = value_type*;
			using reference = value_type&;

		private:
			const ArrReader<StreamType, CodeError>& pSelf;
			bool pEnd = false;

		public:
			constexpr iterator(const ArrReader<StreamType, CodeError>& self, bool end = false) : pSelf{ self }, pEnd{ end } {}
			reference operator*() const {
				return pSelf.get();
			}
			pointer operator->() const {
				return &pSelf.get();
			}
			iterator& operator++() {
				pSelf.next();
				return *this;
			}
			bool operator==(const iterator& it) const {
				return (pSelf.pInstance == it.pSelf.pInstance && (pEnd == it.pEnd || pSelf.done()));
			}
			bool operator!=(const iterator& it) const {
				return !(*this == it);
			}
		};

	private:
		mutable std::shared_ptr<detail::ReaderState<StreamType, CodeError>> pState;
		std::unique_ptr<typename detail::ReaderState<StreamType, CodeError>::Instance> pInstance;

	public:
		constexpr ArrReader() = default;
		constexpr ArrReader(json::ArrReader<StreamType, CodeError>&&) = default;
		constexpr ArrReader(const json::ArrReader<StreamType, CodeError>&) = delete;
		constexpr json::ArrReader<StreamType, CodeError>& operator=(json::ArrReader<StreamType, CodeError>&&) = default;
		constexpr json::ArrReader<StreamType, CodeError>& operator=(const json::ArrReader<StreamType, CodeError>&) = delete;

	private:
		constexpr bool fNext() const {
			if (pState->next(pState, pInstance.get()))
				return true;
			pState.reset();
			return false;
		}

	private:
		constexpr ArrReader(const std::shared_ptr<detail::ReaderState<StreamType, CodeError>>& state, std::unique_ptr<typename detail::ReaderState<StreamType, CodeError>::Instance>&& inst) : pState{ state }, pInstance{ std::move(inst) } {
			fNext();
		}

	public:
		constexpr iterator begin() const {
			return iterator{ *this, false };
		}
		constexpr iterator end() const {
			return iterator{ *this, true };
		}
		constexpr bool next() const {
			return fNext();
		}
		constexpr bool done() const {
			return (pState.get() == 0);
		}
		constexpr const json::Reader<StreamType, CodeError>& get() const {
			return pInstance->value.second;
		}
	};

	template <str::IsStream StreamType, char32_t CodeError>
	class ObjReader {
		friend class json::Reader<StreamType, CodeError>;
	public:
		struct iterator {
		public:
			using iterator_category = std::input_iterator_tag;
			using value_type = const std::pair<json::Str, json::Reader<StreamType, CodeError>>;
			using pointer = value_type*;
			using reference = value_type&;

		private:
			const ObjReader<StreamType, CodeError>& pSelf;
			bool pEnd = false;

		public:
			constexpr iterator(const ObjReader<StreamType, CodeError>& self, bool end = false) : pSelf{ self }, pEnd{ end } {}
			reference operator*() const {
				return pSelf.get();
			}
			pointer operator->() const {
				return &pSelf.get();
			}
			iterator& operator++() {
				pSelf.next();
				return *this;
			}
			bool operator==(const iterator& it) const {
				return (pSelf.pInstance == it.pSelf.pInstance && (pEnd == it.pEnd || pSelf.done()));
			}
			bool operator!=(const iterator& it) const {
				return !(*this == it);
			}
		};

	private:
		mutable std::shared_ptr<detail::ReaderState<StreamType, CodeError>> pState;
		std::unique_ptr<typename detail::ReaderState<StreamType, CodeError>::Instance> pInstance;

	public:
		ObjReader() = default;
		ObjReader(json::ObjReader<StreamType, CodeError>&&) = default;
		ObjReader(const json::ObjReader<StreamType, CodeError>&) = delete;
		json::ObjReader<StreamType, CodeError>& operator=(json::ObjReader<StreamType, CodeError>&&) = default;
		json::ObjReader<StreamType, CodeError>& operator=(const json::ObjReader<StreamType, CodeError>&) = delete;

	private:
		constexpr bool fNext() const {
			if (pState->next(pState, pInstance.get()))
				return true;
			pState.reset();
			return false;
		}

	private:
		constexpr ObjReader(const std::shared_ptr<detail::ReaderState<StreamType, CodeError>>& state, std::unique_ptr<typename detail::ReaderState<StreamType, CodeError>::Instance>&& inst) : pState{ state }, pInstance{ std::move(inst) } {
			fNext();
		}

	public:
		constexpr iterator begin() const {
			return iterator{ *this, false };
		}
		constexpr iterator end() const {
			return iterator{ *this, true };
		}
		constexpr bool next() const {
			return fNext();
		}
		constexpr bool done() const {
			return (pState.get() == 0);
		}
		constexpr const json::Str& key() const {
			return pInstance->key;
		}
		constexpr const json::Reader<StreamType, CodeError>& value() const {
			return pInstance->value;
		}
		constexpr const std::pair<json::Str, json::Reader<StreamType, CodeError>>& get() const {
			return pInstance->value;
		}
	};

	template <str::IsStream StreamType, char32_t CodeError = str::err::DefChar>
	constexpr json::Reader<StreamType, CodeError> Read(StreamType&& stream) {
		/* setup the first state and fetch the initial value */
		auto state = std::make_shared<detail::ReaderState<StreamType, CodeError>>(std::forward<StreamType>(stream));
		return state->initValue(state);
	}
}
