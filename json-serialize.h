#pragma once

#include "json-common.h"

#include <cinttypes>
#include <string>
#include <iostream>
#include <charconv>
#include <limits>
#include <tuple>
#include <type_traits>

namespace json {
	class Utf8Sink {
	public:
		using Ptr = std::shared_ptr<json::Utf8Sink>;

	protected:
		Utf8Sink() = default;

	public:
		virtual ~Utf8Sink() = default;

	public:
		virtual bool consume(const std::string_view& data) = 0;
	};

	namespace sinks {
		class StringSink final : public json::Utf8Sink {
		private:
			std::string& pOut;

		public:
			StringSink(std::string& out) : pOut(out) {}

		public:
			static json::Utf8Sink::Ptr Make(std::string& out) {
				return std::make_unique<sinks::StringSink>(out);
			}

		public:
			bool consume(const std::string_view& data) final {
				pOut.append(data);
				return true;
			}
		};
		class StreamSink final : public json::Utf8Sink {
		private:
			std::ostream& pOut;

		public:
			StreamSink(std::ostream& out) : pOut(out) {}

		public:
			static json::Utf8Sink::Ptr Make(std::ostream& out) {
				return std::make_unique<json::sinks::StreamSink>(out);
			}

		public:
			bool consume(const std::string_view& data) final {
				pOut.write(data.data(), data.size());
				return pOut.good();
			}
		};
	}

	/*
	 *	Indent: Indentation sequence to be used (if empty, compact output)
	 *	BufferSize: Number of bytes buffered before flushing to the sink
	 *	Strings are parsed as utf8/utf16/utf32 (depending on size of type)
	 *	Output is json conform
	 *	Fails if flush fails, otherwise succeeeds
	 *	Serialization can continue on failed object, but will just not write anything out anymore
	 */
	namespace detail {
		template <class Type>
		concept AnyString = std::is_constructible_v<std::string_view, Type> || std::is_constructible_v<std::wstring_view, Type> ||
			std::is_constructible_v<std::u8string_view, Type> || std::is_constructible_v<std::u16string_view, Type> ||
			std::is_constructible_v<std::u32string_view, Type>;
		template <class Type>
		concept AnyPrimitive = std::is_same<std::decay_t<Type>, json::Null>::value || detail::AnyString<Type>
			|| std::is_arithmetic_v<std::decay_t<Type>>;

		class Serializer {
		private:
			/* buffer large enough to hold all numbers/doubles/uft16-sequences */
			char pNumBuffer[96] = { 0 };
			json::Utf8Sink::Ptr pSink;
			std::string pIndent;
			std::string pBuffer;
			size_t pOffset = 0;
			size_t pDepth = 0;
			bool pAlreadyHasValue = false;

