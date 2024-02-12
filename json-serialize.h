#pragma once

#include "json-common.h"

#include <cinttypes>
#include <string>
#include <iostream>
#include <charconv>
#include <limits>
#include <tuple>
#include <concepts>
#include <type_traits>

namespace json {
	template <stc::IsChar ChType>
	class Sink {
	protected:
		Sink() = default;

	public:
		virtual ~Sink() = default;

	public:
		virtual bool consume(const std::basic_string_view<ChType>& data) = 0;
	};

	template <stc::IsChar ChType>
	using SinkPtr = std::shared_ptr<json::Sink<ChType>>;

	namespace sinks {
		template <stc::IsChar ChType>
		class StringSink final : public json::Sink<ChType> {
		private:
			std::basic_string<ChType>& pOut;

		public:
			StringSink(std::basic_string<ChType>& out) : pOut(out) {}

		public:
			static json::SinkPtr<ChType> Make(std::basic_string<ChType>& out) {
				return std::make_unique<sinks::StringSink<ChType>>(out);
			}

		public:
			bool consume(const std::basic_string_view<ChType>& data) final {
				pOut.append(data);
				return true;
			}
		};

		template <stc::IsChar ChType>
		class StreamSink final : public json::Sink<ChType> {
		private:
			std::basic_ostream<ChType>& pOut;

		public:
			StreamSink(std::basic_ostream<ChType>& out) : pOut(out) {}

		public:
			static json::SinkPtr<ChType> Make(std::basic_ostream<ChType>& out) {
				return std::make_unique<json::sinks::StreamSink<ChType>>(out);
			}

		public:
			bool consume(const std::basic_string_view<ChType>& data) final {
				pOut.write(data.data(), data.size());
				return pOut.good();
			}
		};
	}

	/*
	 *	Indent: Indentation sequence to be used (if empty, compact output)
	 *	BufferSize: Number of bytes buffered before flushing to the sink
	 *	Output is json conform
	 *	Fails if flush fails, otherwise succeeeds
	 *	Serialization can continue on failed object, but will just not write anything out anymore
	 */
	namespace detail {
		template <class Type>
		concept SerializeString = json::IsString<Type>;
		template <class Type>
		concept SerializePrimitive = json::IsString<Type> || json::IsPrimitive<Type>;

		template <stc::IsChar ChType>
		class Serializer {
		private:
			/* buffer large enough to hold all numbers/doubles/uft16-sequences */
			char pNumBuffer[96] = { 0 };
			json::SinkPtr<ChType> pSink;
			stc::String<ChType> pBuffer;
			std::string pIndent;
			size_t pOffset = 0;
			size_t pDepth = 0;
			bool pAlreadyHasValue = false;

