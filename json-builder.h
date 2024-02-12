#pragma once

#include "json-common.h"
#include "json-serialize.h"

#include <stack>
#include <stdexcept>

namespace json {
	class JsonBuilderException : public std::runtime_error {
	public:
		JsonBuilderException(const std::string& s) : runtime_error(s) {}
	};

	template <stc::IsChar ChType>
	class BuildValue;

	template <stc::IsChar ChType>
	class BuildObject;

	template <stc::IsChar ChType>
	class BuildArray;

	template <stc::IsChar ChType>
	json::BuildValue<ChType> Build(const json::SinkPtr<ChType>& sink, const std::string& indent = "\t", size_t bufferSize = 2048);

	namespace detail {
		template <stc::IsChar ChType>
		class BuildInstance;

		template <stc::IsChar ChType>
		struct SharedState {
			detail::Serializer<ChType> serializer;
			std::stack<detail::BuildInstance<ChType>*> active;
			size_t valueStamp = 0;
			bool awaitingValue = false;
			bool done = false;
		};

		template <stc::IsChar ChType>
		class BuildInstance {
			friend json::BuildValue<ChType> json::Build<ChType>(const json::SinkPtr<ChType>&, const std::string&, size_t);
		private:
			enum class State : uint8_t {
				closed,
				value,
				arr,
				obj
			};

		private:
			std::shared_ptr<detail::SharedState<ChType>> pShared;
			size_t pStamp = 0;
			State pState = State::closed;

		public:
			BuildInstance() = default;
			BuildInstance(const detail::BuildInstance<ChType>&) = delete;
			BuildInstance(detail::BuildInstance<ChType>&& b) noexcept {
				pShared = std::move(b.pShared);
				pStamp = b.pStamp;
				pState = b.pState;
				b.pState = State::closed;
			}
			detail::BuildInstance<ChType>& operator=(const detail::BuildInstance<ChType>&) = delete;
			detail::BuildInstance<ChType>& operator=(detail::BuildInstance<ChType>&& b) noexcept {
				pState = b.pState;
				b.pState = State::closed;
				std::swap(pShared, b.pShared);
				std::swap(pState, b.pState);
				std::swap(pStamp, b.pStamp);
				return *this;
			}
			~BuildInstance() {
				if (pState == State::closed)
					return;
				fEnsureCapture();
				fClose(true);
			}

		private:
			bool fEnsureCapture() {
				/* check if this object is either burnt or not the active value anymore */
				if (pShared->done || pState == State::closed || (pState == State::value && (pStamp != pShared->valueStamp || !pShared->awaitingValue)))
					throw json::JsonBuilderException("Builder object is not in an active state");

				/* check if this is a value, in which case it must be the active value */
				if (pState == State::value)
					return true;

				/* check if a value is currently expected and just write null */
				bool valid = true;
				if (pShared->awaitingValue) {
					pShared->awaitingValue = false;
					valid = pShared->serializer.addPrimitive(json::Null());
				}

				/* close all builder until this is the next active builder */
				while (pShared->active.top() != this)
					valid = (pShared->active.top()->fClose(true) && valid);
				return valid;
			}
			bool fClose(bool unsolicited) {
				bool valid = true;

				/* check if this is not a primitive value, in which case it can just be closed normally */
				if (pState != State::value) {
					valid = pShared->serializer.end(pState == State::obj);
					pShared->active.pop();
				}

				/* check if this is the active value, in which case it can just be replaced by a null
				*	(unless it has was a solicited close, for example because the value was written) */
				else if (pStamp == pShared->valueStamp && pShared->awaitingValue) {
					pShared->awaitingValue = false;
					if (unsolicited)
						valid = pShared->serializer.addPrimitive(json::Null());
				}

				/* mark this object as burnt and check if this was the last value, in which case the serializer can be flushed
				*	(no need to check for an awaiting value, as it was either this object, or this capture nulled the value) */
				pState = State::closed;
				if (pShared->active.empty()) {
					valid = (pShared->serializer.flush() && valid);
					pShared->done = true;
				}
				return valid;
			}

		private:
			bool fWriteValue(json::IsString auto&& v) {
				return pShared->serializer.addPrimitive(v);
			}
			bool fWriteValue(json::IsPrimitive auto&& v) {
				return pShared->serializer.addPrimitive(v);
			}
			bool fWriteValue(json::IsArray auto&& v) {
				if (!pShared->serializer.begin(false))
					return false;
				for (const auto& entry : v) {
					if (!pShared->serializer.arrayValue() || !fWriteValue(entry))
						return false;
				}
				return pShared->serializer.end(false);
			}
			bool fWriteValue(json::IsObject auto&& v) {
				if (!pShared->serializer.begin(true))
					return false;
				for (const auto& entry : v) {
					if (!pShared->serializer.objectKey(entry.first) || !fWriteValue(entry.second))
						return false;
				}
				return pShared->serializer.end(true);
			}
			bool fWriteValue(json::IsValue auto&& v) {
				if (v.isArr())
					return fWriteValue(v.arr());
				if (v.isObj())
					return fWriteValue(v.obj());
				if (v.isStr())
					return fWriteValue(v.str());
				if (v.isINum())
					return fWriteValue(v.inum());
				if (v.isUNum())
					return fWriteValue(v.unum());
				if (v.isReal())
					return fWriteValue(v.real());
				if (v.isBoolean())
					return fWriteValue(v.boolean());
				return fWriteValue(json::Null());
			}

