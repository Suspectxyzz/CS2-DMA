#pragma once

// Embedded frontend assets for WebRadar single-file deployment.
//
// Frontend dist files are embedded into cs2.exe via Windows RCDATA resources
// (see webRadar_resources.rc). FindEmbeddedAsset looks up a resource by its
// URL path (e.g. "/index.html") and returns a pointer to the in-memory data
// without copying. The returned pointer is valid for the lifetime of the
// module and must not be freed.

#include <cstddef>
#include <string>

namespace webradar {

struct EmbeddedAsset {
	const void* data = nullptr;
	size_t      size = 0;
};

// Looks up an embedded asset by URL path (e.g. "/index.html", "/assets/index-xxxx.js").
// Returns true and fills *out on success, false if no resource matches.
bool FindEmbeddedAsset(const std::string& urlPath, EmbeddedAsset* out);

} // namespace webradar
