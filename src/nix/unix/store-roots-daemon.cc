#include "nix/cmd/command.hh"
#include "nix/cmd/unix-socket-server.hh"
#include "nix/store/local-store.hh"
#include "nix/store/store-api.hh"
#include "nix/store/local-gc.hh"
#include "nix/util/environment-variables.hh"
#include "nix/util/file-descriptor.hh"

#include <fcntl.h>
#include <thread>

namespace nix {

static constexpr int ROOTS_LISTEN_FD = 3;

static void streamRoots(const LocalStoreConfig & localStoreConfig, AutoCloseFD remote)
{
    auto roots = findRuntimeRootsUnchecked(localStoreConfig);

    FdSink sink(remote.get());

    for (auto & [key, _] : roots) {
        sink(localStoreConfig.printStorePath(key));
        sink(std::string_view("\0", 1));
    }

    sink.flush();
}

static int getRootsSocketActivationConnection()
{
    auto listenFds = getEnv("LISTEN_FDS");
    if (listenFds) {
        if (getEnv("LISTEN_PID") != std::to_string(getpid()) || listenFds != "1")
            throw Error("unexpected systemd environment variables");
        unix::closeOnExec(ROOTS_LISTEN_FD);
        return ROOTS_LISTEN_FD;
    }

    if (fcntl(ROOTS_LISTEN_FD, F_GETFD) != -1)
        return ROOTS_LISTEN_FD;

    throw Error("expected socket-activated connection on file descriptor %1%", ROOTS_LISTEN_FD);
}

static void rootsDaemonLoop(const LocalStoreConfig & localStoreConfig)
{
    auto gcSocketPath = localStoreConfig.getRootsSocketPath();

    unix::serveUnixSocket(
        {
            .socketPath = gcSocketPath,
            .socketMode = 0666,
            .activationName = "nix-roots-daemon.socket",
        },
        [&](AutoCloseFD remote, std::function<void()> closeListeners) {
            std::thread([&, remote = std::move(remote)]() mutable {
                streamRoots(localStoreConfig, std::move(remote));
            }).detach();
        });
}

static void rootsDaemonInstance(const LocalStoreConfig & localStoreConfig)
{
    streamRoots(localStoreConfig, AutoCloseFD(getRootsSocketActivationConnection()));
}

struct CmdRootsDaemon : StoreConfigCommand
{
    bool socketActivatedInstance = false;

    CmdRootsDaemon()
    {
        addFlag({
            .longName = "for-socket-activation",
            .description = "Handle a single socket-activated connection on file descriptor 3 and exit.",
            .handler = {&socketActivatedInstance, true},
        });
    }

    std::string description() override
    {
        return "run a daemon that returns garbage collector roots on request";
    }

    std::string doc() override
    {
        return
#include "store-roots-daemon.md"
            ;
    }

    std::optional<ExperimentalFeature> experimentalFeature() override
    {
        return Xp::LocalOverlayStore;
    }

    void run(ref<StoreConfig> storeConfig) override
    {
        auto localStoreConfig = dynamic_cast<LocalStoreConfig *>(&*storeConfig);
        if (!localStoreConfig) {
            throw UsageError(
                "Roots daemon only functions with a local store, not '%s'", storeConfig->getHumanReadableURI());
        }

        if (socketActivatedInstance)
            rootsDaemonInstance(*localStoreConfig);
        else
            rootsDaemonLoop(*localStoreConfig);
    }
};

static auto rCmdStoreRootsDaemon = registerCommand2<CmdRootsDaemon>({"store", "roots-daemon"});

} // namespace nix
