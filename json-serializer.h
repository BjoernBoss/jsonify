/* SPDX-License-Identifier: BSD-3-Clause */
/* Copyright (c) 2024-2025 Bjoern Boss Henrichsen */
#pragma once

#include "json-common.h"

namespace json::detail {
	template <class SinkType, str::CodeError Error>
	class Serializer {
	private:
		SinkType pSink;
		std::wstring pIndent;
		size_t pDepth = 0;
		bool pAlreadyHasValue = false;

	private:
		constexpr void fNewline() {
			if (pIndent.empty())
				return;

			/* add the newline and the indentation */
			str::CodepointTo<Error>(pSink, U'\n');
			for (size_t i = 0; i < pDepth; ++i)
				str::FastcodeAllTo<Error>(pSink, pIndent);
		}
		constexpr void fJsonUEscape(uint32_t val) {
			char32_t buf[6] = { U'\\', U'u', 0, 0, 0, 0 };

			/* create the escape-sequence and write it out */
			for (size_t i = 0; i < 4; ++i)
				buf[2 + i] = U"0123456789abcdef"[(val >> (12 - (i * 4))) & 0x0f];
			str::FastcodeAllTo<Error>(pSink, std::u32string_view{ buf, 6 });
		}
		constexpr void fString(const auto& s) {
			str::CodepointTo<Error>(pSink, U'\"');

			/* decode the codepoints and handle all relevant escaping, as required by the json-standard (although standard
			*	only requires view characters to be encoded as \u, only ascii characters will not be printed as \u strings) */
			std::basic_string_view<str::StringChar<decltype(s)>> view{ s };
			while (!view.empty()) {
				/* transcode the next character to utf-16 */
				auto [out, len] = str::GetFastcode<char16_t, Error>(view);
				view = view.substr(len);

				/* check if there are 0 (error) or 2 (maximum) chars, in which case they can immediately be written out as \u-encoded sequences */
				if (out.size() != 1) {
					for (char16_t c : out)
						fJsonUEscape(static_cast<uint16_t>(c));
					continue;
				}

				/* check if its an escape sequence */
				if (out[0] == u'\b')
					str::FastcodeAllTo<Error>(pSink, U"\\b");
				else if (out[0] == u'\f')
					str::FastcodeAllTo<Error>(pSink, U"\\f");
				else if (out[0] == u'\n')
					str::FastcodeAllTo<Error>(pSink, U"\\n");
				else if (out[0] == u'\r')
					str::FastcodeAllTo<Error>(pSink, U"\\r");
				else if (out[0] == u'\t')
					str::FastcodeAllTo<Error>(pSink, U"\\t");
				else if (out[0] == u'\\')
					str::FastcodeAllTo<Error>(pSink, U"\\\\");
				else if (out[0] == u'\"')
					str::FastcodeAllTo<Error>(pSink, U"\\\"");
				else if (cp::prop::IsPrint(out[0], false))
					str::CodepointTo<Error>(pSink, char32_t(out[0]));
				else
					fJsonUEscape(static_cast<uint16_t>(out[0]));
			}
			str::CodepointTo<Error>(pSink, U'\"');
		}

	public:
		constexpr Serializer(SinkType&& sink, const std::wstring_view& indent) : pSink{ std::forward<SinkType>(sink) } {
			/* ensure whitespace only consists of tabs/spaces */
			for (wchar_t c : indent) {
				if (c == L' ' || c == L'\t')
					pIndent.push_back(c);
			}
		}

	public:
		constexpr void primitive(const auto& v) {
			using VType = std::remove_cvref_t<decltype(v)>;

			/* check if its one of the constant keywords */
			if constexpr (std::same_as<VType, json::Bool>)
				str::FastcodeAllTo<Error>(pSink, v ? U"true" : U"false");
			else if constexpr (std::same_as<VType, json::NullType>)
				str::FastcodeAllTo<Error>(pSink, U"null");

			/* check if its a float and limit it to ensure it will not be formatted to 'inf'/'nan'/... */
			else if constexpr (std::floating_point<VType>) {
				VType val = v;
				if (!std::isfinite(val))
					val = (val < 0 ? std::numeric_limits<VType>::lowest() : std::numeric_limits<VType>::max());
				str::FloatTo(pSink, val, { .fltStyle = str::FloatStyle::general });
			}

			/* check if its an integer */
			else if constexpr (std::integral<VType>)
				str::IntTo(pSink, v);

			/* write the string object out */
			else
				fString(v);
		}
		constexpr void begin(bool obj) {
			++pDepth;
			pAlreadyHasValue = false;
			str::CodepointTo<Error>(pSink, obj ? U'{' : U'[');
		}
		constexpr void objectKey(const auto& s) {
			/* check if a separator needs to be added */
			if (pAlreadyHasValue)
				str::CodepointTo<Error>(pSink, U',');
			pAlreadyHasValue = true;

			/* add the newline, key, and the separator to the upcoming value */
			fNewline();
			fString(s);
			str::FastcodeAllTo<Error>(pSink, pIndent.empty() ? U":" : U": ");
		}
		constexpr void arrayValue() {
			/* check if a separator needs to be added */
			if (pAlreadyHasValue)
				str::CodepointTo<Error>(pSink, U',');
			pAlreadyHasValue = true;

			/* add the newline for the next value */
			fNewline();
		}
		constexpr void end(bool obj) {
			--pDepth;

			/* check if the object/array has values, in which case a newline needs to be added */
			if (pAlreadyHasValue)
				fNewline();

			/* close of the object/array and mark the last value as having a value (if this object was a child of another
			*	object/array, it must have resulted in the corresponding parent having a value, and thereby requiring a separator) */
			pAlreadyHasValue = true;
			str::CodepointTo<Error>(pSink, obj ? U'}' : U']');
		}
		constexpr void insert(const auto& s) {
			str::FastcodeAllTo<Error>(pSink, s);
		}
	};
}
