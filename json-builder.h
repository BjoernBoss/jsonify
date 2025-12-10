/* SPDX-License-Identifier: BSD-3-Clause */
/* Copyright (c) 2024-2025 Bjoern Boss Henrichsen */
#pragma once

#include "json-common.h"
#include "json-serializer.h"

namespace json {
	namespace detail {
		struct BuildAnyType {};
	}

	/* check if the given type is a valid builder-sink */
	template <class Type>
	concept IsBuildType = str::IsSink<Type> || std::is_same_v<Type, detail::BuildAnyType>;

	template <json::IsBuildType SinkType, str::CodeError Error = str::CodeError::replace>
	class Builder;
	template <json::IsBuildType SinkType, str::CodeError Error = str::CodeError::replace>
	class ObjBuilder;
	template <json::IsBuildType SinkType, str::CodeError Error = str::CodeError::replace>
	class ArrBuilder;

	namespace detail {
		template <class SinkType, str::CodeError Error>
		struct BuilderState {
		public:
			struct Instance {
				bool closed = false;
				bool object = false;
			};

		private:
			using ActSink = std::conditional_t<std::is_same_v<SinkType, detail::BuildAnyType>, std::unique_ptr<str::InheritSink>, SinkType&>;

		private:
			detail::Serializer<ActSink, Error> pSerializer;
			std::vector<Instance*> pActive;
			size_t pNextStamp = 0;
			bool pAwaitingValue = false;

		public:
			constexpr BuilderState(ActSink&& sink, std::wstring_view indent) : pSerializer{ std::forward<ActSink>(sink), indent } {}
			constexpr ~BuilderState() {
				/* check if a single value remains (can happen if nothing is ever written out) */
				if (pAwaitingValue)
					pSerializer.primitive(json::Null);
				pAwaitingValue = false;
			}

		private:
			constexpr void fCloseTop() {
				Instance* instance = pActive.back();

				/* write the ending marker out and mark the object as closed */
				pSerializer.end(instance->object);
				pActive.pop_back();
				instance->closed = true;
			}
			constexpr void fCheckStamp(size_t stamp) {
				if (stamp != pNextStamp || !pAwaitingValue)
					throw json::BuilderException(L"Builder is not in an active state");
				pAwaitingValue = false;
			}
			constexpr void fEnsureTopMost(Instance* instance) {
				/* check if a value is currently expected and just write null */
				if (pAwaitingValue) {
					pAwaitingValue = false;
					pSerializer.primitive(json::Null);
				}

				/* close all instances until this is the next active builder */
				while (pActive.back() != instance)
					fCloseTop();
			}

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
			constexpr void fWriteString(const auto& v) {
				pSerializer.primitive(v);
			}
			constexpr void fWritePrimitive(const auto& v) {
				pSerializer.primitive(v);
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
					fWriteString(v);
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
			constexpr bool closed(size_t stamp) const {
				return (stamp != pNextStamp || !pAwaitingValue);
			}
			constexpr void close(Instance* instance) {
				if (instance->closed)
					return;
				fEnsureTopMost(instance);
				fCloseTop();
			}
			constexpr size_t allocFirst() {
				pAwaitingValue = true;
				return ++pNextStamp;
			}
			constexpr size_t allocNext(Instance* instance, const auto& key) {
				/* check if this object is already closed and can therefore not capture the focus anymore and otherwise focus it */
				if (instance->closed)
					throw json::BuilderException(L"Builder is not in an active state");
				fEnsureTopMost(instance);

				/* write the key out and mark the value as awaited */
				if (instance->object)
					pSerializer.objectKey(key);
				else
					pSerializer.arrayValue();
				pAwaitingValue = true;
				return ++pNextStamp;
			}
			constexpr std::unique_ptr<Instance> open(size_t stamp, bool object) {
				fCheckStamp(stamp);

				/* allocate the new instance-object */
				std::unique_ptr<Instance> instance = std::make_unique<Instance>();
				instance->object = object;

				/* write the starting out and push the new instance as active object */
				pSerializer.begin(object);
				pActive.push_back(instance.get());
				return std::move(instance);
			}
			constexpr void next(size_t stamp, const auto& value) {
				fCheckStamp(stamp);
				fWrite(value);
			}
			constexpr void insert(size_t stamp, const auto& value) {
				fCheckStamp(stamp);
				pSerializer.insert(value);
			}
		};

