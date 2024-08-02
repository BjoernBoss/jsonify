#pragma once

#include "json-common.h"
#include "json-serializer.h"

#include <stack>

namespace json {
	/* exception thrown when an already closed builder-object is being set again */
	class JsonBuilderException : public std::runtime_error {
	public:
		JsonBuilderException(const std::string& s) : runtime_error(s) {}
	};

	template <str::IsSink SinkType, char32_t CodeError = str::err::DefChar>
	class BuildValue;
	template <str::IsSink SinkType, char32_t CodeError = str::err::DefChar>
	class BuildObject;
	template <str::IsSink SinkType, char32_t CodeError = str::err::DefChar>
	class BuildArray;

	namespace detail {
		struct BuildAnyType {
		public:
			std::unique_ptr<str::InheritSink> sink;

		public:
			BuildAnyType(std::unique_ptr<str::InheritSink>&& sink) : sink{ std::move(sink) } {}
		};

		template <class SinkType>
		struct BuildAnySink final : public str::InheritSink {
		private:
			SinkType& pSink;

		public:
			constexpr BuildAnySink(SinkType& sink) : pSink{ sink } {}

		public:
			virtual void write(char32_t chr, size_t count) {
				str::CodepointTo<str::err::DefChar>(pSink, chr, count);
			}
			virtual void write(const std::u32string_view& s) {
				str::TranscodeAllTo<str::err::DefChar>(pSink, s);
			}
		};


		template <class SinkType, char32_t CodeError>
		class BuildInstance;

		template <class SinkType, char32_t CodeError>
		struct SharedState {
		public:
			using ActSink = std::conditional_t<std::is_same_v<SinkType, detail::BuildAnyType>, SinkType, SinkType&>;

		public:
			detail::Serializer<ActSink, CodeError> serializer;
			std::stack<detail::BuildInstance<SinkType, CodeError>*> active;
			size_t valueStamp = 0;
			bool awaitingValue = false;
			bool done = false;

		public:
			constexpr SharedState(ActSink&& sink, const std::wstring_view& indent) : serializer{ std::forward<ActSink>(sink), indent } {}
		};

		template <class SinkType, char32_t CodeError>
		class BuildInstance {
		private:
			enum class State : uint8_t {
				closed,
				value,
				arr,
				obj
			};

		private:
			std::shared_ptr<detail::SharedState<SinkType, CodeError>> pShared;
			size_t pStamp = 0;
			State pState = State::closed;

		public:
			constexpr BuildInstance() = default;
			~BuildInstance() {
				/* only arrays/objects are closed at destruction, values are volatile */
				if (pState == State::closed || pState == State::value)
					return;
				fEnsureCapture();
				fClose();
			}

		private:
			void fEnsureCapture() {
				/* check if this object is either burnt or not the active value anymore */
				if (pShared->done || pState == State::closed || (pState == State::value && (pStamp != pShared->valueStamp || !pShared->awaitingValue)))
					throw json::JsonBuilderException("Builder object is not in an active state");

				/* check if this is a value, in which case it must be the active value */
				if (pState == State::value)
					return;

				/* check if a value is currently expected and just write null */
				if (pShared->awaitingValue) {
					pShared->awaitingValue = false;
					pShared->serializer.primitive(json::Null());
				}

				/* close all builder until this is the next active builder */
				while (pShared->active.top() != this)
					pShared->active.top()->fClose();
			}
			void fClose() {
				/* check if this is not a primitive value, in which case it can just be closed normally */
				if (pState != State::value) {
					pShared->serializer.end(pState == State::obj);
					pShared->active.pop();
				}

				/* check if this is the active value, in which case the awaiting state can
				*	be cleared as the value must have been written or passed on */
				else if (pStamp == pShared->valueStamp && pShared->awaitingValue)
					pShared->awaitingValue = false;

				/* mark this object as closed and check if this was the last value (no need to check for
				*	an awaiting value, as it was either this object, or this capture nulled the value) */
				pState = State::closed;
				if (pShared->active.empty())
					pShared->done = true;
			}

