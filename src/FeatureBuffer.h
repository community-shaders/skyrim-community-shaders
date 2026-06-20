#pragma once

/**
 * @brief Returns packed feature buffer data from reused thread-local storage.
 * @param a_early Whether to use early (pre-world) feature data.
 * @return Non-owning pointer into thread-local storage and the packed size.
 *         Do NOT delete the returned pointer.
 */
std::pair<unsigned char*, size_t> GetFeatureBufferData(bool a_early);