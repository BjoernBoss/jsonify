/* SPDX-License-Identifier: BSD-3-Clause */
/* Copyright (c) 2024-2025 Bjoern Boss Henrichsen */
#pragma once

#include "json-common.h"

namespace json {
	/* deserialization interprets \u escape-sequences as utf-16 encoding */
	namespace detail {
		enum class NumState : uint8_t {
			preSign,
			preDigits,
			inDigits,
			postDigits,
			preFraction,
			inFraction,
			preExpSign,
			preExponent,
			inExponent
		};
		using NumberValue = std::variant<json::UNum, json::INum, json::Real>;

		template <class StreamType, str::CodeError Error>
		class Deserializer {
			using ChType = str::StreamChar<StreamType>;
		private:
			str::Stream<StreamType> pStream;
			std::u32string pBuffer;
			size_t pPosition = 0;
			size_t pLength = 0;
			char32_t pLastToken = str::Invalid;

		public:
			constexpr Deserializer(const str::IsStr auto& s) : pStream{ s } {}
			constexpr Deserializer(StreamType&& s) : pStream{ std::forward<StreamType>(s) } {}

		private:
			template <bool AllowEndOfStream>
			constexpr char32_t fPrepare() {
				while (true) {
					if (AllowEndOfStream && pStream.done())
						return str::Invalid;

					/* fetch the next codepoint and skip all codepoints to be ignored (due to Error) */
					auto [cp, len] = str::GetCodepoint<Error>(pStream.load(str::MaxEncSize<ChType>));
					pStream.consume(pLength = len);
					if (cp != str::Invalid)
						return (pLastToken = cp);

					/* check if the EOF has been reached */
					if (len == 0)
						throw json::DeserializeException(L"Unexpected <EOF> encountered at ", pPosition);
					pPosition += pLength;
				}
			}
			constexpr void fConsume() {
				pPosition += pLength;
				pLastToken = str::Invalid;
			}
			template <bool AllowEndOfStream = false>
			constexpr char32_t fNextToken(bool skipWhiteSpace) {
				/* check if the next token needs to be fetched */
				if (pLastToken == str::Invalid)
					pLastToken = fPrepare<AllowEndOfStream>();

				/* skip any leading whitespace */
				if (skipWhiteSpace) {
					while (pLastToken == U' ' || pLastToken == U'\n' || pLastToken == U'\r' || pLastToken == U'\t') {
						pPosition += pLength;
						pLastToken = fPrepare<AllowEndOfStream>();
					}
				}
				return pLastToken;
			}
			constexpr char32_t fConsumeAndNext(bool skipWhiteSpace) {
				fConsume();
				return fNextToken(skipWhiteSpace);
			}
			constexpr std::pair<char32_t, bool> fParseEscape() {
				char32_t c = fConsumeAndNext(false);

				/* unpack the escape sequence */
				switch (c) {
				case U'\"':
				case U'\\':
				case U'/':
					return { c, false };
				case U'b':
					return { U'\b', false };
				case U'f':
					return { U'\f', false };
				case U'n':
					return { U'\n', false };
				case U'r':
					return { U'\r', false };
				case U't':
					return { U'\t', false };
				case U'u':
					break;
				default:
					fParseError(u8"Unknown escape-sequence in string encountered");
				}

				/* decode the four hexits */
				uint32_t num = 0;
				for (size_t j = 0; j < 4; j++) {
					c = fConsumeAndNext(false);
					uint32_t val = uint32_t(cp::ascii::GetRadix(c));
					if (val >= 16)
						fParseError(u8"Invalid [\\u] escape-sequence in string encountered");
					num = (num << 4) + val;
				}
				return { char32_t(num), true };
			}

		private:
			constexpr void fUnexpectedToken(char32_t token, const char8_t* expected) {
				std::wstring err = str::wd::Format(u8"Unexpected token [{:e}] encountered at {} when {} was expected",
					token, pPosition, expected);
				throw json::DeserializeException(err);
			}
			constexpr void fParseError(const char8_t* what) {
				throw json::DeserializeException(what, L" while parsing the json at ", pPosition);
			}
			constexpr void fCheckWord(std::u32string_view word) {
				/* verify the remaining characters (first character is already verified to determine the type) */
				for (size_t i = 1; i < word.size(); ++i) {
					char32_t c = fConsumeAndNext(false);
					if (c != word[i])
						fUnexpectedToken(c, str::u8::Format(u8"[{}] of [{}]", c, word).c_str());
				}
				fConsume();
			}

