#include "Engine/ApplicationInterface.h"

namespace Lime
{
	namespace
	{
		// Used when a project does not declare an application, so the engine never null checks.
		class FDefaultApplication final : public ILimeApplication
		{
		};
	} // namespace

	FApplicationFactory::FFactory& FApplicationFactory::GetStorage()
	{
		// Function local static: safe regardless of translation unit initialization order.
		static FFactory Factory;
		return Factory;
	}

	void FApplicationFactory::Set(FFactory Factory)
	{
		GetStorage() = std::move(Factory);
	}

	std::unique_ptr<ILimeApplication> FApplicationFactory::Create()
	{
		const FFactory& Factory = GetStorage();
		if (Factory != nullptr)
		{
			if (std::unique_ptr<ILimeApplication> Application = Factory())
			{
				return Application;
			}
		}
		return std::make_unique<FDefaultApplication>();
	}
} // namespace Lime
