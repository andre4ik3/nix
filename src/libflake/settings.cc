#include <vector>
#include <cstdlib>
#include <nlohmann/json.hpp>

#include "nix/flake/settings.hh"
#include "nix/flake/flake-primops.hh"
#include "nix/expr/eval-settings.hh"
#include "nix/expr/eval.hh"
#include "nix/util/args.hh"
#include "nix/util/abstract-setting-to-json.hh"
#include "nix/util/config-impl.hh"

namespace nix::flake {

NLOHMANN_JSON_SERIALIZE_ENUM(
    AcceptFlakeConfig,
    {
        {AcceptFlakeConfig::True, true},
        {AcceptFlakeConfig::Ask, "ask"},
        {AcceptFlakeConfig::False, false},
    });

} // namespace nix::flake

namespace nix {

template<>
flake::AcceptFlakeConfig BaseSetting<flake::AcceptFlakeConfig>::parse(const std::string & str) const
{
    if (str == "true")
        return flake::AcceptFlakeConfig::True;
    else if (str == "ask")
        return flake::AcceptFlakeConfig::Ask;
    else if (str == "false")
        return flake::AcceptFlakeConfig::False;
    else
        throw UsageError("option '%s' has invalid value '%s'", name, str);
}

template<>
std::string BaseSetting<flake::AcceptFlakeConfig>::to_string() const
{
    if (value == flake::AcceptFlakeConfig::True)
        return "true";
    else if (value == flake::AcceptFlakeConfig::Ask)
        return "ask";
    else if (value == flake::AcceptFlakeConfig::False)
        return "false";
    else
        abort();
}

template<>
void BaseSetting<flake::AcceptFlakeConfig>::convertToArg(Args & args, const std::string & category)
{
    args.addFlag({
        .longName = name,
        .aliases = aliases,
        .description = "Accept Nix configuration options from flakes without confirmation.",
        .category = category,
        .handler = {[this]() { override(flake::AcceptFlakeConfig::True); }},
        .experimentalFeature = experimentalFeature,
    });
    args.addFlag({
        .longName = "ask-" + name,
        .description = "Ask whether to accept Nix configuration options from flakes.",
        .category = category,
        .handler = {[this]() { override(flake::AcceptFlakeConfig::Ask); }},
        .experimentalFeature = experimentalFeature,
    });
    args.addFlag({
        .longName = "no-" + name,
        .description = "Reject Nix configuration options from flakes.",
        .category = category,
        .handler = {[this]() { override(flake::AcceptFlakeConfig::False); }},
        .experimentalFeature = experimentalFeature,
    });
}

/* Explicit instantiation of templates */
template class BaseSetting<flake::AcceptFlakeConfig>;

} // namespace nix

namespace nix::flake {

Settings::Settings() {}

void Settings::configureEvalSettings(nix::EvalSettings & evalSettings) const
{
    evalSettings.extraPrimOps.emplace_back(primops::getFlake(*this));
    evalSettings.extraPrimOps.emplace_back(primops::parseFlakeRef);
    evalSettings.extraPrimOps.emplace_back(primops::flakeRefToString);
}

} // namespace nix::flake