		struct BuildAccess {
			template <class SinkType, str::CodeError Error>
			static json::Builder<SinkType, Error> MakeValue(const std::shared_ptr<detail::BuilderState<SinkType, Error>>& builder, size_t stamp) {
				return json::Builder<SinkType, Error>{ builder, stamp };
			}
			template <class SinkType, str::CodeError Error>
			static json::ObjBuilder<SinkType, Error> MakeObject(const std::shared_ptr<detail::BuilderState<SinkType, Error>>& builder, std::unique_ptr<typename detail::BuilderState<SinkType, Error>::Instance>&& instance) {
				return json::ObjBuilder<SinkType, Error>{ builder, std::move(instance) };
			}
			template <class SinkType, str::CodeError Error>
			static json::ArrBuilder<SinkType, Error> MakeArray(const std::shared_ptr<detail::BuilderState<SinkType, Error>>& builder, std::unique_ptr<typename detail::BuilderState<SinkType, Error>::Instance>&& instance) {
				return json::ArrBuilder<SinkType, Error>{ builder, std::move(instance) };
			}
		};
	}

	/* json-builder of type [value], which can be used to set the currently expected value (if no value is
	*	written to this object, defaults to null, object is volatile and can be passed around, and it will be
	*	closed on close call, when a value is written/prepared, or when a parent object captures the builder)
	*	Note: This is a light-weight object, which can just be copied around, as it keeps a reference to the actual state */
	template <json::IsBuildType SinkType, str::CodeError Error>
	class Builder {
		friend struct detail::BuildAccess;
	private:
		std::shared_ptr<detail::BuilderState<SinkType, Error>> pBuilder;
		size_t pStamp = 0;

	private:
		constexpr Builder() = delete;
		constexpr Builder(const std::shared_ptr<detail::BuilderState<SinkType, Error>>& builder, size_t stamp) : pBuilder{ builder }, pStamp{ stamp } {}

	public:
		constexpr Builder(const json::Builder<SinkType, Error>&) = default;
		constexpr Builder(json::Builder<SinkType, Error>&&) = default;
		constexpr json::Builder<SinkType, Error>& operator=(const json::Builder<SinkType, Error>&) = default;
		constexpr json::Builder<SinkType, Error>& operator=(json::Builder<SinkType, Error>&&) = default;
		constexpr ~Builder() = default;

	public:
		json::Builder<SinkType, Error>& operator=(const json::IsJson auto& v) {
			Builder<SinkType, Error>::set(v);
			return *this;
		}

	public:
		/* check if this value is done (i.e. a valid value has been assigned to it/corresponding constructor has been created) */
		bool closed() const {
			return pBuilder->closed(pStamp);
		}

		/* assign the json-like object to this value (closes this object) */
		void set(const json::IsJson auto& v) {
			pBuilder->next(pStamp, v);
		}

		/* assign the well formed json-value to this value (closes this object, is
		*	not validated, caller must ensure its a single well formatted value) */
		void setJson(const json::IsString auto& v) {
			pBuilder->insert(pStamp, v);
		}

		/* mark this object and being an object and return the corresponding builder (closes this object) */
		json::ObjBuilder<SinkType, Error> obj() {
			auto instance = pBuilder->open(pStamp, true);
			return detail::BuildAccess::MakeObject<SinkType, Error>(pBuilder, std::move(instance));
		}

		/* mark this object and being an array and return the corresponding builder (closes this object) */
		json::ArrBuilder<SinkType, Error> arr() {
			auto instance = pBuilder->open(pStamp, false);
			return detail::BuildAccess::MakeArray<SinkType, Error>(pBuilder, std::move(instance));
		}
	};

	/* json-builder of type [object], which can be used to write key-value pairs to the corresponding object and to the
	*	sink (this builder does not prevent already used keys to be used again, it will write all used keys out, object
	*	will be closed once close is called, the object is destructed, or a parent object captures the builder)
	*	Note: Although this is a light-weight object, it can only be moved around, as it references the current progress of the building */
	template <json::IsBuildType SinkType, str::CodeError Error>
	class ObjBuilder {
		friend struct detail::BuildAccess;
	private:
		std::shared_ptr<detail::BuilderState<SinkType, Error>> pBuilder;
		std::unique_ptr<typename detail::BuilderState<SinkType, Error>::Instance> pInstance;