		private:
			bool fFlush() {
				if (pSink->consume(std::string_view{ pBuffer.data(), pOffset }))
					pOffset = 0;
				else
					pSink = nullptr;
				return (pSink != nullptr);
			}
			bool fWrite(const std::string_view& data) {
				/* check if the flushing has already failed */
				if (pSink == nullptr)
					return false;

				/* write the data to the buffer and check if it needs to be flushed */
				size_t off = 0;
				while (off < data.size()) {
					size_t count = std::min<size_t>(data.size() - off, pBuffer.size() - pOffset);
					std::copy(data.begin() + off, data.begin() + off + count, pBuffer.begin() + pOffset);
					off += count;

					/* check if the buffer needs to be flushed */
					if ((pOffset += count) >= pBuffer.size() && !fFlush())
						return false;
				}
				return true;
			}
			bool fWrite(char c) {
				return fWrite(std::string_view{ &c, 1 });
			}
			bool fWriteNewline() {
				if (pIndent.empty())
					return true;

				/* add the newline and the indentation */
				if (!fWrite('\n'))
					return false;
				for (size_t i = 0; i < pDepth; ++i) {
					if (!fWrite(pIndent))
						return false;
				}
				return true;
			}
			bool fWriteJsonU16(uint32_t val) {
				pNumBuffer[0] = '\\';
				pNumBuffer[1] = 'u';
				for (size_t i = 0; i < 4; ++i)
					pNumBuffer[2 + i] = "0123456789abcdef"[(val >> (12 - (i * 4))) & 0x0f];
				return fWrite({ pNumBuffer, 6 });
			}
			template <class CType>
			std::tuple<uint32_t, size_t, bool> fFetchCodePoint(const std::basic_string_view<CType>& s, size_t off) const {
				/* on decoding errors: just return first decoded value */

				/* utf-8 */
				if constexpr (sizeof(CType) == 1) {
					uint8_t c8 = static_cast<uint8_t>(s[off]);
					uint32_t cp = 0;
					size_t len = 1;

					if ((c8 & 0x80) == 0x00)
						return { c8, 0, true };
					if ((c8 & 0xe0) == 0xc0) {
						len = 2;
						cp = (c8 & 0x1f);
					}
					else if ((c8 & 0xf0) == 0xe0) {
						len = 3;
						cp = (c8 & 0x0f);
					}
					else if ((c8 & 0xf8) == 0xf0) {
						len = 4;
						cp = (c8 & 0x07);
					}

					if (s.size() - off < len)
						return { c8, 0, false };

					for (size_t j = 1; j < len; ++j) {
						uint8_t n8 = static_cast<uint8_t>(s[off + j]);
						cp = (cp << 6);

						if ((n8 & 0xc0) != 0x80)
							return { c8, 0, false };
						cp |= (n8 & 0x3f);
					}
					return { cp, len - 1, true };
				}

				/* utf-16 */
				else if constexpr (sizeof(CType) == 2) {
					uint32_t c32 = static_cast<uint16_t>(s[off]);

					if (c32 < 0xd800 || c32 > 0xdfff)
						return { c32, 0, true };

					if (c32 > 0xdbff || s.size() - off < 2)
						return { c32, 0, false };

					uint32_t n32 = static_cast<uint16_t>(s[off + 1]);
					if (n32 < 0xdc00 || n32 > 0xdfff)
						return { c32, 0, false };
					return { 0x10000 + ((c32 & 0x03ff) << 10) | (n32 & 0x03ff), 1, true };
				}

				/* utf-32 */
				else
					return { static_cast<uint32_t>(s[off]), 0, true };
			}
			template <class CType>
			bool fWriteString(const std::basic_string_view<CType>& s) {
				if (!fWrite('\"'))
					return false;

				/* add the separate codepoints of the string to the output */
				for (size_t i = 0; i < s.size(); ++i) {
					CType c = s[i];

					/* handle defined escape sequences */
					char escape = 0;
					if (c == '\b')
						escape = 'b';
					else if (c == '\f')
						escape = 'f';
					else if (c == '\n')
						escape = 'n';
					else if (c == '\r')
						escape = 'n';
					else if (c == '\t')
						escape = 't';
					else if (c == '\\' || c == '\"')
						escape = static_cast<char>(c);
					if (escape != '\0') {
						if (!fWrite('\\') || !fWrite(escape))
							return false;
						continue;
					}

					/* read the next codepoint (invalid codepoints will just be skipped) */
					auto [cp, additional, valid] = fFetchCodePoint<CType>(s, i);
					if (!valid)
						continue;
					i += additional;

					/* check if the character is printable and can just be added (although the json-standard allows for any
					*	printable codepoint to be added, this would require any non-ascii code-points to be utf8-encoded) */
					if (std::isprint(cp) && cp < 0x80) {
						if (!fWrite(static_cast<char>(cp)))
							return false;
						continue;
					}

					/* check if the value must be written as a surrogate pair (as too large for single utf-16 char) */
					if (cp >= 0x10000) {
						cp -= 0x10000;
						if (!fWriteJsonU16(0xd800 + ((cp >> 10) & 0x03ff)))
							return false;
						cp = (0xdc00 + (cp & 0x03ff));
					}
					if (!fWriteJsonU16(cp))
						return false;
				}

				return fWrite('\"');
			}
			template <class SType>
			bool fAddString(SType&& s) {
				if constexpr (std::is_constructible_v<std::string_view, SType>)
					return fWriteString<char>(s);
				if constexpr (std::is_constructible_v<std::wstring_view, SType>)
					return fWriteString<wchar_t>(s);
				if constexpr (std::is_constructible_v<std::u8string_view, SType>)
					return fWriteString<char8_t>(s);
				if constexpr (std::is_constructible_v<std::u16string_view, SType>)
					return fWriteString<char16_t>(s);
				if constexpr (std::is_constructible_v<std::u32string_view, SType>)
					return fWriteString<char32_t>(s);
			}