		private:
			constexpr void fWriteArray(const auto& v) {
				pShared->serializer.begin(false);
				for (const auto& entry : v) {
					pShared->serializer.arrayValue();
					fWrite(entry);
				}
				pShared->serializer.end(false);
			}
			constexpr void fWriteObject(const auto& v) {
				pShared->serializer.begin(true);
				for (const auto& entry : v) {
					pShared->serializer.objectKey(entry.first);
					fWrite(entry.second);
				}
				pShared->serializer.end(true);
			}
			constexpr void fWritePrimitive(const auto& v) {
				pShared->serializer.primitive(v);
			}
			constexpr void fWriteValue(const auto& v) {
				if (v.isArr())
					fWriteArray(v.arr());
				else if (v.isObj())
					fWriteObject(v.obj());
				else if (v.isStr())
					fWritePrimitive(v.str());
				else if (v.isINum())
					fWritePrimitive(v.inum());
				else if (v.isUNum())
					fWritePrimitive(v.unum());
				else if (v.isReal())
					fWritePrimitive(v.real());
				else if (v.isBoolean())
					fWritePrimitive(v.boolean());
				else
					fWritePrimitive(json::Null());
			}
			template <class Type>
			constexpr void fWrite(const Type& v) {
				if constexpr (json::IsObject<Type>)
					fWriteObject(v);
				else if constexpr (json::IsString<Type>)
					fWritePrimitive(v);
				else if constexpr (json::IsArray<Type>)
					fWriteArray(v);
				else if constexpr (json::IsValue<Type>)
					fWriteValue(v);
				else {
					static_assert(json::IsPrimitive<Type>);
					fWritePrimitive(v);
				}
			}

		public:
			void initValue(const std::shared_ptr<detail::SharedState<SinkType, CodeError>>& state) {
				pShared = state;
				pStamp = ++pShared->valueStamp;
				pShared->awaitingValue = true;
				pState = State::value;
			}

		public:
			bool done() const {
				return pShared->done;
			}
			bool closed() const {
				return (pState == State::closed);
			}
			void capture() {
				fEnsureCapture();
			}

		public:
			/* must first be called once capture has been called */
			detail::Serializer<typename detail::SharedState<SinkType, CodeError>::ActSink, CodeError>& out() {
				return pShared->serializer;
			}
			void passCapture(detail::BuildInstance<SinkType, CodeError>* b, bool obj) {
				b->pShared = pShared;

				/* add the builder as new top and configure it to the corresponding type */
				b->pState = (obj ? State::obj : State::arr);
				pShared->active.push(b);

				/* add the corresponding begin-token */
				pShared->serializer.begin(obj);
			}
			void nextValue(detail::BuildInstance<SinkType, CodeError>* b) {
				b->pShared = pShared;

				/* configure the object and state such that the object is the next value to be written out */
				b->pState = State::value;
				b->pStamp = ++pShared->valueStamp;
				pShared->awaitingValue = true;
			}
			void close() {
				fClose();
			}

		public:
			constexpr void writeValue(const auto& v) {
				fWrite(v);
			}
		};

		struct BuildAccess {
			template <class SinkType, char32_t CodeError>
			static detail::BuildInstance<SinkType, CodeError>* GetValue(json::BuildValue<SinkType, CodeError>& value) {
				return &value.pBuilder;
			}
			template <class SinkType, char32_t CodeError>
			static detail::BuildInstance<SinkType, CodeError>* GetObject(json::BuildObject<SinkType, CodeError>& value) {
				return value.pBuilder.get();
			}
			template <class SinkType, char32_t CodeError>
			static detail::BuildInstance<SinkType, CodeError>* GetArray(json::BuildArray<SinkType, CodeError>& value) {
				return value.pBuilder.get();
			}
			template <class SinkType, char32_t CodeError>
			static json::BuildValue<SinkType, CodeError> MakeValue() {
				return json::BuildValue<SinkType, CodeError>{};
			}
			template <class SinkType, char32_t CodeError>
			static json::BuildObject<SinkType, CodeError> MakeObject() {
				return json::BuildObject<SinkType, CodeError>{};
			}
			template <class SinkType, char32_t CodeError>
			static json::BuildArray<SinkType, CodeError> MakeArray() {
				return json::BuildArray<SinkType, CodeError>{};
			}
		};
	}

