#pragma once

namespace Deki
{

class Scene;

/// The scene shown between the boot scene and the game's startup scene: a
/// splash screen, such as the Deki logo deki-rendering shows. Once every
/// SetupComponent is done, PlatformSetupComponent opens it instead of the
/// startup scene; the splash calls PlatformSetupComponent::LoadStartupScene()
/// when it has been on screen long enough. The screen keeps the splash while
/// the startup scene loads.
namespace BootSplash
{

/// Makes the splash scene. Returns nullptr to go straight to the startup scene.
using MakeScene = Scene* (*)();

/// Sets what makes the splash scene, or nullptr for none. A package sets it at
/// init; the last one set wins.
void SetMaker(MakeScene make);

/// What makes the splash scene, or nullptr when nothing set one.
MakeScene GetMaker();

}  // namespace BootSplash

}  // namespace Deki