	private:
		ObjBuilder() = delete;
		ObjBuilder(const std::shared_ptr<detail::BuilderState<SinkType, Error>>& builder, std::unique_ptr<typename detail::BuilderState<SinkType, Error>::Instance>&& instance) : pBuilder{ builder }, pInstance{ std::move(instance) } {}

	public:
		constexpr ObjBuilder(const json::ObjBuilder<SinkType, Error>&) = delete;
		constexpr ObjBuilder(json::ObjBuilder<SinkType, Error>&&) = default;
		constexpr json::ObjBuilder<SinkType, Error>& operator=(const json::ObjBuilder<SinkType, Error>&) = delete;
		constexpr json::ObjBuilder<SinkType, Error>& operator=(json::ObjBuilder<SinkType, Error>&&) = default;
		constexpr ~ObjBuilder() {
			if (pInstance.get() != 0)
				pBuilder->close(pInstance.get());
		}

	public:
		json::Builder<SinkType, Error> operator[](const json::IsString auto& k) {
			return ObjBuilder<SinkType, Error>::addVal(k);
		}

	public:
		/* check if this object is done (i.e. the closing bracket has been written out) */
		bool closed() const {
			return pInstance->closed;
		}

		/* close any nested children and this object */
		void close() {
			pBuilder->close(pInstance.get());
		}

		/* add a new value using the given key and return the value-builder to it */
		json::Builder<SinkType, Error> addVal(const json::IsString auto& k) {
			size_t stamp = pBuilder->allocNext(pInstance.get(), k);
			return detail::BuildAccess::MakeValue<SinkType, Error>(pBuilder, stamp);
		}

		/* add a new array using the given key and return the array-builder to it */
		json::ArrBuilder<SinkType, Error> addArr(const json::IsString auto& k) {
			size_t stamp = pBuilder->allocNext(pInstance.get(), k);
			auto instance = pBuilder->open(stamp, false);
			return detail::BuildAccess::MakeArray<SinkType, Error>(pBuilder, std::move(instance));
		}

		/* add a new object using the given key and return the object-builder to it */
		json::ObjBuilder<SinkType, Error> addObj(const json::IsString auto& k) {
			size_t stamp = pBuilder->allocNext(pInstance.get(), k);
			auto instance = pBuilder->open(stamp, true);
			return detail::BuildAccess::MakeObject<SinkType, Error>(pBuilder, std::move(instance));
		}

		/* add a new json-like object using the given key */
		void add(const json::IsString auto& k, const json::IsJson auto& v) {
			size_t stamp = pBuilder->allocNext(pInstance.get(), k);
			pBuilder->next(stamp, v);
		}

		/* push a well formed json-value (is not validated, caller must ensure its a single well formatted value) */
		void pushJson(const json::IsString auto& k, const json::IsString auto& v) {
			size_t stamp = pBuilder->allocNext(pInstance.get(), k);
			pBuilder->insert(stamp, v);
		}
	};

	/* json-builder of type [array], which can be used to push values to the the corresponding array and to the sink
	*	(array will be closed once close is called, the array is destructed, or a parent object captures the builder)
	*	Note: Although this is a light-weight object, it can only be moved around, as it references the current progress of the building */
	template <json::IsBuildType SinkType, str::CodeError Error>
	class ArrBuilder {
		friend struct detail::BuildAccess;
	private:
		std::shared_ptr<detail::BuilderState<SinkType, Error>> pBuilder;
		std::unique_ptr<typename detail::BuilderState<SinkType, Error>::Instance> pInstance;

	private:
		ArrBuilder() = delete;
		ArrBuilder(const std::shared_ptr<detail::BuilderState<SinkType, Error>>& builder, std::unique_ptr<typename detail::BuilderState<SinkType, Error>::Instance>&& instance) : pBuilder{ builder }, pInstance{ std::move(instance) } {}

	public:
		constexpr ArrBuilder(const json::ArrBuilder<SinkType, Error>&) = delete;
		constexpr ArrBuilder(json::ArrBuilder<SinkType, Error>&&) = default;
		constexpr json::ArrBuilder<SinkType, Error>& operator=(const json::ArrBuilder<SinkType, Error>&) = delete;
		constexpr json::ArrBuilder<SinkType, Error>& operator=(json::ArrBuilder<SinkType, Error>&&) = default;
		constexpr ~ArrBuilder() {
			if (pInstance.get() != 0)
				pBuilder->close(pInstance.get());
		}