	/* json-builder of type [value], which can be used to set the currently expected value (if no value is
	*	written to this object, defaults to null, object is volatile and can be passed around, and it will be
	*	closed on close call, when a value is written/prepared, or when a parent object captures the builder) */
	template <str::IsSink SinkType, char32_t CodeError>
	class BuildValue {
		friend struct detail::BuildAccess;
	private:
		detail::BuildInstance<SinkType, CodeError> pBuilder;

	private:
		constexpr BuildValue() = default;

	public:
		constexpr BuildValue(const json::BuildValue<SinkType, CodeError>&) = default;
		constexpr BuildValue(json::BuildValue<SinkType, CodeError>&&) = default;
		constexpr json::BuildValue<SinkType, CodeError>& operator=(const json::BuildValue<SinkType, CodeError>&) = default;
		constexpr json::BuildValue<SinkType, CodeError>& operator=(json::BuildValue<SinkType, CodeError>&&) = default;
		constexpr ~BuildValue() = default;

	public:
		/* check if the overall built object is done (i.e. a root value has been fully completed) */
		bool done() const {
			return pBuilder.done();
		}

		/* check if this value is done (i.e. a valid value has been assigned to it/corresponding constructor has been created) */
		bool closed() const {
			return pBuilder.closed();
		}

	public:
		json::BuildValue<SinkType, CodeError>& operator=(const json::IsJson auto& v) {
			BuildValue<SinkType, CodeError>::set(v);
			return *this;
		}

	public:
		/* assign the json-like object to this value (closes this object) */
		void set(const json::IsJson auto& v) {
			pBuilder.capture();

			/* write the value out and mark this object as done */
			pBuilder.writeValue(v);
			pBuilder.close();
		}

		/* mark this object and being an object and return the corresponding builder (closes this object) */
		json::BuildObject<SinkType, CodeError> obj() {
			json::BuildObject<SinkType, CodeError> out = detail::BuildAccess::MakeObject<SinkType, CodeError>();
			pBuilder.capture();
			pBuilder.passCapture(detail::BuildAccess::GetObject(out), true);
			pBuilder.close();
			return out;
		}

		/* mark this object and being an array and return the corresponding builder (closes this object) */
		json::BuildArray<SinkType, CodeError> arr() {
			json::BuildArray<SinkType, CodeError> out = detail::BuildAccess::MakeArray<SinkType, CodeError>();
			pBuilder.capture();
			pBuilder.passCapture(detail::BuildAccess::GetArray(out), false);
			pBuilder.close();
			return out;
		}
	};

	/* json-builder of type [object], which can be used to write key-value pairs to the corresponding object and to the
	*	sink (this builder does not prevent already used keys to be used again, it will write all used keys out, object
	*	will be closed once close is called, the object is destructed, or a parent object captures the builder) */
	template <str::IsSink SinkType, char32_t CodeError>
	class BuildObject {
		friend struct detail::BuildAccess;
	private:
		/* must be a pointer as it will be stored in the internal build-state */
		std::unique_ptr<detail::BuildInstance<SinkType, CodeError>> pBuilder;

	private:
		BuildObject() {
			pBuilder = std::make_unique<detail::BuildInstance<SinkType, CodeError>>();
		}

	public:
		constexpr BuildObject(const json::BuildObject<SinkType, CodeError>&) = delete;
		constexpr BuildObject(json::BuildObject<SinkType, CodeError>&&) = default;
		constexpr json::BuildObject<SinkType, CodeError>& operator=(const json::BuildObject<SinkType, CodeError>&) = delete;
		constexpr json::BuildObject<SinkType, CodeError>& operator=(json::BuildObject<SinkType, CodeError>&&) = default;
		constexpr ~BuildObject() = default;

	public:
		/* check if the overall built object is done (i.e. a root value has been fully completed) */
		bool done() const {
			return pBuilder->done();
		}

