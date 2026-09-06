#include "RenderGraph/RenderGraphTypes.h"

namespace Lime
{
	namespace
	{
		// Reconciles one property. Returns false and describes the clash when both ends specified a value
		// and the values differ.
		//
		// Templated over the value because the rule is identical for formats and for dimensions: the
		// "unspecified" sentinel differs, so it is passed in rather than assumed.
		template<typename ValueType>
		bool MergeProperty(ValueType& InOut, ValueType Other, ValueType Unspecified, const char* PropertyName,
		                   const std::string& ContextName, std::string& OutMessage)
		{
			if (Other == Unspecified)
			{
				return true;
			}

			if (InOut == Unspecified)
			{
				InOut = Other;
				return true;
			}

			if (InOut != Other)
			{
				OutMessage = "'" + ContextName + "' is connected to endpoints that disagree on " + PropertyName + ".";
				return false;
			}

			return true;
		}
	} // namespace

	bool FRenderGraphResourceDesc::IsDepth() const
	{
		if (Format == nvrhi::Format::UNKNOWN)
		{
			return false;
		}

		// Asked of nvrhi rather than matched against a list of depth formats here, so the answer cannot
		// drift from what the RHI actually does with the format.
		return nvrhi::getFormatInfo(Format).hasDepth;
	}

	bool MergeResourceDesc(FRenderGraphResourceDesc& InOut, const FRenderGraphResourceDesc& Other, const std::string& ContextName,
	                       FRenderGraphIssue& OutIssue)
	{
		// A texture and a buffer cannot be the same resource, so this is a conflict rather than something
		// to reconcile. There is no "unspecified" kind: every declaration names one.
		if (InOut.Kind != Other.Kind)
		{
			OutIssue.Severity = FRenderGraphIssue::ESeverity::Error;
			OutIssue.Message = "'" + ContextName + "' is connected to endpoints that disagree on resource kind.";
			return false;
		}

		std::string Message;
		if (!MergeProperty(InOut.Format, Other.Format, nvrhi::Format::UNKNOWN, "format", ContextName, Message) ||
		    !MergeProperty(InOut.Width, Other.Width, 0u, "width", ContextName, Message) ||
		    !MergeProperty(InOut.Height, Other.Height, 0u, "height", ContextName, Message))
		{
			OutIssue.Severity = FRenderGraphIssue::ESeverity::Error;
			OutIssue.Message = std::move(Message);
			return false;
		}

		// Transient is the unspecified value here, not a claim. A pass reading a resource has no opinion on
		// who allocated it, so a transient reader against an imported producer keeps the import rather than
		// being treated as a disagreement: ownership is a statement about the producer alone.
		if (Other.Source == ERenderGraphResourceSource::Imported)
		{
			InOut.Source = ERenderGraphResourceSource::Imported;
			if (InOut.ImportName.empty())
			{
				InOut.ImportName = Other.ImportName;
			}
			else if (!Other.ImportName.empty() && InOut.ImportName != Other.ImportName)
			{
				// Two different engine slots for one resource has no meaning: the resource is one texture, and
				// which one the engine binds could not be decided.
				OutIssue.Severity = FRenderGraphIssue::ESeverity::Error;
				OutIssue.Message = "'" + ContextName + "' is connected to endpoints that name different imported targets.";
				return false;
			}
		}

		// The load action belongs to the writing end, so a reader never overrides one that was set. This is
		// not a merge of equals: an input has no contents to preserve or discard.
		if (InOut.LoadAction == ERenderGraphLoadAction::Unspecified)
		{
			InOut.LoadAction = Other.LoadAction;
		}

		// The description is documentation rather than a requirement, so the first non-empty one wins
		// instead of a mismatch being reported.
		if (InOut.Description.empty())
		{
			InOut.Description = Other.Description;
		}

		return true;
	}
} // namespace Lime
