#include "nix/util/base-n.hh"
#include "nix/util/strings.hh"
#include "nix/store/machines.hh"
#include "nix/store/globals.hh"
#include "nix/store/store-open.hh"

#include <algorithm>
#include <optional>
#include <span>
#include <toml.hpp>

namespace nix {

static std::string normalizeMachineStoreUri(std::string storeUri)
{
    // Backwards compatibility: if the URI is schemeless, is not a path,
    // and is not one of the special store connection words, prepend
    // ssh://.
    if (storeUri.find("://") != std::string::npos || storeUri.find("/") != std::string::npos || storeUri == "auto"
        || storeUri == "daemon" || storeUri == "local" || hasPrefix(storeUri, "auto?") || hasPrefix(storeUri, "daemon?")
        || hasPrefix(storeUri, "local?") || hasPrefix(storeUri, "?"))
        return storeUri;
    return "ssh://" + storeUri;
}

bool Machine::systemSupported(const std::string & system) const
{
    return system == "builtin" || (systemTypes.count(system) > 0);
}

bool Machine::allSupported(const StringSet & features) const
{
    return std::all_of(features.begin(), features.end(), [&](const std::string & feature) {
        return supportedFeatures.count(feature) || mandatoryFeatures.count(feature);
    });
}

bool Machine::mandatoryMet(const StringSet & features) const
{
    return std::all_of(mandatoryFeatures.begin(), mandatoryFeatures.end(), [&](const std::string & feature) {
        return features.count(feature);
    });
}

StoreReference Machine::completeStoreReference() const
{
    auto storeUri = this->storeUri;

    auto * generic = std::get_if<StoreReference::Specified>(&storeUri.variant);

    if (generic && generic->scheme == "ssh") {
        // Remote builds become flakey, when having more than one ssh connection.
        storeUri.params["max-connections"] = "1";
        storeUri.params["log-fd"] = "4";
    }

    if (generic && (generic->scheme == "ssh" || generic->scheme == "ssh-ng")) {
        if (sshKey)
            storeUri.params["ssh-key"] = sshKey->string();
        if (sshPublicHostKey != "")
            storeUri.params["base64-ssh-public-host-key"] = sshPublicHostKey;
    }

    {
        auto & fs = storeUri.params["system-features"];
        auto append = [&](auto feats) {
            for (auto & f : feats) {
                if (fs.size() > 0)
                    fs += ' ';
                fs += f;
            }
        };
        append(supportedFeatures);
        append(mandatoryFeatures);
    }

    return storeUri;
}

ref<Store> Machine::openStore() const
{
    return nix::openStore(completeStoreReference());
}

namespace machines_legacy_parsing {

static std::vector<std::string> expandBuilderLines(const std::string & builders)
{
    std::vector<std::string> result;
    for (auto line : tokenizeString<std::vector<std::string>>(builders, "\n")) {
        line.erase(std::find(line.begin(), line.end(), '#'), line.end());
        for (auto entry : tokenizeString<std::vector<std::string>>(line, ";")) {
            entry = trim(entry);

            if (entry.empty()) {
                // skip blank entries
            } else if (entry[0] == '@') {
                const std::string path = trim(std::string_view{entry}.substr(1));
                std::string text;
                try {
                    text = readFile(path);
                } catch (const SystemError & e) {
                    if (!e.is(std::errc::no_such_file_or_directory))
                        throw;
                    debug("cannot find machines file '%s'", path);
                    continue;
                }

                const auto entrys = expandBuilderLines(text);
                result.insert(end(result), begin(entrys), end(entrys));
            } else {
                result.emplace_back(entry);
            }
        }
    }
    return result;
}

static Machine parseBuilderLine(const StringSet & defaultSystems, const std::string & line)
{
    const auto tokens = tokenizeString<std::vector<std::string>>(line);

    auto isSet = [&](size_t fieldIndex) {
        return tokens.size() > fieldIndex && tokens[fieldIndex] != "" && tokens[fieldIndex] != "-";
    };

    auto parseUnsignedIntField = [&](size_t fieldIndex) {
        const auto result = string2Int<unsigned int>(tokens[fieldIndex]);
        if (!result) {
            throw FormatError(
                "bad machine specification: failed to convert column #%lu in a row: '%s' to 'unsigned int'",
                fieldIndex,
                line);
        }
        return result.value();
    };

    auto parseFloatField = [&](size_t fieldIndex) {
        const auto result = string2Float<float>(tokens[fieldIndex]);
        if (!result) {
            throw FormatError(
                "bad machine specification: failed to convert column #%lu in a row: '%s' to 'float'", fieldIndex, line);
        }
        return result.value();
    };

    auto ensureBase64 = [&](size_t fieldIndex) {
        const auto & str = tokens[fieldIndex];
        try {
            base64::decode(str);
        } catch (FormatError & e) {
            e.addTrace({}, "while parsing machine specification at a column #%lu in a row: '%s'", fieldIndex, line);
            throw;
        }
        return str;
    };

    if (!isSet(0))
        throw FormatError(
            "bad machine specification: store URL was not found at the first column of a row: '%s'", line);

    auto storeUri = normalizeMachineStoreUri(tokens[0]);

    auto systemTypes = isSet(1) ? tokenizeString<StringSet>(tokens[1], ",") : defaultSystems;
    auto sshKey = isSet(2) ? tokens[2] : "";
    auto maxJobs = isSet(3) ? parseUnsignedIntField(3) : 1U;
    auto speedFactor = isSet(4) ? parseFloatField(4) : 1.0f;
    auto supportedFeatures = isSet(5) ? tokenizeString<StringSet>(tokens[5], ",") : StringSet{};
    auto mandatoryFeatures = isSet(6) ? tokenizeString<StringSet>(tokens[6], ",") : StringSet{};
    auto sshPublicHostKey = isSet(7) ? ensureBase64(7) : "";

    speedFactor = speedFactor == 0.0f ? 1.0f : speedFactor;
    if (speedFactor < 0.0f)
        throw UsageError("speed factor must be >= 0");

    return {
        StoreReference::parse(storeUri),
        systemTypes,
        sshKey.empty() ? std::nullopt : std::make_optional<std::filesystem::path>(sshKey),
        maxJobs,
        speedFactor,
        supportedFeatures,
        mandatoryFeatures,
        sshPublicHostKey,
    };
}

static Machines parseBuilderLines(const StringSet & defaultSystems, const std::vector<std::string> & builders)
{
    Machines result;
    std::transform(builders.begin(), builders.end(), std::back_inserter(result), [&](auto && line) {
        return parseBuilderLine(defaultSystems, line);
    });
    return result;
}

static Machines getMachines()
{
    const auto builderLines = expandBuilderLines(settings.getWorkerSettings().builders);
    return parseBuilderLines({settings.thisSystem}, builderLines);
}

} // namespace machines_legacy_parsing

Machines Machine::parseConfig(const StringSet & defaultSystems, const std::string & s)
{
    const auto builderLines = machines_legacy_parsing::expandBuilderLines(s);
    return machines_legacy_parsing::parseBuilderLines(defaultSystems, builderLines);
}

namespace machines_toml_parsing {

static constexpr int MIN_VERSION = 1;
static constexpr int LATEST_VERSION = 1;

template<typename T>
static std::optional<T>
parseRequired(const toml::value & data, const std::string & key, std::vector<std::string> & errors)
{
    if (!data.contains(key)) {
        auto ei = toml::make_error_info(fmt("%s must be present", key), data, "but was not set");
        errors.push_back(toml::format_error(ei));
        return std::nullopt;
    }
    try {
        return toml::find<T>(data, key);
    } catch (const toml::type_error & e) {
        errors.push_back(e.what());
        return std::nullopt;
    }
}

template<typename T>
static std::optional<T>
parseOptional(const toml::value & data, const std::string & key, T defaultValue, std::vector<std::string> & errors)
{
    if (!data.contains(key))
        return defaultValue;
    try {
        return toml::find<T>(data, key);
    } catch (const toml::type_error & e) {
        errors.push_back(e.what());
        return std::nullopt;
    }
}

static std::optional<float> parseSpeedFactor(const toml::value & data, std::vector<std::string> & errors)
{
    if (!data.contains("speed-factor"))
        return 1.0f;
    const auto & value = data.at("speed-factor");
    if (value.is_integer())
        return static_cast<float>(value.as_integer());
    if (value.is_floating())
        return static_cast<float>(value.as_floating());
    auto ei =
        toml::make_error_info("bad_cast to floating for `speed-factor`", value, "was neither an integer nor a float");
    errors.push_back(toml::format_error(ei));
    return std::nullopt;
}

static std::optional<Machine> parseMachine(const toml::value & data, std::vector<std::string> & errors)
{
    static const StringSet expectedKeys = {
        "uri",
        "system-types",
        "ssh-key",
        "jobs",
        "speed-factor",
        "supported-features",
        "mandatory-features",
        "ssh-public-host-key",
        "enable",
    };

    if (!data.is_table()) {
        auto ei = toml::make_error_info(
            "Each machine must be a table", data, "this should be a table. Did you mean `.uri = ...`?");
        errors.push_back(toml::format_error(ei));
        return std::nullopt;
    }

    auto storeUriString = parseRequired<std::string>(data, "uri", errors);
    auto systemTypes = parseOptional<std::vector<std::string>>(
        data, "system-types", std::vector<std::string>{settings.thisSystem}, errors);
    auto sshKey = parseOptional<std::string>(data, "ssh-key", "", errors);
    auto jobs = parseOptional<int64_t>(data, "jobs", 1, errors);
    auto speedFactor = parseSpeedFactor(data, errors);
    auto supportedFeatures = parseOptional<std::vector<std::string>>(data, "supported-features", {}, errors);
    auto mandatoryFeatures = parseOptional<std::vector<std::string>>(data, "mandatory-features", {}, errors);
    auto sshPublicHostKey = parseOptional<std::string>(data, "ssh-public-host-key", "", errors);

    if (jobs && *jobs < 0) {
        auto ei = toml::make_error_info("jobs must be >= 0", data.at("jobs"), "but got negative value");
        errors.push_back(toml::format_error(ei));
    }
    if (speedFactor && *speedFactor < 0.0f) {
        auto ei = toml::make_error_info("speed factor must be >= 0", data.at("speed-factor"), "but got negative value");
        errors.push_back(toml::format_error(ei));
    }

    for (const auto & [key, value] : data.as_table()) {
        if (!expectedKeys.contains(key)) {
            auto ei = toml::make_error_info(fmt("unexpected key `%s`", key), value, "should not be present");
            errors.push_back(toml::format_error(ei));
        }
    }

    if (!errors.empty())
        return std::nullopt;

    StoreReference storeUri;
    try {
        storeUri = StoreReference::parse(normalizeMachineStoreUri(*storeUriString));
    } catch (const std::exception & e) {
        errors.push_back(fmt("invalid store URI `%s`: %s", *storeUriString, e.what()));
        return std::nullopt;
    }

    std::string sshPublicHostKeyBase64;
    if (!sshPublicHostKey->empty()) {
        std::span<const char> hostKeyBytes(sshPublicHostKey->data(), sshPublicHostKey->size());
        sshPublicHostKeyBase64 = base64::encode(std::as_bytes(hostKeyBytes));
    }

    return Machine{
        storeUri,
        StringSet(systemTypes->begin(), systemTypes->end()),
        sshKey->empty() ? std::nullopt : std::make_optional<std::filesystem::path>(*sshKey),
        static_cast<unsigned int>(*jobs),
        *speedFactor,
        StringSet(supportedFeatures->begin(), supportedFeatures->end()),
        StringSet(mandatoryFeatures->begin(), mandatoryFeatures->end()),
        sshPublicHostKeyBase64,
    };
}

static std::optional<Machines> parseToml(const toml::value & data)
{
    std::vector<std::string> errors;
    Machines machines;

    if (data.size() == 0)
        return machines;

    if (!data.is_table()) {
        errors.push_back("Top level TOML value must be a table");
        throw UsageError("invalid Machines TOML:\n%s", concatStringsSep("\n", errors));
    }

    int64_t version = LATEST_VERSION;
    if (data.contains("version")) {
        try {
            version = toml::find<int64_t>(data, "version");
        } catch (const toml::type_error & e) {
            errors.push_back(e.what());
        }
    }

    if (version < MIN_VERSION || version > LATEST_VERSION) {
        errors.push_back(
            fmt("Unable to parse Machines of version %d, only versions between %d and %d are supported.",
                version,
                MIN_VERSION,
                LATEST_VERSION));
    }

    for (const auto & [key, _] : data.as_table()) {
        if (key != "version" && key != "machines")
            errors.push_back(fmt("unexpected keys found: %s", key));
    }

    if (!data.contains("machines") || !data.at("machines").is_table()) {
        errors.push_back("Expected key `machines` to be a table of name -> machine configurations");
    }

    if (!errors.empty())
        throw UsageError("invalid Machines TOML:\n%s", concatStringsSep("\n", errors));

    for (const auto & [name, machineData] : data.at("machines").as_table()) {
        std::vector<std::string> machineErrors;
        auto machine = parseMachine(machineData, machineErrors);

        if (machine) {
            auto enable = parseOptional<bool>(machineData, "enable", true, machineErrors);
            if (enable && *enable)
                machines.push_back(*machine);
        }

        if (!machineErrors.empty()) {
            errors.push_back(fmt("for machine %s:", name));
            errors.insert(errors.end(), machineErrors.begin(), machineErrors.end());
        }
    }

    if (!errors.empty())
        throw UsageError("invalid Machines TOML:\n%s", concatStringsSep("\n", errors));

    return machines;
}

static std::optional<Machines> getMachines()
{
    toml::value data;
    std::string builders = settings.getWorkerSettings().builders;

    try {
        if (!builders.empty() && builders[0] == '@')
            data = toml::parse(builders.substr(1));
        else
            data = toml::parse_str(builders);
    } catch (const toml::syntax_error & e) {
        if (toLower(builders).find("toml") != std::string::npos || builders.find('"') != std::string::npos)
            throw UsageError("invalid Machines TOML syntax:\n%s", e.what());
        return std::nullopt;
    } catch (const toml::file_io_error &) {
        return std::nullopt;
    }

    return parseToml(data);
}

} // namespace machines_toml_parsing

Machines getMachines()
{
    if (auto tomlMachines = machines_toml_parsing::getMachines())
        return *tomlMachines;

    debug("Trying again with legacy format");
    return machines_legacy_parsing::getMachines();
}

} // namespace nix
