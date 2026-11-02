#pragma once

class Game;

// Engine hooks for Strata Source (Portal: Revolution, x64). Functions are found through RTTI
// (class name -> vtable) and checked vtable indices instead of byte signatures; see dev-notes,
// VTABLES. Replaces the Portal 2 hooks in hooks.cpp step by step.
namespace StrataHooks
{
    // Returns the vtable of the class with this name in the module, or nullptr.
    void **FindVTable(const char *module, const char *className);

    bool Init(Game *game);
}
