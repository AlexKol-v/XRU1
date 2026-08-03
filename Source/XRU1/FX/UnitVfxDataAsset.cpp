#include "UnitVfxDataAsset.h"

#include "XRU1Log.h" // прогрев эффектов пишет в проектную категорию, а не в LogTemp
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "NiagaraTypes.h"

namespace
{
	/**
	 * Пишет векторный user-параметр, СПРОСИВ ЕГО ТИП у самой системы: в UE5
	 * мировая точка — это тип Position (LWC), а не Vector. Сеттер не того типа
	 * молча заводит лишний параметр, который никто не читает, — то есть эффект
	 * выглядит так, будто мы вообще ничего не задали.
	 */
	void SetVectorLikeParameter(UNiagaraComponent* Component, FName ParameterName, const FVector& Value)
	{
		if (!Component || ParameterName.IsNone())
		{
			return;
		}

		bool bIsPosition = true;
		if (const UNiagaraSystem* System = Component->GetAsset())
		{
			TArray<FNiagaraVariable> Parameters;
			System->GetExposedParameters().GetParameters(Parameters);

			const FString Requested = ParameterName.ToString();
			// В сторе имена лежат с префиксом `User.`, а в ассете дизайнер может
			// написать и с ним, и без — принимаем оба написания.
			const FString Prefixed = Requested.StartsWith(TEXT("User."))
				? Requested : FString::Printf(TEXT("User.%s"), *Requested);
			for (const FNiagaraVariable& Parameter : Parameters)
			{
				const FString Candidate = Parameter.GetName().ToString();
				if (Candidate == Requested || Candidate == Prefixed)
				{
					bIsPosition = Parameter.GetType() == FNiagaraTypeDefinition::GetPositionDef();
					break;
				}
			}
		}

		if (bIsPosition)
		{
			Component->SetVariablePosition(ParameterName, Value);
		}
		else
		{
			Component->SetVariableVec3(ParameterName, Value);
		}
	}
}

UNiagaraSystem* UUnitVfxDataAsset::FindImpact(EPhysicalSurface Surface) const
{
	if (const TObjectPtr<UNiagaraSystem>* Found = ImpactBySurface.Find(Surface))
	{
		if (*Found)
		{
			return *Found;
		}
	}
	return DefaultImpact;
}

void UUnitVfxDataAsset::ApplyTracerParameters(UNiagaraComponent* Component,
	const FVector& Start, const FVector& End) const
{
	if (!Component)
	{
		return;
	}

	SetVectorLikeParameter(Component, TracerStartParameter, Start);
	SetVectorLikeParameter(Component, TracerEndParameter, End);
	if (!TracerSpeedParameter.IsNone())
	{
		Component->SetVariableFloat(TracerSpeedParameter, TracerSpeed);
	}
	if (!TracerTrailDurationParameter.IsNone())
	{
		Component->SetVariableFloat(TracerTrailDurationParameter, TracerTrailDuration);
	}
}

void UUnitVfxDataAsset::ApplyImpactParameters(UNiagaraComponent* Component,
	const FVector& Normal, const FVector& Direction) const
{
	if (!Component)
	{
		return;
	}

	SetVectorLikeParameter(Component, ImpactNormalParameter, Normal);
	SetVectorLikeParameter(Component, ImpactDirectionParameter, Direction);
}

void UUnitVfxDataAsset::WarmUpEffects()
{
#if WITH_EDITORONLY_DATA
	const double StartTime = FPlatformTime::Seconds();
	int32 WarmedCount = 0;

	auto WarmUpSystem = [&WarmedCount](UNiagaraSystem* System)
	{
		if (System)
		{
			// Блокирующее ожидание — здесь это и нужно: цена компиляции должна
			// быть уплачена ЗДЕСЬ, а не в кадре первого выстрела.
			System->WaitForCompilationComplete(/*bIncludingGPUShaders=*/false,
				/*bShowProgress=*/false);
			++WarmedCount;
		}
	};

	WarmUpSystem(MuzzleFlash);
	WarmUpSystem(Tracer);
	WarmUpSystem(ImpactFlesh);
	WarmUpSystem(DefaultImpact);
	for (const TPair<TEnumAsByte<EPhysicalSurface>, TObjectPtr<UNiagaraSystem>>& Pair : ImpactBySurface)
	{
		WarmUpSystem(Pair.Value);
	}

	const double Elapsed = FPlatformTime::Seconds() - StartTime;
	if (Elapsed > 0.01)
	{
		// Печатаем только заметный прогрев: это ровно та задержка, которая
		// раньше приходилась на первый выстрел.
		UE_LOG(LogXRU1Combat, Display,
			TEXT("[VFX] Прогрев профиля %s: %d систем за %.2f с (иначе это ждал бы первый выстрел)"),
			*GetName(), WarmedCount, Elapsed);
	}
#endif
}
