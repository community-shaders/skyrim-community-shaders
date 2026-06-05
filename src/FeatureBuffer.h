#pragma once

// Returns a pointer into reused, thread-local storage and the packed size.
// The pointer is non-owning: do NOT delete[] it.
std::pair<unsigned char*, size_t> GetFeatureBufferData(bool a_early);