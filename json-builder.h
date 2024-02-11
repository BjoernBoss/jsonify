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

	class BuildValue;
	class BuildObject;
	class BuildArray;
	json::BuildValue Build(json::Utf8Sink* sink, const std::string& indent = "\t", size_t bufferSize = 2048);

	namespace detail {
		class BuildState {
			friend json::BuildValue json::Build(json::Utf8Sink*, const std::string&, size_t);
		public:
			struct State {
				detail::Serializer serializer;
				std::stack<detail::BuildState*> active;
				size_t valueStamp = 0;
				bool awaitingValue = false;
			};

		private:
			enum class Type : uint8_t {
				closed,
				value,
				arr,
				obj
			};

		private:
			std::shared_ptr<State> pState;
			size_t pStamp = 0;
			Type pType = Type::closed;

		public:
			BuildState() = default;
			BuildState(const detail::BuildState&) = delete;
			BuildState(detail::BuildState&& b) noexcept {
				pState = std::move(b.pState);
				pStamp = b.pStamp;
				pType = b.pType;
				b.pType = Type::closed;
			}
			detail::BuildState& operator=(const detail::BuildState&) = delete;
			detail::BuildState& operator=(detail::BuildState&& b) noexcept {
				pType = b.pType;
				b.pType = Type::closed;
				std::swap(pState, b.pState);
				std::swap(pType, b.pType);
				std::swap(pStamp, b.pStamp);
				return *this;
			}
			~BuildState() {
				if (pType == Type::closed)
					return;
				fEnsureCapture();
				fClose(true);
			}

		private:
			bool fEnsureCapture() {
				/* check if this object is either burnt or not the active value anymore */
				if (pType == Type::closed || (pType == Type::value && (pStamp != pState->valueStamp || !pState->awaitingValue)))
					throw json::JsonBuilderException("Builder object is not in an active state");

				/* check if this is a value, in which case it must be the active value */
				if (pType == Type::value)
					return true;

				/* check if a value is currently expected and just write null */
				bool valid = true;
				if (pState->awaitingValue) {
					pState->awaitingValue = false;
					valid = pState->serializer.addPrimitive(json::Null());
				}

				/* close all builder until this is the next active builder */
				while (pState->active.top() != this)
					valid = (pState->active.top()->fClose(true) && valid);
				return valid;
			}
			bool fClose(bool unsolicited) {
				bool valid = true;

				/* check if this is not a primitive value, in which case it can just be closed normally */
				if (pType != Type::value) {
					valid = pState->serializer.end(pType == Type::obj);
					pState->active.pop();
				}

				/* check if this is the active value, in which case it can just be replaced by a null
				*	(unless it has was a solicited close, for example because the value was written) */
				else if (pStamp == pState->valueStamp && pState->awaitingValue) {
					pState->awaitingValue = false;
					if (unsolicited)
						valid = pState->serializer.addPrimitive(json::Null());
				}

				/* mark this object as burnt and check if this was the last value, in which case the serializer can be flushed
				*	(no need to check for an awaiting value, as it was either this object, or this capture nulled the value) */
				pType = Type::closed;
				if (pState->active.empty())
					valid = (pState->serializer.flush() && valid);
				return valid;
			}

		public:
			bool error() const {
				return !pState->serializer.valid();
			}
			bool closed() const {
				return (pType == Type::closed);
			}
			bool capture() {
				return fEnsureCapture();
			}

		public:
			/* must first be called once capture has been called */
			detail::Serializer& out() {
				return pState->serializer;
			}
			void passCapture(detail::BuildState* b, bool obj) {
				b->pState = pState;

				/* add the builder as new top and configure it to the corresponding type */
				b->pType = (obj ? Type::obj : Type::arr);
				pState->active.push(b);

				/* add the corresponding begin-token */
				pState->serializer.begin(obj);
			}
			void nextValue(detail::BuildState& b) {
				b.pState = pState;

				/* configure the object and state such that the object is the next value to be written out */
				b.pType = Type::value;
				b.pStamp = ++pState->valueStamp;
				pState->awaitingValue = true;
			}
			bool close() {
				return fClose(false);
			}
		};
	}

	class BuildValue {
		friend json::BuildValue json::Build(json::Utf8Sink*, const std::string&, size_t);
		friend class json::BuildObject;
		friend class json::BuildArray;
	private:
		detail::BuildState pBuilder;

	private:
		BuildValue() = default;

	public:
		BuildValue(const json::BuildValue&) = delete;
		BuildValue(json::BuildValue&&) = default;
		json::BuildValue& operator=(const json::BuildValue&) = delete;
		json::BuildValue& operator=(json::BuildValue&&) = default;
		~BuildValue() = default;

	public:
		bool done() const {
			return pBuilder.closed();
		}
		bool error() const {
			return pBuilder.error();
		}

	public:
		bool set(const json::INum& v) {
			bool valid = pBuilder.capture();

			/* write the value out and mark this object as done */
			valid = (pBuilder.out().addPrimitive(v) && valid);
			return (pBuilder.close() && valid);
		}
		bool set(const json::UNum& v) {
			bool valid = pBuilder.capture();

			/* write the value out and mark this object as done */
			valid = (pBuilder.out().addPrimitive(v) && valid);
			return (pBuilder.close() && valid);
		}
		bool set(const json::Real& v) {
			bool valid = pBuilder.capture();

			/* write the value out and mark this object as done */
			valid = (pBuilder.out().addPrimitive(v) && valid);
			return (pBuilder.close() && valid);
		}
		bool set(const json::Bool& v) {
			bool valid = pBuilder.capture();

			/* write the value out and mark this object as done */
			valid = (pBuilder.out().addPrimitive(v) && valid);
			return (pBuilder.close() && valid);
		}
		bool set(const json::Null& v) {
			bool valid = pBuilder.capture();

			/* write the value out and mark this object as done */
			valid = (pBuilder.out().addPrimitive(v) && valid);
			return (pBuilder.close() && valid);
		}
		bool set(const std::string_view& v) {
			bool valid = pBuilder.capture();

			/* write the value out and mark this object as done */
			valid = (pBuilder.out().addString(v) && valid);
			return (pBuilder.close() && valid);
		}
		bool set(const std::wstring_view& v) {
			bool valid = pBuilder.capture();

			/* write the value out and mark this object as done */
			valid = (pBuilder.out().addString(v) && valid);
			return (pBuilder.close() && valid);
		}
		json::BuildObject obj();
		json::BuildArray arr();
	};

	class BuildObject {
		friend class json::BuildValue;
	private:
		/* must be a pointer as it will be stored in the internal build-state */
		std::unique_ptr<detail::BuildState> pBuilder;

	private:
		BuildObject() {
			pBuilder = std::make_unique<detail::BuildState>();
		}

	public:
		BuildObject(const json::BuildObject&) = delete;
		BuildObject(json::BuildObject&&) = default;
		json::BuildObject& operator=(const json::BuildObject&) = delete;
		json::BuildObject& operator=(json::BuildObject&&) = default;
		~BuildObject() = default;

	public:
		bool done() const {
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
		json::BuildValue add(const std::string_view& key) {
			/* dont pass failure out, as the caller can just call the error-method to check for issues */
			pBuilder->capture();
			pBuilder->out().objectKey(key);

			/* setup the builder and pass the capture to it */
			json::BuildValue out;
			pBuilder->nextValue(out.pBuilder);
			return out;
		}
		json::BuildValue add(const std::wstring_view& key) {
			/* dont pass failure out, as the caller can just call the error-method to check for issues */
			pBuilder->capture();
			pBuilder->out().objectKey(key);

			/* setup the builder and pass the capture to it */
			json::BuildValue out;
			pBuilder->nextValue(out.pBuilder);
			return out;
		}
	};

	class BuildArray {
		friend class json::BuildValue;
	private:
		/* must be a pointer as it will be stored in the internal build-state */
		std::unique_ptr<detail::BuildState> pBuilder;

	private:
		BuildArray() {
			pBuilder = std::make_unique<detail::BuildState>();
		}

	public:
		BuildArray(const json::BuildArray&) = delete;
		BuildArray(json::BuildArray&&) = default;
		json::BuildArray& operator=(const json::BuildArray&) = delete;
		json::BuildArray& operator=(json::BuildArray&&) = default;
		~BuildArray() = default;

	public:
		bool done() const {
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
		json::BuildValue push() {
			/* dont pass failure out, as the caller can just call the error-method to check for issues */
			pBuilder->capture();
			pBuilder->out().arrayValue();

			/* setup the builder and pass the capture to it */
			json::BuildValue out;
			pBuilder->nextValue(out.pBuilder);
			return out;
		}
	};

	inline json::BuildObject json::BuildValue::obj() {
		json::BuildObject out;

		/* dont pass failure out, as the caller can just call the error-method to check for issues (important
		*	to first close after passing the capture, as object can otherwise be flushed prematurely) */
		pBuilder.capture();
		pBuilder.passCapture(out.pBuilder.get(), true);
		pBuilder.close();
		return out;
	}
	inline json::BuildArray json::BuildValue::arr() {
		json::BuildArray out;

		/* dont pass failure out, as the caller can just call the error-method to check for issues (important
		*	to first close after passing the capture, as object can otherwise be flushed prematurely) */
		pBuilder.capture();
		pBuilder.passCapture(out.pBuilder.get(), false);
		pBuilder.close();
		return out;
	}

	inline json::BuildValue Build(json::Utf8Sink* sink, const std::string& indent, size_t bufferSize) {
		json::BuildValue out;

		/* setup the first builder and corresponding build-state */
		out.pBuilder.pState = std::make_shared<detail::BuildState::State>();
		out.pBuilder.pType = detail::BuildState::Type::value;

		/* setup the shared build-state */
		out.pBuilder.pState->serializer.setup(indent, sink, bufferSize);
		out.pBuilder.pState->awaitingValue = true;
		return out;
	}
}
