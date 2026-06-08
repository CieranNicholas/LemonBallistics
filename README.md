# LemonBallistics

A small projectile plugin for Unreal Engine 5. It simulates bullets *without* spawning an actor for each one, so you can put a lot of them on screen without paying the usual price.

It started life inside a game project and got lifted out once it was clearly generic enough to stand on its own, so I thought I'd share it.

## What it's for

The default way to do projectiles in UE is one actor per bullet: a movement component, a collision component, the whole actor replicating and ticking. That's fine for a rocket launcher. It starts to hurt the moment you have a full-auto weapon, a few of them, in multiplayer — every bullet is an actor spawn, a tick, and a replicated channel.

LemonBallistics keeps each live bullet as a plain struct in a single array. The subsystem ticks them together, sweeps their collision itself, and draws each one with a pooled Niagara component that it moves along the path. No actors, no per-bullet replication. It'll happily run a few hundred bullets at once.

Two nice things fall out of that approach:

What it doesn't try to be: it isn't networked for you (spawning and authority are yours — see below), and it's a per-frame sweep rather than a sub-stepping physics body. Great for bullets and fast projectiles, not the thing to reach for if you want a grenade that bounces around the room.

## Setup

Copy the `LemonBallistics` folder into your project's `Plugins/` and enable it in **Edit → Plugins** (if your `.uproject` already lists it, it's on). If you're calling it from C++, add the module to your `*.Build.cs`:

```csharp
PublicDependencyModuleNames.Add("LemonBallistics");
```
Its only real dependency is Niagara.

## The moving parts

- **`ULemonProjectileDefinition`** — a data asset describing one kind of bullet: speed, gravity scale, lifetime, max range, collision channel, and the Niagara systems + sounds to play. Make one per projectile type.
- **`ULemonProjectileSubsystem`** — the world subsystem that owns the simulation. You ask it to spawn projectiles; it handles the rest.
- **`FLemonProjectileFireParams`** — the spawn description (which definition, from where, which way, who fired it).
- **`FLemonProjectileHitResult`** — what you get back on a hit: the `FHitResult`, the instigator, and the definition that fired.

## From C++

Grab the subsystem off the world and hand it a fire-params struct. You'll want `LemonProjectileSubsystem.h` and `LemonProjectileDefinition.h`.

```cpp
ULemonProjectileSubsystem* Projectiles = GetWorld()->GetSubsystem<ULemonProjectileSubsystem>();

FLemonProjectileFireParams Params;
Params.Definition     = MyProjectileDef;   // a ULemonProjectileDefinition*
Params.Origin         = MuzzleLocation;
Params.Direction      = FireDirection;     // normalized for you
Params.Instigator     = this;              // excluded from the sweep
Params.bAuthoritative = HasAuthority();    // only the authoritative bullet reports a hit

// What a hit does is up to you. Leave OnHit unbound for a cosmetic-only bullet.
Params.OnHit.BindLambda([](const FLemonProjectileHitResult& Info)
{
    if (AActor* HitActor = Info.Hit.GetActor())
    {
        // apply damage / effects however you like
    }
});

Projectiles->SpawnProjectile(Params);
```

`OnHit` only fires for the authoritative bullet (`bAuthoritative == true`). Cosmetic copies fly and impact visually but never call back.

## From Blueprint

Get the subsystem with the **Get World Subsystem** node (pick `Lemon Projectile Subsystem`), then:

- **Spawn Projectile Simple** — the quick path. Definition, Origin, Direction, Instigator, and a `bAuthoritative` bool. (There's also **Spawn Projectile** if you'd rather make the params struct yourself.)
- **On Projectile Hit** — an assignable event on the subsystem. It fires on every authoritative impact and hands you the hit info. It's global, so if a graph only cares about its own bullets, branch on `Definition` or `Instigator`.

A minimal Blueprint weapon is basically: on fire → Spawn Projectile Simple; on begin play → Bind Event to On Projectile Hit and do your damage in there.

## Networking

The plugin replicates nothing, on purpose — it keeps it small and lets you bolt it onto whatever netcode you already have. The pattern it's built around:

- The **server** spawns the authoritative bullet (`bAuthoritative = true`). That's the one that actually hits.
- Everyone spawns a **cosmetic** bullet (`bAuthoritative = false`) for the tracer, usually off an unreliable multicast.
- The owning client can fire its cosmetic bullet right away for zero-latency feedback while the server's authoritative one does the real work.

None of that is imposed on you, but it's the split the authoritative/`OnHit` design is built around.

## Debugging

A handful of cheat CVars (dev builds only):

- `lemon.Projectile.DebugArc 1` — draw flight arcs, colored by speed
- `lemon.Projectile.DebugHits 1` — mark impacts with the hit actor and distance
- `lemon.Projectile.DebugStats 1` — show the live bullet count on screen

These work even with no `FlightVfx` assigned, so you can check trajectories before any art exists.
