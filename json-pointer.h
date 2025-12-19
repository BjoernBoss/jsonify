/* SPDX-License-Identifier: BSD-3-Clause */
/* Copyright (c) 2024-2025 Bjoern Boss Henrichsen */
#pragma once

#include "json-common.h"
#include "json-value.h"
#include "json-viewer.h"

namespace json {
	/* check if the given type constitutes a valid path component for a json pointer */
	template <class Type>
	concept IsStep = std::convertible_to<Type, size_t> || str::IsStr<Type>;

	namespace detail {
		template <class Type, class ChType, class ItType>
		const Type* NextResolve(const Type& value, json::Str& buffer, ItType it, ItType end) {
			/* check if the object can be indexed */
			if (!value.isArr() && !value.isObj())
				return nullptr;
			buffer.clear();

			/* find the end of the next component and replace any escape sequences */
			bool separatorFound = false, inEscape = false;
			while (++it != end) {
				char32_t cp = *it;
				separatorFound = (cp == U'/');
				if (separatorFound)
					break;

				/* check if this is part of an escape sequence */
				if (inEscape) {
					if (cp != U'0' && cp != U'1')
						return nullptr;
					str::CodepointTo(buffer, (cp == U'0' ? U'~' : U'/'));
					inEscape = false;
				}
				else if (cp == U'~')
					inEscape = true;
				else
					str::CodepointTo(buffer, cp);
			}

			/* check if the string contains an incomplete escape sequence */
			if (inEscape)
				return nullptr;

			/* check if an object key is expected or array index */
			const Type* out = nullptr;
			if (value.isObj()) {
				const auto& obj = value.obj();
				auto it = obj.find(buffer);
				if (it != obj.end())
					out = &it->second;
			}
			else {
				const auto& arr = value.arr();
				size_t index = str::ParseNumAll<size_t>(buffer, arr.size());
				if (index < arr.size())
					out = &arr[index];
			}

			/* check if the value could be resolved and if the end has been reached */
			if (out == nullptr || !separatorFound)
				return out;
			return detail::NextResolve<Type, ChType>(*out, buffer, it, end);
		}

		template <class Type>
		const Type* FirstResolve(const Type& value, const auto& path) {
			using ChType = str::StringChar<decltype(path)>;
			std::basic_string_view<ChType> view{ path };
			str::CPRange<ChType, str::CodeError::replace> cps{ view };
			auto it = cps.begin();

			/* check if the path points to the root */
			if (it == cps.end())
				return &value;

			/* ensure that the path is well-formed */
			if (*it != U'/')
				return nullptr;
			json::Str buffer;
			return detail::NextResolve<Type, ChType>(value, buffer, it, cps.end());
		}

		constexpr void AppendTo(auto& sink, const str::IsStr auto& next) {
			using ChType = str::StringChar<decltype(next)>;
			std::basic_string_view<ChType> view{ next };

			/* add the next slash */
			str::CodepointTo(sink, U'/');

			/* escape the codepoints from the next component to the output */
			for (char32_t cp : str::CPRange<ChType, str::CodeError::replace>{ view }) {
				if (cp == U'~')
					str::FastcodeAllTo(sink, U"~0");
				else if (cp == U'/')
					str::FastcodeAllTo(sink, U"~1");
				else
					str::CodepointTo(sink, cp);
			}
		}
		constexpr void AppendTo(auto& sink, size_t next) {
			str::BuildTo(sink, U'/', next);
		}

		template <class Arg, class... Args>
		constexpr void AppendAll(auto& sink, const Arg& arg, const Args&... args) {
			detail::AppendTo(sink, arg);
			if constexpr (sizeof...(args) > 0)
				detail::AppendAll<Args...>(sink, args...);
		}
	}

	/* resolve the given json pointer in the json::Value and return a reference
	*	to it (or null if the path is malformed or could not be resolved) */
	const json::Value* Resolve(const json::Value& value, const str::IsStr auto& path) {
		return detail::FirstResolve<json::Value>(value, path);
	}

	/* resolve the given json pointer in the json::Viewer and return a reference
	*	to it (or null if the path is malformed or could not be resolved) */
	const json::Viewer* Resolve(const json::Viewer& value, const str::IsStr auto& path) {
		return detail::FirstResolve<json::Viewer>(value, path);
	}

	/* append the given steps to a json pointer in the sink and return a reference to it
	*	(sink must either be empty, or an already valid json pointer to append to) */
	constexpr void PointerTo(str::IsSink auto&& sink, const json::IsStep auto&... steps) {
		if constexpr (sizeof...(steps) > 0)
			detail::AppendAll(sink, steps...);
	}

	/* create a json pointer for the given steps and write them to an object of [SinkType] and return it */
	template <str::IsSink SinkType>
	constexpr SinkType Pointer(const json::IsStep auto&... steps) {
		SinkType sink{};
		json::PointerTo(sink, steps...);
		return sink;
	}
}