		/* check if this object is done (i.e. the closing bracket has been written out) */
		bool closed() const {
			return pBuilder->closed();
		}

	public:
		json::BuildValue<SinkType, CodeError> operator[](const json::IsString auto& k) {
			return BuildObject<SinkType, CodeError>::addVal(k);
		}

	public:
		/* close any nested children and this object */
		void close() {
			pBuilder->capture();
			pBuilder->close();
		}

		/* add a new value using the given key and return the value-builder to it */
		json::BuildValue<SinkType, CodeError> addVal(const json::IsString auto& k) {
			pBuilder->capture();
			pBuilder->out().objectKey(k);

			/* setup the builder and pass the capture to it */
			json::BuildValue<SinkType, CodeError> out = detail::BuildAccess::MakeValue<SinkType, CodeError>();
			pBuilder->nextValue(detail::BuildAccess::GetValue(out));
			return out;
		}

		/* add a new array using the given key and return the array-builder to it */
		json::BuildArray<SinkType, CodeError> addArr(const json::IsString auto& k) {
			pBuilder->capture();
			pBuilder->out().objectKey(k);

			/* setup the builder and pass the capture to it */
			json::BuildArray<SinkType, CodeError> out = detail::BuildAccess::MakeArray<SinkType, CodeError>();
			pBuilder->passCapture(detail::BuildAccess::GetArray(out), false);
			return out;
		}

		/* add a new object using the given key and return the object-builder to it */
		json::BuildObject<SinkType, CodeError> addObj(const json::IsString auto& k) {
			pBuilder->capture();
			pBuilder->out().objectKey(k);

			/* setup the builder and pass the capture to it */
			json::BuildObject<SinkType, CodeError> out = detail::BuildAccess::MakeObject<SinkType, CodeError>();
			pBuilder->passCapture(detail::BuildAccess::GetObject(out), true);
			return out;
		}

		/* add a new json-like object using the given key */
		void add(const json::IsString auto& k, const json::IsJson auto& v) {
			/* write the key out */
			pBuilder->capture();
			pBuilder->out().objectKey(k);

			/* write the value immediately out */
			pBuilder->writeValue(v);
		}
	};

	/* json-builder of type [array], which can be used to push values to the the corresponding array and to the sink
	*	(array will be closed once close is called, the array is destructed, or a parent object captures the builder) */
	template <str::IsSink SinkType, char32_t CodeError>
	class BuildArray {
		friend struct detail::BuildAccess;
	private:
		/* must be a pointer as it will be stored in the internal build-state */
		std::unique_ptr<detail::BuildInstance<SinkType, CodeError>> pBuilder;

	private:
		BuildArray() {
			pBuilder = std::make_unique<detail::BuildInstance<SinkType, CodeError>>();
		}

	public:
		constexpr BuildArray(const json::BuildArray<SinkType, CodeError>&) = delete;
		constexpr BuildArray(json::BuildArray<SinkType, CodeError>&&) = default;
		constexpr json::BuildArray<SinkType, CodeError>& operator=(const json::BuildArray<SinkType, CodeError>&) = delete;
		constexpr json::BuildArray<SinkType, CodeError>& operator=(json::BuildArray<SinkType, CodeError>&&) = default;
		constexpr ~BuildArray() = default;

	public:
		/* check if the overall built object is done (i.e. a root value has been fully completed) */
		bool done() const {
			return pBuilder->done();
		}

		/* check if this object is done (i.e. the closing bracket has been written out) */
		bool closed() const {
			return pBuilder->closed();
		}

	public:
		/* close any nested children and this object */
		void close() {
			pBuilder->capture();
			pBuilder->close();
		}

		/* push a new value and return the value-builder to it */
		json::BuildValue<SinkType, CodeError> pushVal() {
			pBuilder->capture();
			pBuilder->out().arrayValue();

			/* setup the builder and pass the capture to it */
			json::BuildValue<SinkType, CodeError> out = detail::BuildAccess::MakeValue<SinkType, CodeError>();
			pBuilder->nextValue(detail::BuildAccess::GetValue(out));
			return out;
		}

