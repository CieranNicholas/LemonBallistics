#pragma once

#include "Subsystems/WorldSubsystem.h"
#include "Engine/HitResult.h"
#include "LemonProjectileSubsystem.generated.h"

class ULemonProjectileDefinition;
class UNiagaraComponent;

/// What an authoritative projectile hit. Handed to whoever is listening; the projectile system has no damage
/// concept of its own — the listener decides what a hit does (GAS effect, custom damage component, FX, ...).
USTRUCT(BlueprintType)
struct FLemonProjectileHitResult
{
	GENERATED_BODY()

	/// Collision result: hit actor + component, impact point / normal, bone, etc.
	UPROPERTY(BlueprintReadOnly, Category = "Projectile")
	FHitResult Hit;

	/// Actor that fired the bullet (already excluded from the sweep). Null if it was destroyed mid-flight.
	UPROPERTY(BlueprintReadOnly, Category = "Projectile")
	TObjectPtr<AActor> Instigator{nullptr};

	/// Definition that launched the bullet (movement / visuals), for handlers that branch on projectile type.
	UPROPERTY(BlueprintReadOnly, Category = "Projectile")
	TObjectPtr<ULemonProjectileDefinition> Definition{nullptr};
};

/// C++ per-shot hit callback — set on FLemonProjectileFireParams::OnHit. Invoked once, on the authoritative
/// bullet ONLY, on impact. Blueprint uses the subsystem's OnProjectileHit event instead (see below).
DECLARE_DELEGATE_OneParam(FLemonProjectileHitDelegate, const FLemonProjectileHitResult& /*HitInfo*/);

/// Blueprint-bindable equivalent: broadcast on every authoritative projectile impact. Bind
/// ULemonProjectileSubsystem::OnProjectileHit from a Blueprint to react to hits without touching C++.
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FLemonProjectileHitDynDelegate, const FLemonProjectileHitResult&, HitInfo);

/// Spawn-time description of a single projectile, handed to the subsystem. Built by the firing code
/// (gameplay ability on the authority, weapon multicast on remotes, prediction on the owning client).
USTRUCT(BlueprintType)
struct LEMONBALLISTICS_API FLemonProjectileFireParams
{
	GENERATED_BODY()

	/// What kind of bullet this is (movement / collision / visuals).
	UPROPERTY(BlueprintReadWrite, Category = "Projectile")
	TObjectPtr<ULemonProjectileDefinition> Definition{nullptr};

	/// World-space launch point (typically the weapon muzzle).
	UPROPERTY(BlueprintReadWrite, Category = "Projectile")
	FVector Origin{ForceInit};

	/// Normalized launch direction.
	UPROPERTY(BlueprintReadWrite, Category = "Projectile")
	FVector Direction{ForceInit};

	/// Overrides the definition's InitialSpeed when > 0.
	UPROPERTY(BlueprintReadWrite, Category = "Projectile")
	float SpeedOverride{0.0f};

	/// Actor that fired the bullet. Ignored by the collision sweep and reported back as the hit instigator.
	UPROPERTY(BlueprintReadWrite, Category = "Projectile")
	TObjectPtr<AActor> Instigator{nullptr};

	/// True only for the server's authoritative bullet (the one that triggers OnHit / OnProjectileHit). Cosmetic
	/// copies leave it false. Set this true on the authority (or in single-player) for the bullet that applies damage.
	UPROPERTY(BlueprintReadWrite, Category = "Projectile")
	bool bAuthoritative{false};

	/// Called on the authoritative bullet's impact. Bind to apply damage/effects however you like; leave
	/// unbound for a purely cosmetic / non-damaging projectile. Not exposed to Blueprint (non-dynamic delegate).
	FLemonProjectileHitDelegate OnHit;
};

