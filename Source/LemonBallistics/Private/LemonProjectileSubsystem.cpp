#include "LemonProjectileSubsystem.h"

#include "LemonProjectileDefinition.h"

#include "DrawDebugHelpers.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(LemonProjectileSubsystem)

#if ENABLE_DRAW_DEBUG
namespace LemonProjectileDebug
{
	static TAutoConsoleVariable<int32> CVarDebugArc(
		TEXT("lemon.Projectile.DebugArc"), 0,
		TEXT("Draw projectile flight arcs, colored by speed (0=off, 1=on). Authoritative bullets draw thicker."),
		ECVF_Cheat);

	static TAutoConsoleVariable<float> CVarDebugArcDuration(
		TEXT("lemon.Projectile.DebugArcDuration"), 2.0f,
		TEXT("Seconds each projectile arc segment persists on screen."),
		ECVF_Cheat);

	static TAutoConsoleVariable<float> CVarDebugMaxSpeed(
		TEXT("lemon.Projectile.DebugMaxSpeed"), 0.0f,
		TEXT("Speed mapped to the hot (red) end of the arc gradient. 0 = use each bullet's own launch speed."),
		ECVF_Cheat);

	static TAutoConsoleVariable<int32> CVarDebugHits(
		TEXT("lemon.Projectile.DebugHits"), 0,
		TEXT("Draw a marker + info text at projectile impacts (0=off, 1=on)."),
		ECVF_Cheat);

	static TAutoConsoleVariable<int32> CVarDebugStats(
		TEXT("lemon.Projectile.DebugStats"), 0,
		TEXT("Show live projectile counts on screen (0=off, 1=on)."),
		ECVF_Cheat);

	/// Maps a speed to a blue(slow) -> cyan -> green -> yellow -> red(fast) gradient.
	static FColor SpeedToColor(const float Speed, const float MaxSpeed)
	{
		const float Denominator{MaxSpeed > UE_KINDA_SMALL_NUMBER ? MaxSpeed : 1.0f};
		const float T{FMath::Clamp(Speed / Denominator, 0.0f, 1.0f)};
		// Hue 240 (blue) at T=0 down to 0 (red) at T=1.
		return FLinearColor{(1.0f - T) * 240.0f, 1.0f, 1.0f}.HSVToLinearRGB().ToFColor(true);
	}
}
#endif

void ULemonProjectileSubsystem::Deinitialize()
{
	for (const TObjectPtr<UNiagaraComponent>& Component : OwnedComponents)
	{
		if (IsValid(Component))
		{
			Component->DestroyComponent();
		}
	}
	OwnedComponents.Reset();
	FreeVisualsByDefinition.Reset();
	RetiringVisuals.Reset();
	Projectiles.Reset();

	Super::Deinitialize();
}

bool ULemonProjectileSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	// Only real gameplay worlds; skip editor preview / inactive worlds.
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

bool ULemonProjectileSubsystem::IsTickable() const
{
	// Nothing to do until at least one bullet is live (avoids ticking an idle subsystem every frame).
	return Projectiles.Num() > 0;
}

TStatId ULemonProjectileSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(ULemonProjectileSubsystem, STATGROUP_Tickables);
}

bool ULemonProjectileSubsystem::ShouldRender() const
{
	return !IsRunningDedicatedServer();
}