		public:
			constexpr bool closeElseSeparator(bool obj) {
				/* fetch the next token and validate it */
				char32_t c = fNextToken(true);
				if (c == (obj ? U'}' : U']') || c == U',') {
					fConsume();
					return (c != U',');
				}

				/* setup the error */
				fUnexpectedToken(c, obj ? u8"[,] or closing object-bracket" : u8"[,] or closing array-bracket");
				return false;
			}
			constexpr bool checkIsEmpty(bool obj) {
				char32_t c = fNextToken(true);
				if (c != (obj ? U'}' : U']'))
					return false;
				fConsume();
				return true;
			}
			constexpr std::pair<json::Type, size_t> peekOrOpenNext() {
				/* fetch the next token */
				char32_t c = fNextToken(true);
				size_t start = pPosition;

				/* check if this is starting an object or array */
				if (c == U'{' || c == U'[') {
					fConsume();
					return { (c == U'{' ? json::Type::object : json::Type::array), start };
				}

				/* check if its starting a string */
				if (c == U'\"')
					return { json::Type::string, start };

				/* check if it starts a number */
				if (c == U'-' || (c >= U'0' && c <= U'9'))
					return { json::Type::inumber, start };

				/* check if it starts a constant */
				if (c == U'n' || c == U't' || c == U'f')
					return { (c == U'n' ? json::Type::null : json::Type::boolean), start };

				/* setup the error */
				fUnexpectedToken(c, u8"json-value");
				return { json::Type::null, start };
			}
			constexpr json::NullType readNull() {
				fCheckWord(U"null");
				return json::Null;
			}
			constexpr json::Bool readBoolean() {
				if (fNextToken(false) == U't') {
					fCheckWord(U"true");
					return json::Bool(true);
				}
				fCheckWord(U"false");
				return json::Bool(false);
			}
			constexpr detail::NumberValue readNumber() {
				detail::NumState state = detail::NumState::preSign;
				bool neg = false;

				/* verify the number, according to the json-number format */
				pBuffer.clear();
				while (true) {
					char32_t c = fNextToken<true>(false);

					/* update the state-machine */
					if (c == U'-' && (state == NumState::preSign || state == NumState::preExpSign)) {
						if (state == NumState::preSign)
							neg = true;
						state = (state == NumState::preSign) ? NumState::preDigits : NumState::preExponent;
					}
					else if (c == U'+' && state == NumState::preExpSign)
						state = NumState::preExponent;
					else if (c == U'.' && (state == NumState::inDigits || state == NumState::postDigits))
						state = NumState::preFraction;
					else if ((c == U'e' || c == U'E') && (state == NumState::inDigits || state == NumState::postDigits || state == NumState::inFraction))
						state = NumState::preExpSign;
					else if (c >= U'0' && c <= U'9' && state != NumState::postDigits) {
						if (state == NumState::preSign || state == NumState::preDigits)
							state = (c == U'0') ? NumState::postDigits : NumState::inDigits;
						else if (state == NumState::preFraction)
							state = NumState::inFraction;
						else if (state == NumState::preExpSign || state == NumState::preExponent)
							state = NumState::inExponent;
					}
					else
						break;

					/* consume the character and add it to the buffer and fetch the next character to be checked */
					pBuffer.push_back(c);
					fConsume();
				}

				/* check if a valid final state has been entered */
				detail::NumberValue value = json::UNum(0);
				if (state == NumState::preSign || state == NumState::preDigits || state == NumState::preFraction || state == NumState::preExpSign || state == NumState::preExponent)
					fParseError(u8"Malformed json number encountered");

				/* try to parse the number as integer (if its out-of-range for ints, parse it again as real) */
				str::ParsedNum result;
				if (state == NumState::inDigits || state == NumState::postDigits) {
					if (neg)
						result = str::ParseNumTo(pBuffer, std::get<json::INum>(value = json::INum()), { .radix = 10, .prefix = str::PrefixMode::none });
					else
						result = str::ParseNumTo(pBuffer, std::get<json::UNum>(value = json::UNum()), { .radix = 10, .prefix = str::PrefixMode::none });
				}
				else
					result.result = str::NumResult::range;

				/* check if the value did not fit into an integer/was not an integer and parse it again as a float */
				if (result.result == str::NumResult::range)
					result = str::ParseNumTo(pBuffer, std::get<json::Real>(value = json::Real()), { .radix = 10, .prefix = str::PrefixMode::none });

				/* check if the entire number has been consumed */
				if (result.consumed != pBuffer.size())
					fParseError(u8"Number parsing error occurred");
				return value;
			}
			constexpr void readString(auto& sink, bool key) {
				/* validate the opening quotation mark */
				char32_t c = fNextToken(true);
				if (c != U'\"') {
					fUnexpectedToken(c, u8"[\"] as start of a string");
					return;
				}

				/* read the tokens until the closing quotation mark is encountered */
				c = fConsumeAndNext(false);
				while (true) {
					/* check if the end has been encountered and consume the ending character */
					if (c == U'\"') {
						fConsume();

						/* check if the key-separation is required */
						if (!key)
							return;
						c = fNextToken(true);
						if (c != U':')
							fUnexpectedToken(c, u8"[:] object-separator");
						else
							fConsume();
						return;
					}

					/* check if the token is wellformed and not an escape-sequence */
					if (cp::prop::IsControl(c))
						fParseError(u8"Control characters in string encountered");
					if (c != U'\\') {
						str::CodepointTo<Error>(sink, c, 1);
						c = fConsumeAndNext(false);
						continue;
					}

					/* parse the escape sequence and check if its a \u encoding */
					str::Encoded<char16_t> sequence;
					while (true) {
						const auto [val, uEscaped] = fParseEscape();

						/* check if the \u sequence was extended */
						if (uEscaped) {
							sequence.push_back(char16_t(val));

							/* check if another \u is being started */
							c = fConsumeAndNext(false);
							if (c == U'\\' && sequence.size() < sequence.max_size())
								continue;
						}

						/* fetch the next character for the primary string loop */
						else
							c = fConsumeAndNext(false);

						/* flush out the current '\u' sequence out */
						str::TranscodeAllTo<Error>(sink, sequence);

						/* add the last cached escape-character (which occurred after the \u-sequence) */
						if (!uEscaped)
							str::CodepointTo<Error>(sink, val, 1);
						break;
					}
				}
			}
			constexpr void checkDone() {
				/* check if the stream is done or only consists of whitespace */
				char32_t c = fNextToken<true>(true);
				if (c != str::Invalid)
					fUnexpectedToken(c, u8"<EOF>");
			}
			constexpr size_t end() const {
				return pPosition;
			}
		};
	}
}