		public:
			void initValue() {
				pState = State::value;
				pStamp = ++pShared->valueStamp;
				pShared->awaitingValue = true;
			}

		public:
			bool error() const {
				return !pShared->serializer.valid();
			}
			bool done() const {
				return pShared->done;
			}
			bool closed() const {
				return (pState == State::closed);
			}
			bool capture() {
				return fEnsureCapture();
			}

		public:
			/* must first be called once capture has been called */
			detail::Serializer<ChType>& out() {
				return pShared->serializer;
			}
			void passCapture(detail::BuildInstance<ChType>* b, bool obj) {
				b->pShared = pShared;

				/* add the builder as new top and configure it to the corresponding type */
				b->pState = (obj ? State::obj : State::arr);
				pShared->active.push(b);

				/* add the corresponding begin-token */
				pShared->serializer.begin(obj);
			}
			void nextValue(detail::BuildInstance<ChType>& b) {
				b.pShared = pShared;

				/* configure the object and state such that the object is the next value to be written out */
				b.pState = State::value;
				b.pStamp = ++pShared->valueStamp;
				pShared->awaitingValue = true;
			}
			bool close() {
				return fClose(false);
			}

		public:
			bool writeValue(json::IsJson auto&& v) {
				return fWriteValue(v);
			}
		};
	}

	template <stc::IsChar ChType>
	class BuildValue {
		friend json::BuildValue<ChType> json::Build<ChType>(const json::SinkPtr<ChType>&, const std::string&, size_t);
		friend class json::BuildObject<ChType>;
		friend class json::BuildArray<ChType>;
	private:
		detail::BuildInstance<ChType> pBuilder;

	private:
		BuildValue() = default;

	public:
		BuildValue(const json::BuildValue<ChType>&) = delete;
		BuildValue(json::BuildValue<ChType>&&) = default;
		json::BuildValue<ChType>& operator=(const json::BuildValue<ChType>&) = delete;
		json::BuildValue<ChType>& operator=(json::BuildValue<ChType>&&) = default;
		~BuildValue() = default;

	public:
		bool done() const {
			return pBuilder.done();
		}
		bool closed() const {
			return pBuilder.closed();
		}
		bool error() const {
			return pBuilder.error();
		}

	public:
		json::BuildValue<ChType>& operator=(json::IsJson auto&& v) {
			BuildValue<ChType>::set(v);
			return *this;
		}

	public:
		bool set(json::IsJson auto&& v) {
			bool valid = pBuilder.capture();

			/* write the value out and mark this object as done */
			valid = (pBuilder.writeValue(v) && valid);
			return (pBuilder.close() && valid);
		}
		json::BuildObject<ChType> obj() {
			json::BuildObject<ChType> out{};

			/* dont pass failure out, as the caller can just call the error-method to check for issues (important
			*	to first close after passing the capture, as object can otherwise be flushed prematurely) */
			pBuilder.capture();
			pBuilder.passCapture(out.pBuilder.get(), true);
			pBuilder.close();
			return out;
		}
		json::BuildArray<ChType> arr() {
			json::BuildArray<ChType> out{};

			/* dont pass failure out, as the caller can just call the error-method to check for issues (important
			*	to first close after passing the capture, as object can otherwise be flushed prematurely) */
			pBuilder.capture();
			pBuilder.passCapture(out.pBuilder.get(), false);
			pBuilder.close();
			return out;
		}
	};

	template <stc::IsChar ChType>
	class BuildObject {
		friend class json::BuildValue<ChType>;
		friend class json::BuildArray<ChType>;
	private:
		/* must be a pointer as it will be stored in the internal build-state */
		std::unique_ptr<detail::BuildInstance<ChType>> pBuilder;

	private:
		BuildObject() {
			pBuilder = std::make_unique<detail::BuildInstance<ChType>>();
		}

	public:
		BuildObject(const json::BuildObject<ChType>&) = delete;
		BuildObject(json::BuildObject<ChType>&&) = default;
		json::BuildObject<ChType>& operator=(const json::BuildObject<ChType>&) = delete;
		json::BuildObject<ChType>& operator=(json::BuildObject<ChType>&&) = default;
		~BuildObject() = default;