	public:
		/* check if this object is done (i.e. the closing bracket has been written out) */
		bool closed() const {
			return pInstance->closed;
		}

		/* close any nested children and this object */
		void close() {
			pBuilder->close(pInstance.get());
		}

		/* push a new value and return the value-builder to it */
		json::Builder<SinkType, Error> pushVal() {
			size_t stamp = pBuilder->allocNext(pInstance.get(), L"");
			return detail::BuildAccess::MakeValue<SinkType, Error>(pBuilder, stamp);
		}

		/* push a new array and return the array-builder to it */
		json::ArrBuilder<SinkType, Error> pushArr() {
			size_t stamp = pBuilder->allocNext(pInstance.get(), L"");
			auto instance = pBuilder->open(stamp, false);
			return detail::BuildAccess::MakeArray<SinkType, Error>(pBuilder, std::move(instance));
		}

		/* push a new object and return the object-builder to it */
		json::ObjBuilder<SinkType, Error> pushObj() {
			size_t stamp = pBuilder->allocNext(pInstance.get(), L"");
			auto instance = pBuilder->open(stamp, true);
			return detail::BuildAccess::MakeObject<SinkType, Error>(pBuilder, std::move(instance));
		}

		/* push a new json-like object */
		void push(const json::IsJson auto& v) {
			size_t stamp = pBuilder->allocNext(pInstance.get(), L"");
			pBuilder->next(stamp, v);
		}

		/* push a well formed json-value (is not validated, caller must ensure its a single well formatted value) */
		void pushJson(const json::IsString auto& v) {
			size_t stamp = pBuilder->allocNext(pInstance.get(), L"");
			pBuilder->insert(stamp, v);
		}
	};

	/* construct a json builder-value to the given sink, using the corresponding indentation, which writes all
	*	values out immediately, preventing an intermediate state from being created (indentation will be sanitized
	*	to only contain spaces and tabs, if indentation is empty, a compact json stream will be produced)
	*	Note: Can be used to write already well formatted json value strings to the output stream
	*	Note: Must not outlive the sink as it stores a reference to it */
	template <str::IsSink SinkType, str::CodeError Error = str::CodeError::replace>
	constexpr json::Builder<std::remove_cvref_t<SinkType>, Error> Build(SinkType& sink, std::wstring_view indent = L"\t") {
		using ActSink = std::remove_cvref_t<SinkType>;

		/* setup the shared state and setup the root value */
		auto state = std::make_shared<detail::BuilderState<ActSink, Error>>(sink, indent);
		return detail::BuildAccess::MakeValue<ActSink, Error>(state, state->allocFirst());
	}

	/* same as json::Builder, but uses inheritance to hide the underlying sink-type
	*	Note: This is a light-weight object, which can just be copied around, as it keeps a reference to the actual state */
	using AnyBuilder = json::Builder<detail::BuildAnyType, str::CodeError::replace>;

	/* same as json::ObjBuilder, but uses inheritance to hide the underlying sink-type
	*	Note: Although this is a light-weight object, it can only be moved around, as it references the current progress of the building */
	using AnyObjBuilder = json::ObjBuilder<detail::BuildAnyType, str::CodeError::replace>;

	/* same as json::ArrBuilder, but uses inheritance to hide the underlying sink-type
	*	Note: Although this is a light-weight object, it can only be moved around, as it references the current progress of the building */
	using AnyArrBuilder = json::ArrBuilder<detail::BuildAnyType, str::CodeError::replace>;

	/* construct a json any-builder-value to the given sink, using the corresponding indentation but hide
	*	the actual sink-type by using inheritance internally, and is otherwise equivalent to json::Build (uses Error = str::CodeError::replace)
	*	Note: Must not outlive the sink as it stores a reference to it */
	template <str::IsSink SinkType>
	json::AnyBuilder BuildAny(SinkType& sink, std::wstring_view indent = L"\t") {
		using ActSink = std::remove_cvref_t<SinkType>;

		/* wrap the sink to be held by the builder */
		std::unique_ptr<str::InheritSink> buildSink = std::make_unique<str::SinkImplementation<ActSink>>(sink);

		/* setup the shared state and setup the root value */
		auto state = std::make_shared<detail::BuilderState<detail::BuildAnyType, str::CodeError::replace>>(std::move(buildSink), indent);
		return detail::BuildAccess::MakeValue<detail::BuildAnyType, str::CodeError::replace>(state, state->allocFirst());
	}
}
