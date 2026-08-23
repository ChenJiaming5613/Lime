#include "Editor/TestEngine/UITestRegistry.h"

#include "Core/Logging/LogManager.h"

#include <algorithm>

namespace Lime
{
	FUITestRegistry& FUITestRegistry::Get()
	{
		// Function local static: safe regardless of translation unit initialization order, which
		// matters because projects register from static initializers.
		static FUITestRegistry Instance;
		return Instance;
	}

	void FUITestRegistry::Register(FUITestRegistration Registration)
	{
		if (Registration.Category.empty() || Registration.Name.empty())
		{
			LIME_LOG_ERROR(LIME_LOG_CATEGORY_EDITOR, "A UI test needs both a category and a name; ignoring the registration");
			return;
		}
		if (Registration.TestFunc == nullptr)
		{
			LIME_LOG_ERROR(LIME_LOG_CATEGORY_EDITOR, "UI test '{}' has no test function; ignoring it", Registration.GetQualifiedName());
			return;
		}

		const auto Existing = std::find_if(Registrations.begin(), Registrations.end(),
		                                   [&Registration](const FUITestRegistration& Candidate)
		                                   {
			                                   return Candidate.Category == Registration.Category &&
			                                          Candidate.Name == Registration.Name;
		                                   });

		// Replacing rather than appending lets a project override a built-in test, and keeps the
		// name unique so the automation API can address a test unambiguously.
		if (Existing != Registrations.end())
		{
			LIME_LOG_INFO(LIME_LOG_CATEGORY_EDITOR, "UI test '{}' replaces an earlier registration", Registration.GetQualifiedName());
			*Existing = std::move(Registration);
			return;
		}

		Registrations.push_back(std::move(Registration));
	}

	bool RegisterUITest(const char* Category, const char* Name, FUITestFuncPtr TestFunc) noexcept
	{
		// noexcept because this runs from a static initializer, where an escaping exception could not
		// be caught by anything and would terminate before main. A failed allocation here is not
		// recoverable anyway, so turning it into a crash at the point of failure is the honest
		// outcome and keeps the registration sites free of error handling.
		FUITestRegistration Registration;
		Registration.Category = Category != nullptr ? Category : "";
		Registration.Name = Name != nullptr ? Name : "";
		Registration.TestFunc = TestFunc;
		FUITestRegistry::Get().Register(std::move(Registration));
		return true;
	}
} // namespace Lime
