#pragma once
// Fetches what tala-engine runs on, into one folder:
//   model/    Phonon-2 (Hugging Face, FermionResearch/Phonon-2), CC-BY-4.0
//   kernels/  Fermion's CPU kernels from the fermion-research package, Apache-2.0
// Both pinned by version and SHA-256, so what runs is what was measured.

#include <functional>
#include <string>

namespace tala::phonon {

// `progress(stage, done, total)` as the bytes come in ("model", "kernels").
bool install(const std::string& dir, const std::function<void(const std::string&, long long, long long)>& progress,
             std::string& error);

// Both halves present and complete.
bool installed(const std::string& dir);

} // namespace tala::phonon