void ULemonProjectileSubsystem::SpawnProjectile(const FLemonProjectileFireParams& Params)
{
	if (!IsValid(Params.Definition) || Projectiles.Num() >= MaxLiveProjectiles)
	{
		return;
	}

	const ULemonProjectileDefinition& Def{*Params.Definition};

	FLemonProjectile Projectile;
	Projectile.Definition = Params.Definition;
	Projectile.Instigator = Params.Instigator;
	Projectile.OnHit = Params.OnHit;
	Projectile.Position = Params.Origin;
	Projectile.Velocity = Params.Direction.GetSafeNormal() * (Params.SpeedOverride > 0.0f ? Params.SpeedOverride : Def.InitialSpeed);
	Projectile.LaunchSpeed = UE_REAL_TO_FLOAT(Projectile.Velocity.Size());
	Projectile.GravityScale = Def.GravityScale;
	Projectile.MaxLifetime = Def.MaxLifetime;
	Projectile.MaxRange = Def.MaxRange;
	Projectile.CollisionRadius = Def.CollisionRadius;
	Projectile.CollisionChannel = Def.CollisionChannel;
	Projectile.bAuthoritative = Params.bAuthoritative;

	// Give the bullet its own pooled Niagara component that travels with it (clients / listen host only).
	if (ShouldRender() && IsValid(Def.FlightVfx))
	{
		if (UNiagaraComponent* Visual{AcquireVisual(Params.Definition)})
		{
			Visual->SetWorldLocationAndRotation(Projectile.Position, Projectile.Velocity.Rotation());
			Visual->Activate(/*bReset*/ true);
			Projectile.Visual = Visual;
		}
	}

	Projectiles.Add(MoveTemp(Projectile));

	// Muzzle flash at the launch point (cosmetic, every machine that renders).
	if (ShouldRender() && IsValid(Def.MuzzleVfx))
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(GetWorld(), Def.MuzzleVfx, Params.Origin, Params.Direction.Rotation());
	}

	// Gunshot audio at the muzzle (cosmetic, every machine that renders). Each sound carries its own attenuation
	// + concurrency; FireSoundDistant is an optional longer-range "thump" so far-away listeners hear a distinct
	// distant report. SpawnSoundAtLocation is evaluated per listener, so one spawn serves everyone in earshot.
	if (ShouldRender())
	{
		if (Def.FireSound != nullptr)
		{
			UGameplayStatics::SpawnSoundAtLocation(GetWorld(), Def.FireSound, Params.Origin);
		}
		if (Def.FireSoundDistant != nullptr)
		{
			UGameplayStatics::SpawnSoundAtLocation(GetWorld(), Def.FireSoundDistant, Params.Origin);
		}
	}
}

void ULemonProjectileSubsystem::SpawnProjectileSimple(ULemonProjectileDefinition* Definition, const FVector Origin,
	const FVector Direction, AActor* Instigator, const bool bAuthoritative, const float SpeedOverride)
{
	FLemonProjectileFireParams Params;
	Params.Definition = Definition;
	Params.Origin = Origin;
	Params.Direction = Direction;
	Params.Instigator = Instigator;
	Params.bAuthoritative = bAuthoritative;
	Params.SpeedOverride = SpeedOverride;
	SpawnProjectile(Params);
}

void ULemonProjectileSubsystem::Tick(float DeltaTime)
{
	if (DeltaTime <= 0.0f)
	{
		return;
	}

	for (FLemonProjectile& Projectile : Projectiles)
	{
		const EStepResult StepResult{StepProjectile(Projectile, DeltaTime)};
		if (StepResult != EStepResult::Alive)
		{
			Projectile.bPendingDestroy = true;

			// Park the visual at the bullet's final position, then tear it down. On a solid impact the head is
			// killed immediately so nothing pokes through the surface (the ImpactVfx covers the hit); on an
			// open-air expiry it is deactivated gently so an existing trail can finish.
			if (Projectile.Visual != nullptr)
			{
				Projectile.Visual->SetWorldLocation(Projectile.Position);
				RetireVisual(Projectile.Visual, Projectile.Definition.Get(), /*bImmediate*/ StepResult == EStepResult::Impact);
				Projectile.Visual = nullptr;
			}
		}
	}

	Projectiles.RemoveAllSwap([](const FLemonProjectile& Projectile) { return Projectile.bPendingDestroy; }, EAllowShrinking::No);

	ProcessRetiringVisuals();

#if ENABLE_DRAW_DEBUG
	if (ShouldRender() && GEngine != nullptr && LemonProjectileDebug::CVarDebugStats.GetValueOnGameThread() != 0)
	{
		int32 NumAuthoritative{0};
		for (const FLemonProjectile& Projectile : Projectiles)
		{
			NumAuthoritative += Projectile.bAuthoritative ? 1 : 0;
		}

		GEngine->AddOnScreenDebugMessage(reinterpret_cast<uint64>(this), 0.0f, FColor::Cyan,
			FString::Printf(TEXT("[Lemon Projectiles] live: %d  (auth: %d, cosmetic: %d)"),
				Projectiles.Num(), NumAuthoritative, Projectiles.Num() - NumAuthoritative));
	}
#endif
}

