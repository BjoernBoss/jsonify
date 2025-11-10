/* SPDX-License-Identifier: BSD-3-Clause */
/* Copyright (c) 2024-2025 Bjoern Boss Henrichsen */
#pragma once

#include "json-common.h"
#include "json-serializer.h"

namespace json {
	namespace detail {
		template <class SinkType, str::CodeError Error>
		class JsonSerializer {
		private:
			detail::Serializer<SinkType, Error> pSerializer;

		private:
			constexpr void fWriteArray(const auto& v) {
				pSerializer.begin(false);
				for (const auto& entry : v) {
					pSerializer.arrayValue();
					fWrite(entry);
				}
				pSerializer.end(false);
			}
			constexpr void fWriteObject(const auto& v) {
				pSerializer.begin(true);
				for (const auto& entry : v) {
					pSerializer.objectKey(entry.first);
					fWrite(entry.second);
				}
				pSerializer.end(true);
			}
			constexpr void fWritePrimitive(const auto& v) {
				return pSerializer.primitive(v);
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
					fWritePrimitive(json::Null);
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
			constexpr JsonSerializer(auto&& sink, std::wstring_view indent, const auto& v) : pSerializer{ std::forward<SinkType>(sink), indent } {
				fWrite(v);
			}
		};
	}

	/* serialize the json-like object to the sink and return it (indentation will be sanitized to
	*	only contain spaces and tabs, if indentation is empty, a compact json stream will be produced) */
	template <str::CodeError Error = str::CodeError::replace>
	constexpr auto& SerializeTo(str::IsSink auto&& sink, const json::IsJson auto& value, std::wstring_view indent = L"\t") {
		detail::JsonSerializer<decltype(sink), Error> _serializer{ sink, indent, value };
		return sink;
	}

	/* serialize the json-like object to an object of the given sink-type using json::SerializeTo and return it (indentation
	*	will be sanitized to only contain spaces and tabs, if indentation is empty, a compact json stream will be produced) */
	template <str::IsSink SinkType, str::CodeError Error = str::CodeError::replace>
	constexpr SinkType Serialize(const json::IsJson auto& value, std::wstring_view indent = L"\t") {
		SinkType sink{};
		json::SerializeTo<Error>(sink, value, indent);
		return sink;
	}
}
