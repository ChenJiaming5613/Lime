// Reflection to JSON bridge for automation.
//
// FJsonArchive already moves reflected values in and out of JSON; this adds what a remote client
// needs on top: the field metadata (widget, range, tooltip) so a script can discover what is
// tunable, and single field writes so a command can change one property without sending the rest.

#pragma once

#include "Core/Json/JsonUtils.h"
#include "Renderer/RenderTypes.h"

#include <string>

namespace Lime
{
	class FAutomationReflection
	{
	public:
		// Describes every reflected field: name, type, current value and its metadata.
		static FJson DescribeFields(const FReflectedRef& Ref);

		// Current values only, in the same shape FJsonArchive produces.
		static FJson ReadValues(const FReflectedRef& Ref);

		// Writes one field. Returns false and fills OutError when the field is absent, read only or
		// the value does not fit its type.
		static bool WriteField(const FReflectedRef& Ref, const std::string& FieldName, const FJson& Value, std::string& OutError);

		// Writes several fields, stopping at the first failure. Names that were applied are appended
		// to OutApplied, which lets the caller report a partial success precisely.
		static bool WriteFields(const FReflectedRef& Ref, const FJson& Values, std::vector<std::string>& OutApplied, std::string& OutError);
	};
} // namespace Lime