ULemonProjectileSubsystem::EStepResult ULemonProjectileSubsystem::StepProjectile(FLemonProjectile& Projectile, float DeltaTime)
{
	UWorld* World{GetWorld()};
	if (World == nullptr)
	{
		return EStepResult::Expired;
	}

	// Integrate velocity (gravity) then position.
	if (Projectile.GravityScale != 0.0f)
	{
		Projectile.Velocity.Z += World->GetGravityZ() * Projectile.GravityScale * DeltaTime;
	}

	const FVector Start{Projectile.Position};
	const FVector End{Start + Projectile.Velocity * DeltaTime};
	const float StepDistance{UE_REAL_TO_FLOAT((End - Start).Size())};

	// Sweep (or line trace) the segment travelled this frame.
	FCollisionQueryParams QueryParams{SCENE_QUERY_STAT(LemonProjectile), /*bTraceComplex*/ false};
	if (Projectile.Instigator.IsValid())
	{
		QueryParams.AddIgnoredActor(Projectile.Instigator.Get());
	}

	FHitResult Hit;
	bool bBlockingHit{false};
	if (Projectile.CollisionRadius > UE_KINDA_SMALL_NUMBER)
	{
		bBlockingHit = World->SweepSingleByChannel(Hit, Start, End, FQuat::Identity, Projectile.CollisionChannel,
		                                           FCollisionShape::MakeSphere(Projectile.CollisionRadius), QueryParams);
	}
	else
	{
		bBlockingHit = World->LineTraceSingleByChannel(Hit, Start, End, Projectile.CollisionChannel, QueryParams);
	}

#if ENABLE_DRAW_DEBUG
	// Arc: draw the segment travelled this frame, colored by speed. Persisting segments build the arc.
	if (ShouldRender() && LemonProjectileDebug::CVarDebugArc.GetValueOnGameThread() != 0)
	{
		const FVector DrawEnd{bBlockingHit ? Hit.ImpactPoint : End};
		const float CVarMaxSpeed{LemonProjectileDebug::CVarDebugMaxSpeed.GetValueOnGameThread()};
		const float MaxSpeed{CVarMaxSpeed > 0.0f ? CVarMaxSpeed : Projectile.LaunchSpeed};
		const float Speed{UE_REAL_TO_FLOAT(Projectile.Velocity.Size())};
		const FColor Color{LemonProjectileDebug::SpeedToColor(Speed, MaxSpeed)};
		const float Duration{LemonProjectileDebug::CVarDebugArcDuration.GetValueOnGameThread()};
		DrawDebugLine(World, Start, DrawEnd, Color, /*bPersistent*/ false, Duration, /*DepthPriority*/ 0,
		              /*Thickness*/ Projectile.bAuthoritative ? 2.0f : 1.0f);
	}
#endif

	if (bBlockingHit)
	{
		Projectile.Position = Hit.ImpactPoint;

		// The authoritative bullet reports its impact to whoever is listening, which decides what a hit does
		// (damage, stun, knockback, ...). The projectile system stays damage-agnostic. Cosmetic bullets never
		// report. C++ gets the per-shot Params.OnHit; Blueprint binds the subsystem's OnProjectileHit event.
		if (Projectile.bAuthoritative)
		{
			const bool bHasGlobalListener{OnProjectileHit.IsBound()};
			if (Projectile.OnHit.IsBound() || bHasGlobalListener)
			{
				FLemonProjectileHitResult HitInfo;
				HitInfo.Hit = Hit;
				HitInfo.Instigator = Projectile.Instigator.Get();
				HitInfo.Definition = Projectile.Definition.Get();

				Projectile.OnHit.ExecuteIfBound(HitInfo);
				if (bHasGlobalListener)
				{
					OnProjectileHit.Broadcast(HitInfo);
				}
			}
		}

#if ENABLE_DRAW_DEBUG
		if (ShouldRender() && LemonProjectileDebug::CVarDebugHits.GetValueOnGameThread() != 0)
		{
			const float Duration{FMath::Max(LemonProjectileDebug::CVarDebugArcDuration.GetValueOnGameThread(), 2.0f)};
			const FColor MarkerColor{Projectile.bAuthoritative ? FColor::Red : FColor::Yellow};
			DrawDebugSphere(World, Hit.ImpactPoint, FMath::Max(Projectile.CollisionRadius, 6.0f), 12, MarkerColor, false, Duration);
			DrawDebugDirectionalArrow(World, Hit.ImpactPoint, Hit.ImpactPoint + Hit.ImpactNormal * 40.0f, 12.0f, MarkerColor, false, Duration);

			const FString Info{FString::Printf(TEXT("%s HIT\n%s\nSpd %.0f  Dist %.0fm"),
				Projectile.bAuthoritative ? TEXT("[AUTH]") : TEXT("[COSM]"),
				*GetNameSafe(Hit.GetActor()),
				UE_REAL_TO_FLOAT(Projectile.Velocity.Size()),
				Projectile.DistanceTravelled / 100.0f)};
			DrawDebugString(World, Hit.ImpactPoint, Info, nullptr, MarkerColor, Duration, /*bDrawShadow*/ true);
		}
#endif

		if (ShouldRender())
		{
			if (const ULemonProjectileDefinition* Def{Projectile.Definition.Get()})
			{
				if (IsValid(Def->ImpactVfx))
				{
					UNiagaraFunctionLibrary::SpawnSystemAtLocation(World, Def->ImpactVfx, Hit.ImpactPoint, Hit.ImpactNormal.Rotation());
				}
			}
		}

		return EStepResult::Impact;
	}

	Projectile.Position = End;
	Projectile.Age += DeltaTime;
	Projectile.DistanceTravelled += StepDistance;

	// Carry the visual along the bullet, oriented to its travel direction.
	if (Projectile.Visual != nullptr)
	{
		Projectile.Visual->SetWorldLocationAndRotation(Projectile.Position, Projectile.Velocity.Rotation());
	}

	// Expire silently (no impact effect) on lifetime / range.
	const bool bStillAlive{Projectile.Age < Projectile.MaxLifetime && Projectile.DistanceTravelled < Projectile.MaxRange};
	return bStillAlive ? EStepResult::Alive : EStepResult::Expired;
}