		public:
			void setup(const std::string& indent, const json::Utf8Sink::Ptr& sink, size_t bufferSize) {
				pIndent = indent;
				pSink = sink;
				pBuffer.resize(std::max<size_t>(1, bufferSize));
				pOffset = 0;
				pDepth = 0;
				pAlreadyHasValue = false;
			}
			bool flush() {
				if (pSink == nullptr || pOffset == 0)
					return (pSink != nullptr);
				return fFlush();
			}
			bool valid() const {
				return (pSink != nullptr);
			}

		public:
			template <detail::AnyPrimitive Type>
			bool addPrimitive(Type&& v) {
				if constexpr (std::is_same_v<std::decay_t<Type>, json::Bool>)
					return fWrite(v ? "true" : "false");
				else if constexpr (std::is_same_v<std::decay_t<Type>, json::Null>)
					return fWrite("null");
				else if constexpr (std::is_floating_point_v<std::decay_t<Type>>) {
					using ActType = std::decay_t<Type>;

					/* limit the double to ensure it will not be formatted to 'inf'/'NaN'/... */
					ActType val = v;
					if (!std::isfinite(val))
						val = (val < 0 ? std::numeric_limits<ActType>::lowest() : std::numeric_limits<ActType>::max());

					/* will at all times fit into the buffer and can be written without checking for errors (to_chars is locale independent,
					*	value can just be written without issues regarding non-json-conformant characters, such as ',' as decimal separator) */
					char* end = std::to_chars(pNumBuffer, std::end(pNumBuffer), val, std::chars_format::general).ptr;
					return fWrite({ pNumBuffer, static_cast<size_t>(end - pNumBuffer) });
				}
				else if constexpr (std::is_integral_v<std::decay_t<Type>>) {
					/* will at all times fit into the buffer and can be written without checking for errors */
					char* end = std::to_chars(pNumBuffer, std::end(pNumBuffer), v).ptr;
					return fWrite({ pNumBuffer, static_cast<size_t>(end - pNumBuffer) });
				}
				else if constexpr (detail::AnyString<Type>)
					return fAddString<Type>(v);
			}
			bool begin(bool obj) {
				++pDepth;
				pAlreadyHasValue = false;
				return fWrite(obj ? '{' : L'[');
			}
			template <detail::AnyString Type>
			bool objectKey(Type&& s) {
				/* check if a separator needs to be added */
				if (pAlreadyHasValue && !fWrite(','))
					return false;
				pAlreadyHasValue = true;

				/* add the newline, key, and the separator to the upcoming value */
				return (fWriteNewline() && fAddString<Type>(s) && fWrite(pIndent.empty() ? ":" : ": "));
			}
			bool arrayValue() {
				/* check if a separator needs to be added */
				if (pAlreadyHasValue && !fWrite(','))
					return false;
				pAlreadyHasValue = true;

				/* add the newline for the next value */
				return fWriteNewline();
			}
			bool end(bool obj) {
				--pDepth;

				/* check if the object/array has values, in which case a newline needs to be added */
				if (pAlreadyHasValue && !fWriteNewline())
					return false;

				/* close of the object/array and mark the last value as having a value (if this object was a child of another
				*	object/array, it must have resulted in the corresponding parent having a value, and thereby requiring a separator) */
				pAlreadyHasValue = true;
				return fWrite(obj ? '}' : ']');
			}
		};
	}
}