/// Central, non-actor projectile simulation. Holds a flat array of lightweight projectile structs, ticks
/// their motion, and sweeps for collisions itself - so there is no actor, replication channel, or tick
/// function per bullet. Each live bullet is drawn by a pooled, recycled UNiagaraComponent that the
/// subsystem moves along its path, which makes any marketplace flight effect drop in without parameter
/// wiring. This scales to hundreds of simultaneous bullets, far above one-actor-per-bullet designs.
///
/// Networking model (predicted cosmetic + server-authoritative hit):
///   - The owning client spawns a cosmetic bullet immediately (prediction) for zero-latency feedback.
///   - The server spawns an authoritative bullet that performs the real hit and applies the damage effect.
///   - The server multicasts a compact fire event so every other client spawns its own cosmetic bullet.
/// Cosmetic bullets never apply damage; only the authoritative bullet does.
UCLASS(BlueprintType)
class LEMONBALLISTICS_API ULemonProjectileSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	// UTickableWorldSubsystem
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickable() const override;
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

	/// Spawns a projectile into the simulation. Safe to call on any machine; whether it triggers a hit (and thus
	/// OnProjectileHit / the params' OnHit) is governed by Params.bAuthoritative. C++ callers may set Params.OnHit
	/// for a per-shot callback; Blueprint typically uses SpawnProjectileSimple + the OnProjectileHit event.
	UFUNCTION(BlueprintCallable, Category = "Lemon|Projectile", Meta = (AutoCreateRefTerm = "Params"))
	void SpawnProjectile(const FLemonProjectileFireParams& Params);

	/// Blueprint-friendly spawn that fills in the params for you. Set bAuthoritative = true on the server (or in
	/// single-player) for the bullet that should report its hit through OnProjectileHit. SpeedOverride 0 = use the
	/// definition's InitialSpeed.
	UFUNCTION(BlueprintCallable, Category = "Lemon|Projectile", Meta = (AdvancedDisplay = "SpeedOverride"))
	void SpawnProjectileSimple(ULemonProjectileDefinition* Definition, FVector Origin, FVector Direction,
		AActor* Instigator, bool bAuthoritative, float SpeedOverride = 0.0f);

	/// Broadcast on every authoritative projectile impact (server / single-player). Bind from Blueprint to apply
	/// damage or spawn reactions through any system; branch on HitInfo.Definition / Instigator. Cosmetic bullets
	/// never broadcast it. (Equivalent to the C++ per-shot FLemonProjectileFireParams::OnHit, but global.)
	UPROPERTY(BlueprintAssignable, Category = "Lemon|Projectile")
	FLemonProjectileHitDynDelegate OnProjectileHit;

private:
	/// Live simulation state for one bullet.
	struct FLemonProjectile
	{
		TWeakObjectPtr<ULemonProjectileDefinition> Definition;
		TWeakObjectPtr<AActor> Instigator;
		FLemonProjectileHitDelegate OnHit;
		FVector Position{ForceInit};
		FVector Velocity{ForceInit};
		float LaunchSpeed{0.0f};
		float Age{0.0f};
		float DistanceTravelled{0.0f};
		float GravityScale{0.0f};
		float MaxLifetime{0.0f};
		float MaxRange{0.0f};
		float CollisionRadius{0.0f};
		TEnumAsByte<ECollisionChannel> CollisionChannel{ECC_Visibility};
		bool bAuthoritative{false};
		bool bPendingDestroy{false};

		/// Pooled Niagara component drawing this bullet (clients / listen host only). Kept alive by
		/// OwnedComponents; null on a dedicated server or when the definition has no FlightVfx.
		UNiagaraComponent* Visual{nullptr};
	};

	/// Outcome of advancing one bullet for a frame - decides how its visual is torn down.
	enum class EStepResult : uint8
	{
		Alive,    ///< Still flying; keep simulating.
		Impact,   ///< Hit solid geometry this frame; the head must vanish immediately (no poking through surfaces).
		Expired,  ///< Ran out of lifetime / range in open air; an existing trail may finish gracefully.
	};

	/// A retired visual whose trail is still finishing. Reclaimed once complete, or force-killed after a cap.
	struct FRetiringVisual
	{
		UNiagaraComponent* Component{nullptr};
		ULemonProjectileDefinition* Definition{nullptr};
		float RetireTimeSeconds{0.0f};
	};

	/// Advances one bullet; the result tells the caller whether (and how) to remove it this frame.
	EStepResult StepProjectile(FLemonProjectile& Projectile, float DeltaTime);

	/// Whether this machine renders anything (false on a dedicated server).
	bool ShouldRender() const;

	// Niagara component pool (one component per live bullet, recycled across bullets of the same definition).

	/// Gets a ready Niagara component for the definition - reused from the free pool or freshly created.
	UNiagaraComponent* AcquireVisual(ULemonProjectileDefinition* Definition);

	/// Tears down a bullet's component and queues it for reclamation. bImmediate kills every particle now
	/// (impact - nothing left poking through the surface); otherwise the existing trail is left to finish.
	void RetireVisual(UNiagaraComponent* Component, ULemonProjectileDefinition* Definition, bool bImmediate);

	/// Returns finished (complete) retiring components to their definition's free pool.
	void ProcessRetiringVisuals();

	TArray<FLemonProjectile> Projectiles;

	/// Strong references to every pooled component (the pool's backing store; nothing is destroyed until
	/// Deinitialize - components are deactivated and reused).
	UPROPERTY(Transient)
	TArray<TObjectPtr<UNiagaraComponent>> OwnedComponents;

	/// Idle components ready for reuse, bucketed by the definition (Niagara asset) they were built for.
	TMap<ULemonProjectileDefinition*, TArray<UNiagaraComponent*>> FreeVisualsByDefinition;

	/// Components whose bullet has died but whose trail is still finishing; reclaimed once complete.
	TArray<FRetiringVisual> RetiringVisuals;

	/// Hard cap so a runaway spawner cannot grow the array without bound.
	static constexpr int32 MaxLiveProjectiles{4096};

	/// Safety cap on how long a retiring visual may linger before it is force-killed and reclaimed. Stops a
	/// looping / endless flight system (whose head never dies on its own) from sticking around indefinitely.
	static constexpr float MaxRetireLingerSeconds{3.0f};
};
