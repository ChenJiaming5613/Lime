#include "RenderGraph/RenderGraphInjection.h"

#include <utility>

namespace Lime
{
	namespace
	{
		FRenderGraphIssue MakeIssue(FRenderGraphIssue::ESeverity Severity, std::string Message)
		{
			FRenderGraphIssue Issue;
			Issue.Severity = Severity;
			Issue.Message = std::move(Message);
			return Issue;
		}

		// Finds or creates the compiled resource standing for an engine supplied target.
		//
		// Shared by import name, so two injected passes writing "$BackBuffer" end up bound to one resource
		// rather than two that happen to resolve to the same texture. That matters for the load action: the
		// second writer has to be able to see that something already wrote the target this frame.
		SizeType FindOrAddImportedResource(FRenderGraphCompileResult& Compiled, const FRenderGraphResourceDesc& Desc,
		       const std::string& ImportName)
		{
			for (SizeType Index = 0; Index < Compiled.Resources.size(); ++Index)
			{
				const FCompiledResource& Existing = Compiled.Resources[Index];
				if (Existing.Source == ERenderGraphResourceSource::Imported && Existing.ImportName == ImportName)
				{
					return Index;
				}
			}

			FCompiledResource Resource;
			// Named after the slot rather than after the producing field, because an imported resource has no
			// single producer: it is the engine's, and several passes may write it in turn.
			Resource.Name = ImportName;
			Resource.Kind = Desc.Kind;
			Resource.Format = Desc.Format;
			Resource.Width = Desc.Width;
			Resource.Height = Desc.Height;
			Resource.bIsDepth = Desc.IsDepth();
			Resource.bUsedAsRenderTarget = true;
			Resource.Source = ERenderGraphResourceSource::Imported;
			Resource.ImportName = ImportName;
			Compiled.Resources.push_back(std::move(Resource));
			return Compiled.Resources.size() - 1;
		}
	} // namespace

	void InjectRenderGraphPasses(FRenderGraphCompileResult& Compiled, const std::vector<FRenderGraphInjection>& Injections,
	        const FRenderGraphPassTypeRegistry& Types)
	{
		if (Injections.empty())
		{
			return;
		}

		// A graph that failed to compile has no execution order and no outputs to bind to. Injecting into it
		// would produce a result that looks partially runnable, which is the state the compiler goes out of
		// its way never to return.
		if (!Compiled.bSucceeded)
		{
			return;
		}

		for (const FRenderGraphInjection& Injection : Injections)
		{
			const FRenderGraphPassTypeDesc* Type = Types.Find(Injection.TypeName);
			if (Type == nullptr)
			{
				Compiled.Issues.push_back(MakeIssue(FRenderGraphIssue::ESeverity::Warning,
				        "Injected pass '" + Injection.PassName + "' has unknown type '" +
				     Injection.TypeName + "' and was skipped."));
				continue;
			}

			FCompiledPass Pass;
			Pass.PassName = Injection.PassName;
			Pass.TypeName = Injection.TypeName;
			Pass.bInjected = true;
			Pass.bBeginsNewSubmission = Injection.bBeginsNewSubmission;

			// Outputs first, matching the order the compiler itself emits bindings in, so a reader sees an
			// injected pass laid out the same way as a described one.
			for (const FRenderGraphResourceDesc& Output : Type->Outputs)
			{
				if (Output.Source != ERenderGraphResourceSource::Imported)
				{
					// A transient output on an injected pass has nobody to read it: the injection runs last and
					// the file cannot name it. Allocating it would be memory for a resource with no reader.
					Compiled.Issues.push_back(MakeIssue(FRenderGraphIssue::ESeverity::Warning,
					        "Injected pass '" + Injection.PassName + "' declares transient output '" +
					     Output.Name + "', which nothing can read; it was not bound."));
					continue;
				}

				// The pass may name the slot itself; the injection's target is the fallback, which is what lets
				// one pass type be injected against different targets.
				const std::string ImportName = !Output.ImportName.empty() ? Output.ImportName : Injection.ImportedTarget;
				if (ImportName.empty())
				{
					Compiled.Issues.push_back(MakeIssue(FRenderGraphIssue::ESeverity::Warning,
					        "Injected pass '" + Injection.PassName + "' output '" + Output.Name +
					     "' is imported but no target was named; it was not bound."));
					continue;
				}

				const SizeType ResourceIndex = FindOrAddImportedResource(Compiled, Output, ImportName);

				FCompiledPassBinding Binding;
				Binding.FieldName = Output.Name;
				Binding.ResourceIndex = ResourceIndex;
				Binding.Visibility = ERenderGraphResourceVisibility::Output;
				// Resolved here rather than at execution time: the pass declares what it wants, and an
				// unspecified action on an imported target means load, since the engine may already have drawn
				// into it this frame.
				Binding.LoadAction = Output.LoadAction != ERenderGraphLoadAction::Unspecified ? Output.LoadAction
				: ERenderGraphLoadAction::Load;
				Pass.Bindings.push_back(std::move(Binding));
			}

			// The input is optional by construction: a graph that produced no output still has to leave the
			// editor drawable, so an unbound slot is a normal state rather than a failure.
			if (Injection.InputFromOutputSlot >= 0 && !Type->Inputs.empty())
			{
				const SizeType Slot = static_cast<SizeType>(Injection.InputFromOutputSlot);
				if (Slot < Compiled.OutputResourceIndices.size())
				{
					const std::string& FieldName =
					    !Injection.InputFieldName.empty() ? Injection.InputFieldName : Type->Inputs.front().Name;

					if (Type->FindInput(FieldName) != nullptr)
					{
						const SizeType ResourceIndex = Compiled.OutputResourceIndices[Slot];
						// The graph output is now read by a pass, so it needs the shader resource bind flag it
						// would not otherwise have had: before injection its only consumer was a copy.
						Compiled.Resources[ResourceIndex].bUsedAsShaderResource = true;

						FCompiledPassBinding Binding;
						Binding.FieldName = FieldName;
						Binding.ResourceIndex = ResourceIndex;
						Binding.Visibility = ERenderGraphResourceVisibility::Input;
						Pass.Bindings.push_back(std::move(Binding));
					}
					else
					{
						Compiled.Issues.push_back(MakeIssue(FRenderGraphIssue::ESeverity::Warning,
						        "Injected pass '" + Injection.PassName + "' has no input named '" +
						     FieldName + "'; it runs unbound."));
					}
				}
			}

			Compiled.ExecutionOrder.push_back(std::move(Pass));
		}
	}
} // namespace Lime