		/* push a new array and return the array-builder to it */
		json::BuildArray<SinkType, CodeError> pushArr() {
			pBuilder->capture();
			pBuilder->out().arrayValue();

			/* setup the builder and pass the capture to it */
			json::BuildArray<SinkType, CodeError> out = detail::BuildAccess::MakeArray<SinkType, CodeError>();
			pBuilder->passCapture(detail::BuildAccess::GetArray(out), false);
			return out;
		}

		/* push a new object and return the object-builder to it */
		json::BuildObject<SinkType, CodeError> pushObj() {
			pBuilder->capture();
			pBuilder->out().arrayValue();

			/* setup the builder and pass the capture to it */
			json::BuildObject<SinkType, CodeError> out = detail::BuildAccess::MakeObject<SinkType, CodeError>();
			pBuilder->passCapture(detail::BuildAccess::GetObject(out), true);
			return out;
		}

		/* push a new json-like object */
		void push(const json::IsJson auto& v) {
			/* start the next value */
			pBuilder->capture();
			pBuilder->out().arrayValue();

			/* write the value immediately out */
			pBuilder->writeValue(v);
		}
	};

	/* construct a json builder-value to the given sink, using the corresponding indentation (indentation will be
	*	sanitized to only contain spaces and tabs, if indentation is empty, a compact json stream will be produced)
	*	Note: Must not outlive the sink as it stores a reference to it */
	template <str::IsSink SinkType, char32_t CodeError = str::err::DefChar>
	constexpr json::BuildValue<std::remove_cvref_t<SinkType>, CodeError> Build(SinkType& sink, const std::wstring_view& indent = L"\t") {
		using ActSink = std::remove_cvref_t<SinkType>;
		json::BuildValue<ActSink, CodeError> out = detail::BuildAccess::MakeValue<ActSink, CodeError>();

		/* setup the shared state and mark this value as the first value */
		auto state = std::make_shared<detail::SharedState<ActSink, CodeError>>(sink, indent);
		detail::BuildAccess::GetValue(out)->initValue(state);
		return out;
	}
}

template <>
struct str::CharWriter<json::detail::BuildAnyType> {
	using ChType = char32_t;
	constexpr void operator()(json::detail::BuildAnyType& sink, char32_t chr, size_t count) const {
		sink.sink->write(chr, count);
	}
	constexpr void operator()(json::detail::BuildAnyType& sink, const std::u32string_view& s) const {
		sink.sink->write(s);
	}
};

namespace json {
	/* same as json::BuildValue, but uses inheritance to hide the underlying sink-type */
	using BuildAnyValue = json::BuildValue<detail::BuildAnyType, str::err::DefChar>;

	/* same as json::BuildObject, but uses inheritance to hide the underlying sink-type */
	using BuildAnyObject = json::BuildObject<detail::BuildAnyType, str::err::DefChar>;

	/* same as json::BuildArray, but uses inheritance to hide the underlying sink-type */
	using BuildAnyArray = json::BuildArray<detail::BuildAnyType, str::err::DefChar>;

	/* construct a json any-builder-value to the given sink, using the corresponding indentation but hide the actual sink-type by using inheritance
	*	internally (indentation will be sanitized to only contain spaces and tabs, if indentation is empty, a compact json stream will be produced)
	*	Note: Must not outlive the sink as it stores a reference to it */
	template <str::IsSink SinkType>
	constexpr json::BuildAnyValue BuildAny(SinkType& sink, const std::wstring_view& indent = L"\t") {
		json::BuildAnyValue out = detail::BuildAccess::MakeValue<detail::BuildAnyType, str::err::DefChar>();

		/* setup the wrapper to the sink and the shared state and mark this value as the first value */
		detail::BuildAnyType buildSink{ std::make_unique<detail::BuildAnySink<SinkType>>(sink) };
		auto state = std::make_shared<detail::SharedState<detail::BuildAnyType, str::err::DefChar>>(std::move(buildSink), indent);
		detail::BuildAccess::GetValue(out)->initValue(state);
		return out;
	}
}
