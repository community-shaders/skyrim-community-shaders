#pragma once

// Client-side bridge to the devbench host (https://github.com/alandtse/devbench).
// Registers Community Shaders' tools into devbench over its cross-plugin C-ABI so they are
// drivable from the shared bench (MCP + REST).
//
// The implementation compiles only with -DDEVBENCH_BRIDGE_ENABLED (set by CMake when the
// `devbench-api` port is available); otherwise this file compiles to an empty Install().
// When built in, Install() is still a runtime no-op if no devbench host is present — so
/**
 * Registers the project's tools with the devbench host.
 *
 * Fetches the devbench interface and registers the project's tools for remote
 * control. If the devbench host is not present or the bridge was compiled with
 * the feature disabled, this function is a no-op.
 */
namespace DevBenchBridge
{
	// Fetch the devbench interface (after kPostLoad) and register our tools. No-op if
	// devbench is not present or the bridge was built disabled. Safe to call always.
	void Install();
}
