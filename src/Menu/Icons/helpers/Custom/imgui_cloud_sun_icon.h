// imgui_cloud_sun_icon.h
// "Cloud + sun" icon for Dear ImGui, drawn with ImDrawList (no fonts or textures).
//
// Design: Font Awesome 7 cloud silhouette (two lobes + rounded base) with the
// Lucide sun (ring + 4 rays) tucked behind it, with a gap cut between them.
// Attribution: Font Awesome Free icons are CC BY 4.0, Lucide is ISC.
//
// Lucide sun geometry, used under the ISC License:
//   Copyright (c) 2026 Lucide Icons and Contributors
//
//   Permission to use, copy, modify, and/or distribute this software for any
//   purpose with or without fee is hereby granted, provided that the above
//   copyright notice and this permission notice appear in all copies.
//
//   THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
//   WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
//   MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
//   ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
//   WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
//   ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
//   OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
#pragma once
#include "imgui.h"
#include <cmath>

namespace Icons
{
	// Draws the icon inside a (size x size) box at `pos`.
	//   cloudCol : colour of the cloud
	//   sunCol   : colour of the sun (defaults to the cloud colour)
	// Tip: use a fully opaque colour. The cloud is 3 overlapping shapes, so a
	// translucent colour would show through where they overlap.
	inline void CloudSun(ImDrawList* dl, ImVec2 pos, float size,
		ImU32 cloudCol = IM_COL32(255, 255, 255, 255),
		ImU32 sunCol = 0)
	{
		if (sunCol == 0)
			sunCol = cloudCol;

		// Design space (Font Awesome units): content spans x 32..604, y -44..512
		const float X0 = 32.0f, Y0 = -44.0f, W = 572.0f, H = 556.0f;
		const float k = size / W;                  // W >= H, so width fits the box
		const float offY = (size - H * k) * 0.5f;  // centre vertically
		auto P = [&](float x, float y) { return ImVec2(pos.x + (x - X0) * k, pos.y + offY + (y - Y0) * k); };

		// ---- Sun (drawn first, sits behind the cloud) ----
		const float sw = 38.0f * k;           // stroke width
		const ImVec2 sc = P(395.0f, 165.0f);  // sun centre
		const float deg = 3.14159265f / 180.0f;

		// Ring: everything except the part hidden behind the cloud (149.5 -> 412.5 deg)
		dl->PathArcTo(sc, 76.0f * k, 149.5f * deg, 412.5f * deg);
		dl->PathStroke(sunCol, ImDrawFlags_None, sw);

		// Rays: line + round caps
		const float rays[4][4] = {
			{ 395.0f, -25.0f, 395.0f, 13.0f },   // top
			{ 260.7f, 30.7f, 287.5f, 57.5f },    // top-left
			{ 529.3f, 30.7f, 502.5f, 57.5f },    // top-right
			{ 547.0f, 165.0f, 585.0f, 165.0f },  // right
		};
		for (const auto& r : rays) {
			ImVec2 a = P(r[0], r[1]), b = P(r[2], r[3]);
			dl->AddLine(a, b, sunCol, sw);
			dl->AddCircleFilled(a, sw * 0.5f, sunCol);
			dl->AddCircleFilled(b, sw * 0.5f, sunCol);
		}

		// ---- Cloud: left lobe + right lobe + rounded base ----
		dl->AddCircleFilled(P(208.3f, 304.0f), 112.0f * k, cloudCol);
		dl->AddCircleFilled(P(368.3f, 304.0f), 80.0f * k, cloudCol);
		dl->AddRectFilled(P(32.0f, 320.0f), P(544.0f, 512.0f), cloudCol, 96.0f * k);
	}
}