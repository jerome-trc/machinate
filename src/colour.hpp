/**
 * @file colour.hpp
 * @brief Colour constants for use in ImGui, etc.
 */

#pragma once

namespace mxn
{
	constexpr float RED_F[3] = { 1.0f, 0.3f, 0.3f }, ORANGE_F[3] = { 0.9f, 0.6f, 0.3f },
					YELLOW_F[3] = { 1.0f, 1.0f, 0.4f }, GREEN_F[3] = { 0.6f, 1.0f, 0.6f },
					CYAN_F[3] = { 0.05f, 0.9f, 0.9f },
					TEAL_F[3] = { 0.15f, 0.75f, 0.75f }, DTEAL_F[3] = { 0.08, 0.3, 0.36 },
					BLUE_F[3] = { 0.3f, 0.3f, 1.0f }, PURPLE_F[3] = { 0.65f, 0.0f, 1.0f },
					PINK_F[3] = { 0.8f, 0.2f, 0.8f };

	constexpr float OFFWHITE_F[3] = { 0.9f, 0.9f, 0.9f },
					LGREY_F[3] = { 0.75f, 0.75f, 0.75f },
					DGREY_F[3] = {
						0.5f,
						0.5f,
						0.5f,
					};
} // namespace mxn