UNiagaraComponent* ULemonProjectileSubsystem::AcquireVisual(ULemonProjectileDefinition* Definition)
{
	UWorld* World{GetWorld()};
	if (World == nullptr || !IsValid(Definition) || !IsValid(Definition->FlightVfx))
	{
		return nullptr;
	}

	// Reuse an idle component built for this definition if one is free.
	if (TArray<UNiagaraComponent*>* Free{FreeVisualsByDefinition.Find(Definition)})
	{
		while (Free->Num() > 0)
		{
			if (UNiagaraComponent* Reused{Free->Pop(EAllowShrinking::No)}; IsValid(Reused))
			{
				return Reused;
			}
		}
	}

	// Otherwise create one. It is world-owned (no parent), positioned in world space each frame, and never
	// auto-destroyed - it is recycled instead. The OwnedComponents array keeps it referenced for GC.
	UNiagaraComponent* Component{NewObject<UNiagaraComponent>(World)};
	Component->SetAsset(Definition->FlightVfx);
	Component->SetAutoActivate(false);
	Component->SetAutoDestroy(false);
	Component->RegisterComponentWithWorld(World);
	OwnedComponents.Add(Component);

	return Component;
}

void ULemonProjectileSubsystem::RetireVisual(UNiagaraComponent* Component, ULemonProjectileDefinition* Definition, bool bImmediate)
{
	if (!IsValid(Component))
	{
		return;
	}

	if (bImmediate)
	{
		// Solid impact: kill every particle now (head + trail) so nothing is left poking through the surface.
		Component->DeactivateImmediate();
	}
	else
	{
		// Open-air expiry: stop spawning new particles but let existing ones (e.g. a trail) finish.
		Component->Deactivate();
	}

	const float Now{GetWorld() != nullptr ? UE_REAL_TO_FLOAT(GetWorld()->GetTimeSeconds()) : 0.0f};
	RetiringVisuals.Add(FRetiringVisual{Component, Definition, Now});
}

void ULemonProjectileSubsystem::ProcessRetiringVisuals()
{
	const float Now{GetWorld() != nullptr ? UE_REAL_TO_FLOAT(GetWorld()->GetTimeSeconds()) : 0.0f};

	for (int32 Index{RetiringVisuals.Num() - 1}; Index >= 0; --Index)
	{
		UNiagaraComponent* Component{RetiringVisuals[Index].Component};
		ULemonProjectileDefinition* Definition{RetiringVisuals[Index].Definition};

		if (!IsValid(Component))
		{
			RetiringVisuals.RemoveAtSwap(Index, EAllowShrinking::No);
			continue;
		}

		// Reclaim once the trail has finished. The linger cap force-kills a looping / endless system whose
		// particles would otherwise never complete, so it can never stick around (or stall reuse) forever.
		const bool bLingeredTooLong{Now - RetiringVisuals[Index].RetireTimeSeconds > MaxRetireLingerSeconds};
		if (Component->IsComplete() || bLingeredTooLong)
		{
			if (bLingeredTooLong && !Component->IsComplete())
			{
				Component->DeactivateImmediate();
			}

			if (IsValid(Definition))
			{
				FreeVisualsByDefinition.FindOrAdd(Definition).Add(Component);
			}
			RetiringVisuals.RemoveAtSwap(Index, EAllowShrinking::No);
		}
	}
}
