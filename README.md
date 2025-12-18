# JSON Serializer and Deserializer for C++
![C++](https://img.shields.io/badge/language-c%2B%2B20-blue?style=flat-square)
[![License](https://img.shields.io/badge/license-BSD--3--Clause-brightgreen?style=flat-square)](LICENSE.txt)

Header only library written in `C++20` to add support for json serialization and deserialization as well as representation. Simply include [`jsonify.h`](jsonify.h) to add the entire functionality.

The header includes a general representation of any json value `json::Value`, as well as options to serialize anything json-like or deserialize any string to a `json::Value`. Further, it adds `json::Builder` and `json::Reader` to serialize or deserialize any json in a continuous stream style. Lastly, it also adds `json::Viewer`, as a reduced complexity, but pre-parsed json-value from a stream.

The library uses the [`ustring`](https://github.com/BjoernBoss/ustring.git) header-only library to add support for any character encoding and proper decoding.

## Using the library
This library is a header only library. Simply clone the repository, ensure that `./repos` is on the path (or at least that `<ustring/ustring.h>` can be resolved) and include `<jsonify/jsonify.h>`.

    $ git clone https://github.com/BjoernBoss/jsonify.git --recursive

## [json::Value](json-value.h)

`json::Value` represents any valid json construct. Internally it represents objects as `std::unordered_map`, arrays as `std::vector`, and strings as `std::wstring`. It can be constructed from any json-like representation. It offers a user-friendly interface, and will automatically perform type-conversions wherever required/necessary.


```C++
/* assign a direct json-value */
json::Value value = json::Obj{ { L"abc", 5 }, { L"def", json::Null } };

/* assign any json-like, which will be evaluated and assigned based on the type */
value[L"ghi"] = { u8"abc", u8"def", u8"ghi" };
```

## [json::Serialize](json-serialize.h)

The library offers two serialization functions: `json::Serialize(value, indent)` and `json::SerializeTo(sink, value, indent)`. `json::Serialize` will serialize the json-like object to a string-sink (see [`ustring`](https://github.com/BjoernBoss/ustring.git)) of the given type and return the sink. `json::SerializeTo` will perform the same serialization, but will write it to the sink passed in as the first argument.


```C++
auto _s0 = json::Serialize<std::wstring>(json::Arr{ 1, 2, 3 });
auto _s1 = json::Serialize<std::u8string>(json::Obj{
    { L"abc", json::Null },
    { L"def", json::Arr{ 5, 6, 7 } }
}, L"\t");

std::string _s2;
json::SerializeTo(_s2, 50);
json::SerializeTo(std::cout, 50.0f);
```

## [json::Deserialize](json-deserialize.h)

The `json::Deserialize(stream)` function takes any character-stream (see [`ustring`](https://github.com/BjoernBoss/ustring.git)) and deserializes it to a `json::Value`. The `\u` escape sequences within strings are considered `utf-16` encodings. The function expects the entire stream of characters to be fully consumed, and will otherwise raise an exception. For duplicate keys, the last encountered value will be used.

```C++
std::ifstream file = /* ... */;

auto _v0 = json::Deserialize(file);

auto _v1 = json::Deserialize(u"{ \"1\": 50, \"2\": null, \"3\": [] }");
```

## [json::Builder](json-builder.h)

The `json::Builder` can be used to continuously construct a serialized json-string. Suitable for large data-structures, which should be serialized to json, without an intermediate `json::Value` being constructed. To instantiate a `json::Builder`, the function `json::Build(sink, indent)` is provided. It sets up an internal state, which serializes directly out to the string-sink.

The builder will not prevent duplicate keys being written out to objects. But this sequential writing allows for already well-formatted json content to be written out, instead of values.

Important: Regarding to the lifetime of the source objects, the reader uses the same lifetime model, as `ustring`.

```C++
std::ofstream file = /* ... */;

/* builder expects to be assigned any value */
json::Builder<std::ofstream&> builder = json::Build(file, L"  ");

/* make the root builder an object */
auto obj = builder.obj();
obj[u8"abc"] = 50;
obj[u8"def"] = "abc";
auto arr = obj[L"ghi"].arr();
arr.push(1);
arr.push(50);
arr.push("x");

/* important: arr cannot be used anymore after this point, as it
*   will implicitly be closed once a root object is touched again */
obj["y"] = 2;

/* assign any json-like value */
obj["z"] = json::Obj{ { L"abc", 1 }, { L"def", 2 } };

/* explicit close, otherwise implicitly closed at destruction */
obj.close();
```

The `json::Builder` can also be used to embed already well formatted `JSON` string into the output stream.

## [json::Reader](json-reader.h)

The `json::Reader` can be used to read a character-stream and fetch the json value simultaneously to parsing the stream. This is suitable for large data-structures, which should be deserialized from json, without an intermediate `json::Value` being contructed. To instantiate a `json::Reader`, the function `json::Read(stream)` is provided. It sets up an internal state, which directly parses the entire character stream.

Due to the nature of the reader, objects cannot be accessed in random order, and duplicate keys will all be forwarded. But this sequential reading, allows for the object's origin range in the source string to be queried.

Important: Regarding to the lifetime of the source objects, the reader uses the same lifetime model, as `ustring`.

```C++
std::ifstream file = /* ... */;

/* reader which can be of any type */
json::Reader<std::ifstream&> reader = json::Read(file);

/* check if the read is an object */
if (reader.isObj()) {
    json::Value _t0;
    std::wstring _t1;

    /* iterate over all key/value pairs in the object and look for the expected keys */
    for (const auto&[key, val] : reader.obj()) {
        if (key == L"abc" && val.isStr())
            _t1 = val.str();
        else if(key == L"def")
            _t0 = val.value();
    }
}
```
The `json::Reader` can also be used to determine the position of the object in the `JSON` source stream.

## [json::Viewer](json-viewer.h)

The `json::Viewer` can be used to read a character-stream and validate and parse it into a randomly accessible json-like structure. This is suitable for data-structures, which need to be fully validated once, but will immediately be converted to another representation internally, while simultaneously allowing for random accesses to object-members. To instantiate a `json::Viewer`, the function `json::View(stream)` is provided. It sets up an internal state, which directly parses the entire character stream.

The viewer allows objects to be accessed in random order, albeit slower than a `json::Value`, as it has a lookup-time of `O(n)`, and duplicate keys will all be preserved.

```C++
std::ifstream file = /* ... */;

json::Viewer viewer = json::View(file);

/* check if the read is an object */
if (viewer.isObj()) {
    json::Value _t0;
    unsigned long long _t1;

    /* check if the necessary keys exist */
    if (viewer.contains(L"abc", json::Type::string) && viewer.contains(L"def", json::Type::unumber)) {
        _t0 = viewer[L"abc"];
        _t1 = viewer[L"def"].unum();
    }
}
```

## [json::AnyBuilder](json-builder.h), [json::AnyReader](json-reader.h)

As `json::Builder` and `json::Reader` are templated, and based on the type of the sink/stream, the `json::AnyBuilder` and corresponding function `json::BuildAny(sink, indent)`, as well as `json::AnyReader` and corresponding `json::ReadAny(stream)`, are provided. They provide a generic type-independent builder/reader type by hiding the underlying type, using inheritance. The building/reading is therefore slightly more expensive, while offering independence of the type as a trade-off.


## [json::Pointer, json::Resolve](json-pointer.h)

The function `json::Resolve` allows to resolve a given well formed `JSON Pointer` into a `json::Value` or `json::Viewer`, and return the corresponding target object.

The functions `json::Pointer` and `json::PointerTo` can be used to construct a well formed and escaped `JSON Pointer` string.
