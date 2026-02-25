#include <algorithm>
#include <queue>

#include "nix/store/store-api.hh"
#include "nix/store/build-result.hh"
#include "nix/util/util.hh"

#include "derivation-check.hh"

namespace nix {

void checkCAFixedOutput(
    StoreDirConfig & store,
    const StorePath & drvPath,
    const DerivationOutput & outputSpec,
    const ValidPathInfo & info,
    Activity & act)
{
    if (const auto * dof = std::get_if<DerivationOutput::CAFixed>(&outputSpec.raw)) {
        auto & wanted = dof->ca.hash;

        /* Check wanted hash */
        assert(info.ca);
        auto & got = info.ca->hash;
        bool hashMismatch = wanted != got;
        if (hashMismatch) {
            act.result(
                resHashMismatch,
                {
                    {"storePath", store.printStorePath(drvPath)},
                    {"wanted", wanted},
                    {"got", got},
                });
        }
        if (!info.references.empty()) {
            std::string references;
            for (auto & reference : info.references)
                references.append("\n  " + store.printStorePath(reference));

            throw BuildError(
                BuildResult::Failure::HashMismatch,
                "the fixed-output derivation '%s' must not reference store paths but "
                "%d such references were found:%s",
                store.printStorePath(drvPath),
                info.references.size(),
                references);
        }

        if (hashMismatch) {
            /* Throw an error after registering the path as
               valid. */
            throw BuildError(
                BuildResult::Failure::HashMismatch,
                "hash mismatch in fixed-output derivation '%s':\n  specified: %s\n     got:    %s",
                store.printStorePath(drvPath),
                wanted.to_string(HashFormat::SRI, true),
                got.to_string(HashFormat::SRI, true));
        }
    }
}

void checkOutputs(
    Store & store,
    const StorePath & drvPath,
    const decltype(Derivation::outputs) & drvOutputs,
    const decltype(DerivationOptions<StorePath>::outputChecks) & outputChecks,
    const std::map<std::string, ValidPathInfo> & outputs,
    Activity & act)
{
    std::map<StorePath, const ValidPathInfo &> outputsByPath;
    for (auto & output : outputs)
        outputsByPath.emplace(output.second.path, output.second);

    for (auto & pair : outputs) {
        // We can't use auto destructuring here because
        // clang-tidy seems to complain about it.
        const std::string & outputName = pair.first;
        const auto & info = pair.second;

        auto * outputSpec = get(drvOutputs, outputName);
        assert(outputSpec);

        checkCAFixedOutput(store, drvPath, *outputSpec, info, act);

        struct Closure
        {
            /* Keys: paths in the closure, values: direct references of that path. */
            std::map<StorePath, StorePathSet> paths;
            uint64_t size;
        };

        /* Compute the closure and closure size of some output. This
           is slightly tricky because some of its references (namely
           other outputs) may not be valid yet. */
        auto getClosure = [&](const StorePath & path) {
            uint64_t closureSize = 0;
            std::map<StorePath, StorePathSet> pathsDone;
            std::queue<StorePath> pathsLeft;
            pathsLeft.push(path);

            while (!pathsLeft.empty()) {
                auto path = pathsLeft.front();
                pathsLeft.pop();
                if (pathsDone.contains(path))
                    continue;

                auto i = outputsByPath.find(path);
                auto & refs = pathsDone[path];
                if (i != outputsByPath.end()) {
                    closureSize += i->second.narSize;
                    for (auto & ref : i->second.references) {
                        pathsLeft.push(ref);
                        refs.insert(ref);
                    }
                } else {
                    auto info = store.queryPathInfo(path);
                    closureSize += info->narSize;
                    for (auto & ref : info->references) {
                        pathsLeft.push(ref);
                        refs.insert(ref);
                    }
                }
            }

            return Closure{
                .paths = std::move(pathsDone),
                .size = closureSize,
            };
        };

        auto applyChecks = [&](const DerivationOptions<StorePath>::OutputChecks & checks) {
            if (checks.maxSize && info.narSize > *checks.maxSize)
                throw BuildError(
                    BuildResult::Failure::OutputRejected,
                    "path '%s' is too large at %d bytes; limit is %d bytes",
                    store.printStorePath(info.path),
                    info.narSize,
                    *checks.maxSize);

            if (checks.maxClosureSize) {
                uint64_t closureSize = getClosure(info.path).size;
                if (closureSize > *checks.maxClosureSize)
                    throw BuildError(
                        BuildResult::Failure::OutputRejected,
                        "closure of path '%s' is too large at %d bytes; limit is %d bytes",
                        store.printStorePath(info.path),
                        closureSize,
                        *checks.maxClosureSize);
            }

            auto checkRefs = [&](const std::set<DrvRef<StorePath>> & value, bool allowed, bool recursive) {
                /* Parse a list of reference specifiers.  Each element must
                   either be a store path, or the symbolic name of the output
                   of the derivation (such as `out'). */
                StorePathSet spec;
                for (auto & i : value) {
                    std::visit(
                        overloaded{
                            [&](const StorePath & path) { spec.insert(path); },
                            [&](const OutputName & refOutputName) {
                                if (auto output = get(outputs, refOutputName))
                                    spec.insert(output->path);
                                else {
                                    std::string outputsListing =
                                        concatMapStringsSep(", ", outputs, [](auto & o) { return o.first; });
                                    throw BuildError(
                                        BuildResult::Failure::OutputRejected,
                                        "derivation '%s' output check for '%s' contains output name '%s',"
                                        " but this is not a valid output of this derivation."
                                        " (Valid outputs are [%s].)",
                                        store.printStorePath(drvPath),
                                        outputName,
                                        refOutputName,
                                        outputsListing);
                                }
                            }},
                        i);
                }

                std::map<StorePath, StorePathSet> used;
                if (recursive) {
                    used = getClosure(info.path).paths;
                } else {
                    for (auto & ref : info.references)
                        used.insert({ref, {}});
                }

                std::set<StorePath> badPaths;

                for (auto & [path, refs] : used) {
                    (void) refs;
                    if (path == info.path && recursive && checks.ignoreSelfRefs)
                        continue;
                    if (allowed) {
                        if (!spec.count(path))
                            badPaths.insert(path);
                    } else {
                        if (spec.count(path))
                            badPaths.insert(path);
                    }
                }

                if (!badPaths.empty()) {
                    std::string badPathsList;
                    for (auto & i : badPaths) {
                        if (!badPathsList.empty())
                            badPathsList += "\n";
                        badPathsList += store.printStorePath(i);
                    }

                    if (recursive) {
                        auto renderChain = [&](const StorePath & target) {
                            std::queue<StorePath> todo;
                            std::set<StorePath> visited;
                            std::map<StorePath, StorePath> prev;

                            todo.push(info.path);
                            visited.insert(info.path);
                            bool found = info.path == target;

                            while (!todo.empty() && !found) {
                                auto cur = todo.front();
                                todo.pop();
                                auto it = used.find(cur);
                                if (it == used.end())
                                    continue;
                                for (auto & next : it->second) {
                                    if (!visited.insert(next).second)
                                        continue;
                                    prev.insert_or_assign(next, cur);
                                    if (next == target) {
                                        found = true;
                                        break;
                                    }
                                    todo.push(next);
                                }
                            }

                            std::vector<StorePath> chain;
                            if (found) {
                                for (auto cur = target;; cur = prev.at(cur)) {
                                    chain.push_back(cur);
                                    if (cur == info.path)
                                        break;
                                }
                                std::reverse(chain.begin(), chain.end());
                            } else {
                                chain = {info.path, target};
                            }

                            std::string graph = store.printStorePath(info.path) + "\n";
                            std::string pad;
                            for (size_t i = 1; i < chain.size(); ++i) {
                                graph += pad + treeLast + store.printStorePath(chain[i]) + "\n";
                                pad += treeNull;
                            }
                            return graph;
                        };

                        std::string badPathRefsTree;
                        for (auto & i : badPaths)
                            badPathRefsTree += renderChain(i);

                        throw BuildError(
                            BuildResult::Failure::OutputRejected,
                            "output '%s' is not allowed to refer to the following paths:\n%s\n\nShown below are chains that lead to the forbidden path(s).\n%s",
                            store.printStorePath(info.path),
                            badPathsList,
                            badPathRefsTree);
                    } else {
                        throw BuildError(
                            BuildResult::Failure::OutputRejected,
                            "output '%s' is not allowed to have direct references to the following paths:\n%s",
                            store.printStorePath(info.path),
                            badPathsList);
                    }
                }
            };

            /* Mandatory check: absent whitelist, and present but empty
               whitelist mean very different things. */
            if (auto & refs = checks.allowedReferences) {
                checkRefs(*refs, true, false);
            }
            if (auto & refs = checks.allowedRequisites) {
                checkRefs(*refs, true, true);
            }

            /* Optimization: don't need to do anything when
               disallowed and empty set. */
            if (!checks.disallowedReferences.empty()) {
                checkRefs(checks.disallowedReferences, false, false);
            }
            if (!checks.disallowedRequisites.empty()) {
                checkRefs(checks.disallowedRequisites, false, true);
            }
        };

        std::visit(
            overloaded{
                [&](const DerivationOptions<StorePath>::OutputChecks & checks) { applyChecks(checks); },
                [&](const std::map<std::string, DerivationOptions<StorePath>::OutputChecks, std::less<>> &
                        checksPerOutput) {
                    if (auto outputChecks = get(checksPerOutput, outputName))

                        applyChecks(*outputChecks);
                },
            },
            outputChecks);
    }
}

} // namespace nix