		private:
			bool fFlush() {
				if (pSink->consume(stc::View<ChType>{ pBuffer.data(), pOffset }))
					pOffset = 0;
				else
					pSink = nullptr;
				return (pSink != nullptr);
			}
			bool fWrite(stc::IsString auto&& data) {
				/* check if the flushing has already failed */
				if (pSink == nullptr)
					return false;

				/* write the data to the buffer and check if it needs to be flushed */
				stc::ViewFromStr<decltype(data)> view{ data };
				while (!view.empty()) {
					/* transcode the next codepoint (invalid codepoints will just not show up in the output) */
					auto [out, len] = stc::Transcode<ChType>(view);
					view = view.substr(len);

					/* check if the data can be appended to the buffer, or if it needs to be flushed (buffer-size is
					*	ensured to be large enough to fit at least one transcoded character of any possible length) */
					if (pBuffer.size() - pOffset < out.size() && !fFlush())
						return false;
					std::copy(out.begin(), out.end(), pBuffer.begin() + pOffset);
					pOffset += out.size();
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
				return fWrite(std::string_view{ pNumBuffer, 6 });
			}
			bool fWriteString(stc::IsString auto&& s) {
				if (!fWrite('\"'))
					return false;
				stc::ViewFromStr<decltype(s)> view{ s };

				/* decode the codepoints and handle all relevant escaping, as required by the json-standard
				*	(although standard only requires view characters to be encoded as \u, only ascii characters
				*	will not be printed as \u strings; invalid codepoints will be replaced by default error-char) */
				while (!view.empty()) {
					/* transcode the next character to utf-16 */
					auto [out, len] = stc::Transcode<char16_t>(view);
					view = view.substr(len);

					/* check if there are 0 (error) or 2 (maximum) chars, in which case they can immediately be written out as \u-encoded sequences */
					if (out.size() != 1) {
						for (char16_t c : out) {
							if (!fWriteJsonU16(static_cast<uint16_t>(c)))
								return false;
						}
						continue;
					}

					/* check if its an escape sequence */
					uint16_t c = static_cast<uint16_t>(out[0]);
					if (c == '\b')
						c = 'b';
					else if (c == '\f')
						c = 'f';
					else if (c == '\n')
						c = 'n';
					else if (c == '\r')
						c = 'n';
					else if (c == '\t')
						c = 't';
					else if (c != '\\' && c != '\"') {
						/* add the character (only clear if its printable and valid ascii) */
						if ((std::isprint(c) && c < 0x80) ? fWrite(static_cast<char>(c)) : fWriteJsonU16(c))
							continue;
						return false;
					}

					/* the character must be escaped */
					if (!fWrite('\\') || !fWrite(static_cast<char>(c)))
						return false;
				}
				return fWrite('\"');
			}

		public:
			void setup(const std::string& indent, const json::SinkPtr<ChType>& sink, size_t bufferSize) {
				/* ensure whitespace only consists of tabs/spaces */
				pIndent.clear();
				for (char c : indent) {
					if (c == ' ' || c == '\t')
						pIndent.push_back(c);
				}

				pSink = sink;
				pBuffer.resize(std::max<size_t>({ 1, stc::MaxEncodeLength, bufferSize }));
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
			bool addPrimitive(detail::SerializePrimitive auto&& v) {
				using VType = std::decay_t<decltype(v)>;

				if constexpr (std::same_as<VType, json::Bool>)
					return fWrite(v ? "true" : "false");
				else if constexpr (std::same_as<VType, json::Null>)
					return fWrite("null");
				else if constexpr (std::floating_point<VType>) {
					/* limit the double to ensure it will not be formatted to 'inf'/'NaN'/... */
					VType val = v;
					if (!std::isfinite(val))
						val = (val < 0 ? std::numeric_limits<VType>::lowest() : std::numeric_limits<VType>::max());

					/* will at all times fit into the buffer and can be written without checking for errors (to_chars is locale independent,
					*	value can just be written without issues regarding non-json-conformant characters, such as ',' as decimal separator) */
					char* end = std::to_chars(pNumBuffer, std::end(pNumBuffer), val, std::chars_format::general).ptr;
					return fWrite(std::string_view{ pNumBuffer, static_cast<size_t>(end - pNumBuffer) });
				}
				else if constexpr (std::integral<VType>) {
					/* will at all times fit into the buffer and can be written without checking for errors */
					char* end = std::to_chars(pNumBuffer, std::end(pNumBuffer), v).ptr;
					return fWrite(std::string_view{ pNumBuffer, static_cast<size_t>(end - pNumBuffer) });
				}
				else if constexpr (detail::SerializeString<VType>)
					return fWriteString(v);
			}
			bool begin(bool obj) {
				++pDepth;
				pAlreadyHasValue = false;
				return fWrite(obj ? '{' : L'[');
			}
			bool objectKey(detail::SerializeString auto&& s) {
				/* check if a separator needs to be added */
				if (pAlreadyHasValue && !fWrite(','))
					return false;
				pAlreadyHasValue = true;

				/* add the newline, key, and the separator to the upcoming value */
				return (fWriteNewline() && fWriteString(s) && fWrite(pIndent.empty() ? ":" : ": "));
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

		template <stc::IsChar ChType>
		class JsonSerializer {
		private:
			detail::Serializer<ChType> pSerializer;

		private:
			bool fProcess(json::IsPrimitive auto&& v) {
				return pSerializer.addPrimitive(v);
			}
			bool fProcess(json::IsString auto&& v) {
				return pSerializer.addPrimitive(v);
			}
			bool fProcess(json::IsArray auto& v) {
				if (!pSerializer.begin(false))
					return false;
				for (const auto& entry : v) {
					if (!pSerializer.arrayValue() || !fProcess(entry))
						return false;
				}
				return pSerializer.end(false);
			}
			bool fProcess(json::IsObject auto& v) {
				if (!pSerializer.begin(true))
					return false;
				for (const auto& entry : v) {
					if (!pSerializer.objectKey(entry.first) || !fProcess(entry.second))
						return false;
				}
				return pSerializer.end(true);
			}
			bool fProcess(json::IsValue auto&& v) {
				if (v.isArr())
					return fProcess(v.arr());
				if (v.isObj())
					return fProcess(v.obj());
				if (v.isStr())
					return fProcess(v.str());
				if (v.isINum())
					return fProcess(v.inum());
				if (v.isUNum())
					return fProcess(v.unum());
				if (v.isReal())
					return fProcess(v.real());
				if (v.isBoolean())
					return fProcess(v.boolean());
				return fProcess(json::Null());
			}

		public:
			bool run(json::IsJson auto&& v, const json::SinkPtr<ChType>& sink, const std::string& indent, size_t bufferSize) {
				pSerializer.setup(indent, sink, bufferSize);
				if (!fProcess(v))
					return false;
				return pSerializer.flush();
			}
		};
	}

	/* serialize json-like object to the sink */
	template <stc::IsChar ChType>
	bool Serialize(json::IsJson auto&& v, const json::SinkPtr<ChType>& sink, const std::string& indent = "\t", size_t bufferSize = 2048) {
		return detail::JsonSerializer<ChType>{}.run(v, sink, indent, bufferSize);
	}

	/* convenience wrappers */
	std::string ToString(json::IsJson auto&& v, const std::string& indent = "\t") {
		std::string out;
		if (!json::Serialize(v, sinks::StringSink<char>::Make(out), indent))
			out.clear();
		return out;
	}
	std::wstring ToWideString(json::IsJson auto&& v, const std::string& indent = "\t") {
		std::wstring out;
		if (!json::Serialize(v, sinks::StringSink<wchar_t>::Make(out), indent))
			out.clear();
		return out;
	}
	bool ToStream(json::IsJson auto&& v, std::ostream& stream, const std::string& indent = "\t") {
		return json::Serialize(v, sinks::StreamSink<char>::Make(stream), indent);
	}
	bool ToWideStream(json::IsJson auto&& v, std::wostream& stream, const std::string& indent = "\t") {
		return json::Serialize(v, sinks::StreamSink<wchar_t>::Make(stream), indent);
	}
}
