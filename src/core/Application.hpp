#pragma once

namespace Supersonic {

// Asking the application to stop.
//
// A game could not close itself. The window closes when the player clicks the
// title bar's cross, and that was the only way out: a layer cannot reach the
// Window - deliberately, it has no business owning one - and Window::ShouldClose
// only reports what GLFW already decided. So a Quit button, which is on the
// pause menu and the main menu of every game ever written, had nothing to call.
//
// Godot spells this get_tree().quit(). Here it is a free function, matching
// Input rather than the registry context, because quitting is not a property of
// a world: a game with no scene open still has a menu with a Quit on it, and a
// request that lived in the registry would be cleared by the next scene load.
//
// The request is a LATCH rather than an immediate exit. Whoever presses the
// button is inside a tick, several systems deep, with a frame half-built around
// them; tearing the device down from there would be a crash rather than a
// shutdown. The run loop reads it at the top of the next frame and leaves the
// same way it does for a closed window, so exactly one shutdown path exists.
namespace Application {

// Stop after the current frame. Safe to call from anywhere, including from
// inside a tick, and safe to call more than once.
void RequestQuit();

// Whether someone has asked. Read by the run loop.
bool QuitRequested();

// Forgets the request.
//
// For tests, and for an editor that wants to offer "are you sure". Nothing in
// the run loop calls it: a request that could be quietly withdrawn by the
// engine itself would be a Quit button that sometimes does nothing.
void ClearQuitRequest();

} // namespace Application

} // namespace Supersonic
