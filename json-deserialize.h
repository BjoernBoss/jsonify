#pragma once

#include "json-common.h"
#include "json-deserializer.h"
#include "json-value.h"

namespace json {
	namespace detail {
		template <class StreamType, char32_t CodeError>
		class JsonDeserializer {
		private:
			detail::Deserializer<StreamType, CodeError> pDeserializer;

		private:
			constexpr void fObject(json::Obj& out) {
				if (pDeserializer.checkIsEmpty(true))
					return;
				do {
					/* read the key */
					json::Str key;
					pDeserializer.readString(key, true);

					/* read the value and check if the end has been reached */
					fValue(out[key]);
				} while (!pDeserializer.closeElseSeparator(true));
			}
			constexpr void fArray(json::Arr& out) {
				if (pDeserializer.checkIsEmpty(false))
					return;

				/* read the value and check if the end has been reached */
				do {
					fValue(out.emplace_back());
				} while (!pDeserializer.closeElseSeparator(false));
			}
			constexpr void fValue(json::Value& out) {
				switch (pDeserializer.peekOrOpenNext()) {
				case json::Type::string:
					pDeserializer.readString(out.str(), false);
					break;
				case json::Type::object:
					fObject(out.obj());
					break;
				case json::Type::array:
					fArray(out.arr());
					break;
				case json::Type::boolean:
					out = pDeserializer.readBoolean();
					break;
				case json::Type::inumber:
				case json::Type::unumber:
				case json::Type::real: {
					detail::NumberValue value = pDeserializer.readNumber();
					if (std::holds_alternative<json::INum>(value))
						out = std::get<json::INum>(value);
					else if (std::holds_alternative<json::UNum>(value))
						out = std::get<json::UNum>(value);
					else
						out = std::get<json::Real>(value);
					break;
				}
				case json::Type::null:
				default:
					out = pDeserializer.readNull();
					break;
				}
			}

		public:
			constexpr JsonDeserializer(StreamType&& stream, json::Value& out) : pDeserializer{ std::forward<StreamType>(stream) } {
				fValue(out);
				pDeserializer.checkDone();
			}
		};
	}

	/* deserialize the stream to a json-object into a json::Value object
	*	- interprets \u escape-sequences as utf-16 encoding
	*	- expects entire stream to be a single json value until the end with optional whitespace padding
	*	- for objects with multiple identical keys, the last occurring value will be used */
	template <char32_t CodeError = str::err::DefChar>
	constexpr json::Value Deserialize(str::IsStream auto&& stream) {
		json::Value out;
		detail::JsonDeserializer<decltype(stream), CodeError> _deserializer{ std::forward<decltype(stream)>(stream), out };
		return out;
	}
}
