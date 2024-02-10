#include "json-value.h"

#include <cinttypes>
#include <locale>
#include <sstream>
#include <charconv>
#include <limits>
#include <map>

struct JsonSerializeState {
private:
	std::string pBuffer;
	std::string pIndent;
	size_t pDepth = 0;

public:
	JsonSerializeState(const std::string& indent) : pIndent(indent) {}

private:
	std::pair<uint32_t, size_t> fReadCodepoint(const std::wstring& s, size_t off) const {
		/* legacy: utf-8 (on decoding errors, just treat it as separate code points) */
		if constexpr (sizeof(wchar_t) == 1) {
			uint8_t c8 = static_cast<uint8_t>(s[off]);
			uint32_t cp = 0;
			size_t len = 1;

			if ((c8 & 0x80) == 0x00)
				return { c8, 0 };
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
				return { c8, 0 };

			for (size_t j = 1; j < len; ++j) {
				uint8_t n8 = static_cast<uint8_t>(s[off + j]);
				cp = (cp << 6);

				if ((n8 & 0xc0) != 0x80)
					return { c8, 0 };
				cp |= (n8 & 0x3f);
			}
			return { cp, len - 1 };
		}

		/* utf-16 (on decoding errors, just treat it as separate code points) */
		else if constexpr (sizeof(wchar_t) == 2) {
			uint32_t c32 = static_cast<uint16_t>(s[off]);

			if (c32 < 0xd800 || c32 > 0xdbff || s.size() - off < 2)
				return { c32, 0 };
			uint32_t n32 = static_cast<uint16_t>(s[off + 1]);
			if (n32 < 0xdc00 || n32 > 0xdfff)
				return { c32, 0 };
			return { 0x10000 + ((c32 & 0x03ff) << 10) | (n32 & 0x03ff), 1 };
		}

		/* utf-32 */
		else
			return { static_cast<uint32_t>(s[off]), 0 };
	}

private:
	void fAddChar(char c) {
		pBuffer.push_back(c);
	}
	void fAddStr(const char* c) {
		pBuffer.append(c);
	}
	void fAddu16JsonValue(uint32_t val) {
		char buffer[16] = { 0 };
		std::snprintf(buffer, sizeof(buffer), "\\u%04" PRIx16, val);
		fAddStr(buffer);
	}
	void fAddNewline(bool child) {
		/* can never be zero, because the root is '1' */
		size_t count = (child ? pDepth : pDepth - 1);

		/* add the newline and the indentations */
		if (pIndent.empty())
			return;
		pBuffer.push_back('\n');
		for (size_t i = 0; i < count; ++i)
			pBuffer.append(pIndent);
	}
	void fString(const std::wstring& s) {
		static const std::map<wchar_t, const char*> kMapping = {
			{L'\"', "\\\""}, {L'\\', "\\\\"}, {L'\b', "\\b"}, {L'\f', "\\f"},
			{L'\n', "\\n"}, {L'\r', "\\r"}, {L'\t', "\\t"}
		};
		fAddChar('\"');

		for (size_t i = 0; i < s.size(); ++i) {
			wchar_t c = s[i];

			/* handle escape sequences */
			auto it = kMapping.find(c);
			if (it != kMapping.end()) {
				fAddStr(it->second);
				continue;
			}

			/* read the next codepoint */
			auto [cp, additional] = fReadCodepoint(s, i);
			i += additional;

			/* write the codepoint as utf-16 encoded string */
			if (cp >= 0x10000) {
				cp -= 0x10000;
				fAddu16JsonValue(0xd800 + ((cp >> 10) & 0x03ff));
				fAddu16JsonValue(0xdc00 + (cp & 0x03ff));
			}

			/* check if the character is printable and can just be added (although the json-standard allows for any
			*	printable codepoint to be added, this would require any non-ascii code-points to be utf8-encoded) */
			if (cp < 0x80 && std::isprint(cp))
				fAddChar(cp);
			else
				fAddu16JsonValue(cp);
		}

		fAddChar('\"');
	}
	void fArray(const json::Arr& a) {
		if (a.empty()) {
			fAddStr("[]");
			return;
		}
		fAddChar('[');

		for (size_t i = 0; i < a.size(); i++) {
			if (i > 0)
				fAddChar(',');
			fAddNewline(true);
			fProcess(a[i]);
		}

		fAddNewline(false);
		fAddChar(']');
	}
	void fObject(const json::Obj& o) {
		if (o.empty()) {
			fAddStr("{}");
			return;
		}
		fAddChar('{');

		bool content = false;
		for (auto& val : o) {
			if (content)
				fAddChar(',');
			content = true;

			fAddNewline(true);
			fString(val.first);

			fAddStr(pIndent.empty() ? ":" : ": ");

			fProcess(val.second);
		}

		fAddNewline(false);
		fAddChar('}');
	}
	void fAddNum(const json::UNum& n) {
		char buffer[128] = { 0 };
		char* end = std::to_chars(buffer, std::end(buffer), n).ptr;
		pBuffer.append(buffer, end);
	}
	void fAddNum(const json::INum& n) {
		char buffer[128] = { 0 };
		char* end = std::to_chars(buffer, std::end(buffer), n).ptr;
		pBuffer.append(buffer, end);
	}
	void fAddReal(const json::Real& r) {
		/* sanitize the value */
		long double val = r;
		if (!std::isfinite(val))
			val = (val < 0 ? std::numeric_limits<long double>::lowest() : std::numeric_limits<long double>::max());

		/* write the value to the string */
		char buffer[128] = { 0 };
		char* end = std::to_chars(buffer, std::end(buffer), val, std::chars_format::general).ptr;
		pBuffer.append(buffer, end);
	}
	void fAddConst(const json::Value& v) {
		if (v.isBoolean())
			fAddStr(v.boolean() ? "true" : "false");
		else
			fAddStr("null");
	}
	void fProcess(const json::Value& value) {
		++pDepth;

		if (value.isObj())
			fObject(value.obj());
		else if (value.isArr())
			fArray(value.arr());
		else if (value.isStr())
			fString(value.str());
		else if (value.isUNum())
			fAddNum(value.unum());
		else if (value.isINum())
			fAddNum(value.inum());
		else if (value.isReal())
			fAddReal(value.real());
		else
			fAddConst(value);
		--pDepth;
	}

public:
	std::string process(const json::Value& value) {
		/* cache the current locale to be able to reset it again (us-locale is necessary to write reals properly) */
		std::string locale;
		char* currentLocale = std::setlocale(LC_NUMERIC, 0);
		if (currentLocale != 0)
			locale = currentLocale;
		std::setlocale(LC_NUMERIC, "en_US");

		/* create the string and reset the locale */
		fProcess(value);
		std::setlocale(LC_NUMERIC, locale.empty() ? 0 : locale.c_str());
		return pBuffer;
	}
};
struct JsonDeserializeState {
private:
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

private:
	std::string_view pString;
	size_t pOffset = 0;

public:
	JsonDeserializeState(const std::string_view& s) : pString(s) {}

private:
	void fAddCodepoint(uint32_t cp, std::wstring& s) const {
		/* legacy: utf-8 */
		if constexpr (sizeof(wchar_t) == 1) {
			if (cp <= 0x7f) {
				s.push_back(static_cast<wchar_t>(cp));
				return;
			}

			if (cp <= 0x07ff)
				s.push_back(static_cast<wchar_t>(0xc0 | ((cp >> 6) & 0x1f)));
			else {
				if (cp <= 0xffff)
					s.push_back(static_cast<wchar_t>(0xe0 | ((cp >> 12) & 0x0f)));
				else {
					s.push_back(static_cast<wchar_t>(0xf0 | ((cp >> 18) & 0x07)));
					s.push_back(static_cast<wchar_t>(0x80 | ((cp >> 12) & 0x3f)));
				}

				/* push the second to last 6 bits of the codepoint */
				s.push_back(static_cast<wchar_t>(0x80 | ((cp >> 6) & 0x3f)));
			}

			/* push the last 6 bits of the codepoint */
			s.push_back(static_cast<wchar_t>(0x80 | (cp & 0x3f)));
		}

		/* utf-16 */
		else if constexpr (sizeof(wchar_t) == 2) {
			if (cp >= 0x10000) {
				cp -= 0x10000;
				s.push_back(static_cast<wchar_t>(0xd800 + ((cp >> 10) & 0x03ff)));
				cp = 0xdc00 + (cp & 0x03ff);
			}
			s.push_back(static_cast<wchar_t>(cp));
			return;
		}

		/* utf-32 */
		else
			s.push_back(static_cast<wchar_t>(cp));
	}

private:
	uint32_t fNext(bool consume = false, bool skipWhitespace = true) {
		/* consume the last character in the stream */
		if (consume && pOffset < pString.size())
			++pOffset;

		/* skip leading whitespace */
		if (skipWhitespace) {
			while (pOffset < pString.size()) {
				char c = pString[pOffset];
				if (c != ' ' && c != '\n' && c != '\r' && c != '\t')
					break;
				++pOffset;
			}
		}

		if (pOffset < pString.size() && !std::iscntrl(pString[pOffset]))
			return pString[pOffset];
		return 0;
	}
	const wchar_t* fString(std::wstring& out) {
		while (true) {
			uint32_t c = fNext(true, false);

			/* check if the end has been encountered and consume the ending character */
			if (c == '\"') {
				fNext(true);
				return 0;
			}

			/* check if the token is wellformed and not an escape-sequence */
			if (c == 0)
				return (pOffset < pString.size() ? L"Control characters in string encountered" : L"Malformed string with missing closing-quote encountered");
			if (c != '\\') {
				/* will always be an ascii-character as otherwise not valid json */
				fAddCodepoint(c, out);
				continue;
			}

			/* unpack the escape sequence */
			c = fNext(true, false);
			if (c == '\"' || c == '\\' || c == '/')
				out.push_back(static_cast<wchar_t>(c));
			else if (c == 'b')
				out.push_back(L'\b');
			else if (c == 'f')
				out.push_back(L'\f');
			else if (c == 'n')
				out.push_back(L'\n');
			else if (c == 'r')
				out.push_back(L'\r');
			else if (c == 't')
				out.push_back(L'\t');
			else if (c != 'u')
				return L"Unknown escape-sequence in string encountered";
			if (c != 'u')
				continue;

			uint32_t first = 0;
			for (size_t i = 0; i < 2; ++i) {
				uint32_t num = 0;

				/* decode the unicode character */
				for (size_t j = 0; j < 4; j++) {
					c = fNext(true, false);
					num = (num << 4);

					if (c >= '0' && c <= '9')
						num += (c - '0');
					else if (c >= 'a' && c <= 'f')
						num += 10 + (c - 'a');
					else if (c >= 'A' && c <= 'F')
						num += 10 + (c - 'A');
					else
						return L"Invalid \\u-escape-sequence in string encountered";
				}

				/* check if the number is a surrogate-pair number and it forms a valid surrogate pair */
				bool valid = false;
				if (num >= 0xd800 && num <= 0xdbff) {
					if (i == 0 && pString.size() - pOffset > 2 && pString.compare(pOffset + 1, 2, "\\u") == 0) {
						pOffset += 2;
						first = num;
						continue;
					}
				}
				else if (num >= 0xdc00 && num <= 0xdfff)
					valid = (i == 1);
				else
					valid = true;

				/* check if this is a valid codepoint and otherwise simply use the separate surrogate pair-values as codepoints */
				if (valid) {
					if (i == 1)
						num = 0x10000 + ((first - 0xd800) << 10) + (num - 0xdc00);
					fAddCodepoint(num, out);
				}
				else {
					if (i == 1)
						fAddCodepoint(first, out);
					fAddCodepoint(num, out);
				}
				break;
			}
		}
	}
	const wchar_t* fObject(json::Obj& obj) {
		/* skip the opening bracket */
		uint32_t c = fNext(true);

		while (c != '}') {
			if (!obj.empty()) {
				if (c != ',') {
					if (c == 0)
						return L"Malformed object with missing closing-bracket encountered";
					return L"Malformed object with missing entry-separator encountered";
				}
				c = fNext(true);
			}

			if (c != '\"')
				return L"Malformed object with missing key encountered";
			std::wstring key;
			const wchar_t* err = fString(key);
			if (err != 0)
				return err;

			c = fNext();
			if (c != ':')
				return L"Malformed object with missing key-value-separator encountered";
			fNext(true);

			err = fDeserialize(obj[key]);
			if (err != 0)
				return err;

			c = fNext();
		}

		fNext(true);
		return 0;
	}
	const wchar_t* fArray(json::Arr& arr) {
		/* skip the opening bracket */
		uint32_t c = fNext(true);

		while (c != ']') {
			if (!arr.empty()) {
				if (c != ',') {
					if (c == 0)
						return L"Malformed array with missing closing-bracket encountered";
					return L"Malformed array with missing entry-separator encountered";
				}
				fNext(true);
			}

			const wchar_t* err = fDeserialize(arr.emplace_back());
			if (err != 0)
				return err;

			c = fNext();
		}

		fNext(true);
		return 0;
	}
	const wchar_t* fNumber(json::Value& val) {
		NumState state = NumState::preSign;
		size_t len = 0;
		bool isNeg = false;

		/* verify the number, according to the json-number format */
		uint32_t c = fNext();
		while (true) {
			if (c == '-' && (state == NumState::preSign || state == NumState::preExpSign)) {
				if (state == NumState::preSign)
					isNeg = true;
				state = (state == NumState::preSign) ? NumState::preDigits : NumState::preExponent;
			}
			else if (c == '+' && state == NumState::preExpSign)
				state = NumState::preExponent;
			else if (c == '.' && (state == NumState::inDigits || state == NumState::postDigits))
				state = NumState::preFraction;
			else if ((c == 'e' || c == 'E')
				&& (state == NumState::inDigits || state == NumState::postDigits || state == NumState::inFraction))
				state = NumState::preExpSign;
			else if (c >= '0' && c <= '9' && state != NumState::postDigits) {
				if (state == NumState::preSign || state == NumState::preDigits)
					state = (c == '0') ? NumState::postDigits : NumState::inDigits;
				else if (state == NumState::preFraction)
					state = NumState::inFraction;
				else if (state == NumState::preExpSign || state == NumState::preExponent)
					state = NumState::inExponent;
			}
			else
				break;
			++len;
			c = fNext(true, false);
		}

		/* check if a valid final state has been entered */
		if (state == NumState::preSign || state == NumState::preDigits || state == NumState::preFraction || state == NumState::preExpSign || state == NumState::preExponent)
			return L"Malformed json number encountered";

		/* try to parse the number as integer (if its out-of-range for ints, parse it again as real) */
		const char* begin = pString.data() + pOffset - len;
		std::from_chars_result result = {  };
		if (state != NumState::inDigits && state != NumState::postDigits)
			result.ec = std::errc::result_out_of_range;
		else if (isNeg) {
			int64_t num = 0;
			result = std::from_chars(begin, begin + len, num, 10);
			val = json::INum(num);
		}
		else {
			uint64_t num = 0;
			result = std::from_chars(begin, begin + len, num, 10);
			val = json::UNum(num);
		}

		/* parse the number as real */
		if (result.ec == std::errc::result_out_of_range) {
			long double num = 0;
			if ((result = std::from_chars(begin, begin + len, num, std::chars_format::general)).ec == std::errc::result_out_of_range)
				num = (isNeg ? -std::numeric_limits<long double>::infinity() : std::numeric_limits<long double>::infinity());
			val = json::Real(num);
		}

		/* check if the entire number has been consumed */
		if (result.ptr - begin != len)
			return L"Parsing error occurred while parsing a number";
		return 0;
	}
	const wchar_t* fConstant(json::Value& val) {
		/* skip whitespace */
		fNext();

		/* check which constant matches */
		size_t available = pString.size() - pOffset;
		if (available >= 4 && pString.compare(pOffset, 4, "true") == 0) {
			pOffset += 4;
			val = json::Bool(true);
		}
		else if (available >= 4 && pString.compare(pOffset, 4, "null") == 0) {
			pOffset += 4;
			val = json::Null();
		}
		else if (available >= 5 && pString.compare(pOffset, 5, "false") == 0) {
			pOffset += 5;
			val = json::Bool(false);
		}
		else
			return (available == 0 ? L"Premature <EOF> encountered" : L"Unknown json value encountered");
		return 0;
	}
	const wchar_t* fDeserialize(json::Value& val) {
		uint32_t c = fNext();

		/* detect the type of json-value to be parsed */
		if (c == '{')
			return fObject(val.obj());
		else if (c == '[')
			return fArray(val.arr());
		else if (c == '\"')
			return fString(val.str());
		else if (c == '-' || (c >= '0' && c <= '9'))
			return fNumber(val);
		else
			return fConstant(val);
	}
	std::pair<json::Value, bool> fParse() {
		/* deserialize the root value */
		std::pair<json::Value, bool> result = { json::Null(), true };
		const wchar_t* err = fDeserialize(result.first);
		if (err != 0)
			return { json::Str(err), false };

		/* check if the entire string has been consumed */
		fNext();
		if (pOffset < pString.size())
			return { json::Str(L"Expected <EOF> but further content was encountered"), false };
		return result;
	}

public:
	std::pair<json::Value, bool> process() {
		/* cache the current locale to be able to reset it again (us-locale is necessary to parse reals properly) */
		std::string locale;
		char* currentLocale = std::setlocale(LC_NUMERIC, 0);
		if (currentLocale != 0)
			locale = currentLocale;
		std::setlocale(LC_NUMERIC, "en_US");

		/* parse the string and reset the locale */
		std::pair<json::Value, bool> result = fParse();
		std::setlocale(LC_NUMERIC, locale.empty() ? 0 : locale.c_str());
		return result;
	}
};

std::string json::Serialize(const json::Value& v, const std::string& indent) {
	JsonSerializeState state(indent);
	return state.process(v);
}

std::pair<json::Value, bool> json::Deserialize(const std::string_view& s) {
	JsonDeserializeState state(s);
	return state.process();
}
std::pair<json::Value, bool> json::Deserialize(std::istream& s) {
	/* fetch the size of the stream */
	s.seekg(0, std::ios::end);
	size_t strSize = s.tellg();
	s.seekg(0);

	/* allocate a string buffer and read the stream into the buffer */
	std::string fileContent(strSize, ' ');
	s.read(fileContent.data(), fileContent.size());

	/* parse the actual string */
	JsonDeserializeState state(fileContent);
	return state.process();
}