	public:
		bool done() const {
			return pBuilder->done();
		}
		bool closed() const {
			return pBuilder->closed();
		}
		bool error() const {
			return pBuilder->error();
		}

	public:
		bool close() {
			bool valid = pBuilder->capture();
			return (pBuilder->close() && valid);
		}
		json::BuildValue<ChType> addVal(detail::SerializeString auto&& k) {
			/* dont pass failure out, as the caller can just call the error-method to check for issues */
			pBuilder->capture();
			pBuilder->out().objectKey(k);

			/* setup the builder and pass the capture to it */
			json::BuildValue<ChType> out{};
			pBuilder->nextValue(out.pBuilder);
			return out;
		}
		json::BuildArray<ChType> addArr(detail::SerializeString auto&& k) {
			/* dont pass failure out, as the caller can just call the error-method to check for issues */
			pBuilder->capture();
			pBuilder->out().objectKey(k);

			/* setup the builder and pass the capture to it */
			json::BuildArray<ChType> out{};
			pBuilder->passCapture(out.pBuilder.get(), false);
			return out;
		}
		json::BuildObject<ChType> addObj(detail::SerializeString auto&& k) {
			/* dont pass failure out, as the caller can just call the error-method to check for issues */
			pBuilder->capture();
			pBuilder->out().objectKey(k);

			/* setup the builder and pass the capture to it */
			json::BuildObject<ChType> out{};
			pBuilder->passCapture(out.pBuilder.get(), true);
			return out;
		}
		bool add(detail::SerializeString auto&& k, json::IsJson auto&& v) {
			/* write the key out */
			bool valid = pBuilder->capture();
			valid = (pBuilder->out().objectKey(k) && valid);

			/* write the value immediately out */
			return (pBuilder->writeValue(v) && valid);
		}
	};

	template <stc::IsChar ChType>
	class BuildArray {
		friend class json::BuildValue<ChType>;
		friend class json::BuildObject<ChType>;
	private:
		/* must be a pointer as it will be stored in the internal build-state */
		std::unique_ptr<detail::BuildInstance<ChType>> pBuilder;

	private:
		BuildArray() {
			pBuilder = std::make_unique<detail::BuildInstance<ChType>>();
		}

	public:
		BuildArray(const json::BuildArray<ChType>&) = delete;
		BuildArray(json::BuildArray<ChType>&&) = default;
		json::BuildArray<ChType>& operator=(const json::BuildArray<ChType>&) = delete;
		json::BuildArray<ChType>& operator=(json::BuildArray<ChType>&&) = default;
		~BuildArray() = default;

	public:
		bool done() const {
			return pBuilder->done();
		}
		bool closed() const {
			return pBuilder->closed();
		}
		bool error() const {
			return pBuilder->error();
		}

	public:
		bool close() {
			bool valid = pBuilder->capture();
			return (pBuilder->close() && valid);
		}
		json::BuildValue<ChType> pushVal() {
			/* dont pass failure out, as the caller can just call the error-method to check for issues */
			pBuilder->capture();
			pBuilder->out().arrayValue();

			/* setup the builder and pass the capture to it */
			json::BuildValue<ChType> out{};
			pBuilder->nextValue(out.pBuilder);
			return out;
		}
		json::BuildArray<ChType> pushArr() {
			/* dont pass failure out, as the caller can just call the error-method to check for issues */
			pBuilder->capture();
			pBuilder->out().arrayValue();

			/* setup the builder and pass the capture to it */
			json::BuildArray<ChType> out{};
			pBuilder->passCapture(out.pBuilder.get(), false);
			return out;
		}
		json::BuildObject<ChType> pushObj() {
			/* dont pass failure out, as the caller can just call the error-method to check for issues */
			pBuilder->capture();
			pBuilder->out().arrayValue();

			/* setup the builder and pass the capture to it */
			json::BuildObject<ChType> out{};
			pBuilder->passCapture(out.pBuilder.get(), true);
			return out;
		}
		bool push(json::IsJson auto&& v) {
			/* start the next value */
			bool valid = pBuilder->capture();
			valid = (pBuilder->out().arrayValue() && valid);

			/* write the value immediately out */
			return (pBuilder->writeValue(v) && valid);
		}
	};

	template <stc::IsChar ChType>
	inline json::BuildValue<ChType> Build(const json::SinkPtr<ChType>& sink, const std::string& indent, size_t bufferSize) {
		json::BuildValue<ChType> out{};

		/* setup the shared state and mark this value as the first value */
		out.pBuilder.pShared = std::make_shared<detail::SharedState<ChType>>();
		out.pBuilder.pShared->serializer.setup(indent, sink, bufferSize);
		out.pBuilder.initValue();
		return out;
	}
}
