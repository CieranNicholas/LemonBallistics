#pragma once

#include "Engine/DataAsset.h"
#include "Engine/EngineTypes.h"
#include "LemonProjectileDefinition.generated.h"

class UNiagaraSystem;
class USoundBase;

/// Describes one kind of projectile: how it moves, how it collides, and how it is drawn. The projectile
/// subsystem simulates lightweight instances of this (no per-bullet actor), so a single definition can
/// drive hundreds of in-flight bullets that all render through one Niagara system.
///
/// Deliberately has NO damage concept — the projectile is decoupled from any gameplay/damage system. What a
/// hit *does* is decided by whoever fires it, via the FLemonProjectileFireParams::OnHit delegate.
///
/// Assign an instance to whatever weapon/ability fires it and hand it to ULemonProjectileSubsystem.
UCLASS(BlueprintType)
class LEMONBALLISTICS_API ULemonProjectileDefinition : public UDataAsset
{
	GENERATED_BODY()

public:
	// Movement

	/// Launch speed along the fire direction.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement", Meta = (ClampMin = 0, ForceUnits = "cm/s"))
	float InitialSpeed{8000.0f};

	/// Fraction of world gravity applied to the bullet (0 = perfectly straight, 1 = full drop).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement", Meta = (ClampMin = 0))
	float GravityScale{0.0f};

	/// Maximum time the bullet lives before it is silently removed (no impact effect).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement", Meta = (ClampMin = 0, ForceUnits = "s"))
	float MaxLifetime{3.0f};

	/// Maximum distance the bullet may travel before it is silently removed.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement", Meta = (ClampMin = 0, ForceUnits = "cm"))
	float MaxRange{50000.0f};

	// Collision

	/// Sphere radius used to sweep for hits. 0 uses a thin line trace (cheapest; true hitscan-thickness).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Collision", Meta = (ClampMin = 0, ForceUnits = "cm"))
	float CollisionRadius{2.0f};

	/// Trace channel the bullet collides against. Use a dedicated "Projectile" channel in production.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Collision")
	TEnumAsByte<ECollisionChannel> CollisionChannel{ECC_Visibility};

	// Visuals

	/// Niagara system that travels with the bullet. The subsystem gives each live bullet its own pooled
	/// Niagara component, moves it along the bullet's path each frame, and orients it to the velocity - so
	/// any marketplace "projectile" / "tracer" / "trail" system works as-is, with no parameter wiring.
	/// Leave null for invisible bullets (e.g. a server-only test, or debug-arc-only previewing).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Visuals", Meta = (DisplayThumbnail = false))
	TObjectPtr<UNiagaraSystem> FlightVfx;

	/// One-shot Niagara system spawned at the muzzle when the bullet is fired (optional).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Visuals", Meta = (DisplayThumbnail = false))
	TObjectPtr<UNiagaraSystem> MuzzleVfx;

	/// One-shot Niagara system spawned at the impact point when the bullet hits (optional).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Visuals", Meta = (DisplayThumbnail = false))
	TObjectPtr<UNiagaraSystem> ImpactVfx;

	// Audio (cosmetic; played at the muzzle on every machine that renders the shot, alongside MuzzleVfx).

	/// The gunshot — typically a MetaSound. It carries its own Sound Attenuation + Concurrency (assigned on the
	/// asset), which own spatialization, distance low-pass (air absorption), occlusion, and the reverb-submix
	/// send. Null = silent.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Audio", Meta = (DisplayThumbnail = false))
	TObjectPtr<USoundBase> FireSound;

	/// Optional separate "distant report" for far-away listeners (a duller, longer thump). Give it a long-range,
	/// heavily low-passed attenuation so near listeners hear FireSound and far listeners hear this. Both are
	/// spawned per shot; each listener's attenuation picks the right blend. Null = no distant layer.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Audio", Meta = (DisplayThumbnail = false))
	TObjectPtr<USoundBase> FireSoundDistant;
};
