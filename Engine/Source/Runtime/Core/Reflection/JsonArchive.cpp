#include "Core/Reflection/JsonArchive.h"

#include "Core/Logging/LogManager.h"
#include "Core/Math/Vector.h"

namespace Lime
{
	namespace
	{
		// Vectors are stored as arrays so the JSON stays compact and diff friendly.
		template<typename VectorType, int32 ComponentCount>
		bool SaveVector(entt::meta_any& Value, FJson& OutJson)
		{
			const VectorType* Vector = Value.try_cast<VectorType>();
			if (Vector == nullptr)
			{
				return false;
			}

			const float* Components = &Vector->X;
			OutJson = FJson::array();
			for (int32 Index = 0; Index < ComponentCount; ++Index)
			{
				OutJson.push_back(Components[Index]);
			}
			return true;
		}

		template<typename VectorType, int32 ComponentCount>
		bool LoadVector(entt::meta_any& Value, const FJson& InJson)
		{
			VectorType* Vector = Value.try_cast<VectorType>();
			if (Vector == nullptr)
			{
				return false;
			}
			if (!InJson.is_array() || InJson.size() != static_cast<SizeType>(ComponentCount))
			{
				return false;
			}

			float* Components = &Vector->X;
			for (int32 Index = 0; Index < ComponentCount; ++Index)
			{
				if (!InJson[static_cast<SizeType>(Index)].is_number())
				{
					return false;
				}
				Components[Index] = InJson[static_cast<SizeType>(Index)].get<float>();
			}
			return true;
		}

		bool ShouldSkip(const entt::meta_data& Data)
		{
			const FPropertyMeta* Meta = GetPropertyMeta(Data);
			return Meta != nullptr && Meta->bTransient;
		}
	} // namespace

	void FJsonArchive::SaveFields(const entt::meta_type& MetaType, void* Instance, FJson& OutJson)
	{
		if (!MetaType || Instance == nullptr)
		{
			return;
		}

		// from_void yields a reference wrapper, which is what meta_data::get expects as its instance.
		entt::meta_any Owner = MetaType.from_void(Instance);

		OutJson = FJson::object();
		for (auto&& [Id, Data] : MetaType.data())
		{
			const char* Name = Data.name();
			if (Name == nullptr || ShouldSkip(Data))
			{
				continue;
			}

			entt::meta_any Value = Data.get(Owner);
			FJson FieldJson;
			if (SaveValue(Value, FieldJson))
			{
				OutJson[Name] = std::move(FieldJson);
			}
			else
			{
				LIME_LOG_WARNING(LIME_LOG_CATEGORY_CORE, "Field '{}' has an unsupported type and was not serialized", Name);
			}
		}
	}

	void FJsonArchive::LoadFields(const entt::meta_type& MetaType, void* Instance, const FJson& InJson)
	{
		if (!MetaType || Instance == nullptr || !InJson.is_object())
		{
			return;
		}

		entt::meta_any Owner = MetaType.from_void(Instance);

		for (auto&& [Id, Data] : MetaType.data())
		{
			const char* Name = Data.name();
			if (Name == nullptr || ShouldSkip(Data))
			{
				continue;
			}

			const auto Iterator = InJson.find(Name);
			if (Iterator == InJson.end())
			{
				// Absent fields keep their current value, so partial files are valid.
				continue;
			}

			// as_ref_t makes this an alias to the real field, so writing through it mutates Instance.
			entt::meta_any Value = Data.get(Owner);
			if (!LoadValue(Value, *Iterator))
			{
				LIME_LOG_WARNING(LIME_LOG_CATEGORY_CORE, "Field '{}' could not be read from JSON; keeping the current value", Name);
			}
		}
	}

	bool FJsonArchive::SaveValue(entt::meta_any& Value, FJson& OutJson)
	{
		if (const bool* BoolValue = Value.try_cast<bool>())
		{
			OutJson = *BoolValue;
			return true;
		}
		if (const float* FloatValue = Value.try_cast<float>())
		{
			OutJson = *FloatValue;
			return true;
		}
		if (const int32* IntValue = Value.try_cast<int32>())
		{
			OutJson = *IntValue;
			return true;
		}
		if (const uint32* UIntValue = Value.try_cast<uint32>())
		{
			OutJson = *UIntValue;
			return true;
		}
		if (const std::string* StringValue = Value.try_cast<std::string>())
		{
			OutJson = *StringValue;
			return true;
		}

		return SaveVector<FVector2, 2>(Value, OutJson) || SaveVector<FVector3, 3>(Value, OutJson) ||
		       SaveVector<FVector4, 4>(Value, OutJson);
	}

	bool FJsonArchive::LoadValue(entt::meta_any& Value, const FJson& InJson)
	{
		if (bool* BoolValue = Value.try_cast<bool>())
		{
			if (!InJson.is_boolean())
			{
				return false;
			}
			*BoolValue = InJson.get<bool>();
			return true;
		}
		if (float* FloatValue = Value.try_cast<float>())
		{
			if (!InJson.is_number())
			{
				return false;
			}
			*FloatValue = InJson.get<float>();
			return true;
		}
		if (int32* IntValue = Value.try_cast<int32>())
		{
			if (!InJson.is_number_integer())
			{
				return false;
			}
			*IntValue = InJson.get<int32>();
			return true;
		}
		if (uint32* UIntValue = Value.try_cast<uint32>())
		{
			if (!InJson.is_number_integer() || InJson.get<int64>() < 0)
			{
				return false;
			}
			*UIntValue = InJson.get<uint32>();
			return true;
		}
		if (std::string* StringValue = Value.try_cast<std::string>())
		{
			if (!InJson.is_string())
			{
				return false;
			}
			*StringValue = InJson.get<std::string>();
			return true;
		}

		return LoadVector<FVector2, 2>(Value, InJson) || LoadVector<FVector3, 3>(Value, InJson) || LoadVector<FVector4, 4>(Value, InJson);
	}

	const char* FJsonArchive::GetValueTypeName(const entt::meta_any& Value)
	{
		// try_cast needs a non-const any, and this only inspects the type.
		entt::meta_any& Mutable = const_cast<entt::meta_any&>(Value);

		if (Mutable.try_cast<bool>() != nullptr)
		{
			return "bool";
		}
		if (Mutable.try_cast<float>() != nullptr)
		{
			return "float";
		}
		if (Mutable.try_cast<int32>() != nullptr)
		{
			return "int32";
		}
		if (Mutable.try_cast<uint32>() != nullptr)
		{
			return "uint32";
		}
		if (Mutable.try_cast<std::string>() != nullptr)
		{
			return "string";
		}
		if (Mutable.try_cast<FVector2>() != nullptr)
		{
			return "vector2";
		}
		if (Mutable.try_cast<FVector3>() != nullptr)
		{
			return "vector3";
		}
		if (Mutable.try_cast<FVector4>() != nullptr)
		{
			return "vector4";
		}
		return "unsupported";
	}
} // namespace Lime
