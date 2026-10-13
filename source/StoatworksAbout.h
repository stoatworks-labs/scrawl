/*
 * Stoatworks Labs - About window data for Scrawl.
 *
 * PROVISIONAL HAND COPY (2026-10-13), adapted from jackpot's generated one,
 * because Scrawl is not yet in projects.json. Registering it replaces this
 * with stoatworks-backend/scripts/sync-about.py's output; do not hand-edit
 * after that. There is no user guide yet, so `guide` is empty and the About
 * block has three buttons.
 *
 * `version` here is a fallback read from this repo's own manifest at sync
 * time. Anything with a build step injects the real one at build time and
 * overrides this.
 */
#pragma once

namespace stoatworks::about
{
    inline constexpr auto name = "Scrawl";
    inline constexpr auto slug = "scrawl";
    inline constexpr auto hook = "Hand-drawn doodles to the music, for Resolume";
    inline constexpr auto licence = "MIT";
    inline constexpr auto guide = "";
    inline constexpr auto page = "https://stoatworks-labs.com/software/scrawl/";
    inline constexpr auto repo = "https://github.com/stoatworks-labs/scrawl";
    inline constexpr auto versionFallback = "v0.1.0";

    inline constexpr auto org = "Stoatworks Labs";
    inline constexpr auto home = "https://stoatworks-labs.com";
    inline constexpr auto tagline = "Open tools for the people who run the show.";

    /* The canonical funding links, matching FUNDING.yml and the support footer. */
    struct Link { const char* name; const char* url; };
    inline constexpr Link funding[] = {
        { "GitHub Sponsors", "https://github.com/sponsors/stoatworks-labs" },
        { "Ko-fi", "https://ko-fi.com/stoatworkslabs" },
        { "Patreon", "https://patreon.com/StoatworksLabs" },
        { "Liberapay", "https://liberapay.com/stoatworks-labs" },
    };
}
