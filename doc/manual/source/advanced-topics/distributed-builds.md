# Remote Builds

A local Nix installation can forward Nix builds to other machines,
allowing multiple builds to be performed in parallel.

Remote builds also allow Nix to perform multi-platform builds in a
semi-transparent way. For example, if you perform a build for
`aarch64-darwin` on an `x86_64-linux` machine, Nix can automatically
forward the build to an `aarch64-darwin` machine, if one is available.

## Requirements

For a local machine to forward a build to a remote machine, the remote machine must:

- Have Nix installed
- Be running an SSH server, e.g. `sshd`
- Be accessible via SSH from the local machine over the network
- Have the local machine's public SSH key in `/etc/ssh/authorized_keys.d/<username>`
- Have the username of the SSH user in the `trusted-users` setting in `nix.conf`

## Testing

To test connecting to a remote [Nix instance] (in this case `mac`), run:

```console
nix store info --store ssh://username@mac
```

To specify an SSH identity file as part of the remote store URI add a
query parameter, e.g.

```console
nix store info --store ssh://username@mac?ssh-key=/home/alice/my-key
```

Since builds should be non-interactive, the key should not have a
passphrase. Alternatively, you can load identities ahead of time into
`ssh-agent` or `gpg-agent`.

In a multi-user installation (default), builds are executed by the Nix
Daemon. The Nix Daemon cannot prompt for a passphrase via the terminal
or `ssh-agent`, so the SSH key must not have a passphrase.

In addition, the Nix Daemon's user (typically root) needs to have SSH
access to the remote builder.

Access can be verified by running `sudo su`, and then validating SSH
access, e.g. by running `ssh mac`. SSH identity files for root users
are usually stored in `/root/.ssh/` (Linux) or `/var/root/.ssh` (MacOS).

If you get the error

```console
bash: nix: command not found
error: cannot connect to 'mac'
```

then you need to ensure that the `PATH` of non-interactive login shells
contains Nix.

## Configuration

The [list of remote build machines](@docroot@/command-ref/conf-file.md#conf-builders)
can be specified on the command line or in the Nix configuration file.
The former is convenient for testing.

For example, the following command allows you to build a derivation for
`aarch64-darwin` on a Linux machine:

```console
nix build --impure \
  --expr '(with import <nixpkgs> { system = "aarch64-darwin"; }; runCommand "foo" {} "uname > $out")' \
  --builders 'ssh://mac aarch64-darwin'
```

It is possible to specify multiple build machines separated by a semicolon or
newline, e.g.

```console
--builders 'ssh://mac aarch64-darwin ; ssh://beastie x86_64-freebsd'
```

Additionally, there are two supported formats for `builders`:

- The legacy space-separated format.
- A TOML configuration.

Remote build machines can also be configured in [`nix.conf`](@docroot@/command-ref/conf-file.md), e.g.

    builders = ssh://mac aarch64-darwin ; ssh://beastie x86_64-freebsd

After making changes to `nix.conf`, restart the Nix daemon for changes to take effect.

Finally, remote build machines can be configured in a separate configuration
file included in `builders` via the syntax `@/path/to/file`. For example,

    builders = @/etc/nix/machines

causes the list of machines in `/etc/nix/machines` to be included.
(This is the default.)

---

Each machine specification consists of the following attributes. How those are
combined depends on the format.

1. `uri` (**required**)

   The URI of the remote store in the format
   `ssh[-ng]://[username@]hostname[?port=<port>]`, e.g. `ssh://nix@mac` or `ssh://mac`.

2. `system-types` (**optional**)

   A list of Nix platform type identifiers, such as `x86_64-darwin`.
   A machine may support multiple platform types.

   Defaults to the local platform type.

3. `ssh-key` (**optional**)

   The SSH identity file used to log in to the remote machine.

   Defaults to SSH's regular identities.

4. `jobs` (**optional**)

   The maximum number of builds Nix will execute in parallel on that machine.

   Defaults to 1; must be a non-negative integer.

5. `speed-factor` (**optional**)

   Indicates relative machine speed. If multiple machines match, Nix prefers
   faster machines while accounting for load.

   Defaults to 1; must be a non-negative number.

6. `supported-features` (**optional**)

   A list of supported features. If a derivation declares
   `requiredSystemFeatures`, it is only scheduled onto machines supporting
   those features.

7. `mandatory-features` (**optional**)

   A list of mandatory features. A machine is only used when all of its
   mandatory features appear in the derivation's `requiredSystemFeatures`.

8. `ssh-public-host-key` (**optional**)

   The remote machine public host key.

   Defaults to standard SSH known-hosts behavior when omitted.

9. `enable` (**optional**, TOML only)

   If set to `false`, the machine is statically disabled and not loaded.

   Defaults to `true`.

### Using a TOML configuration

Each machine is configured as a key under `machines`:

```toml
version = 1

[machines.andesite]
uri = "ssh://nix@andesite.example.org"
system-types = ["x86_64-linux"]
jobs = 8
speed-factor = 1.0
supported-features = ["kvm"]
ssh-key = "/home/nix/.ssh/id_ed25519"

[machines.diorite]
uri = "ssh://nix@diorite.example.org"
system-types = ["x86_64-linux"]
jobs = 8
speed-factor = 2.0
ssh-key = "/home/nix/.ssh/id_ed25519"

[machines.legacy]
uri = "ssh://nix@old-builder.example.org"
enable = false
```

For ad-hoc CLI usage, TOML can also be provided inline, for example:

```console
--builders 'machines.andesite = { uri = "ssh://nix@andesite.example.org", jobs = 8 }'
```

> **Note**
>
> If `version` is omitted (for example, in ad-hoc CLI input), it defaults to
> the latest supported version. For file-based config, providing `version` is
> recommended for forward compatibility.

### Using the legacy format

> **Warning**
>
> This format is frozen and new options are expected to land only in TOML.

The legacy format uses positional, space-separated fields in the order listed
above. Use `-` to leave a field at its default.

```text
nix@andesite.example.org  x86_64-linux  /home/nix/.ssh/id_ed25519  8 1 kvm
nix@diorite.example.org   x86_64-linux  /home/nix/.ssh/id_ed25519  8 2
nix@granite.example.org   x86_64-linux  /home/nix/.ssh/id_ed25519  1 2 kvm benchmark
```

Special handling:

- `uri`: for backward compatibility, `ssh://` may be omitted.
- `ssh-public-host-key`: key must be base64 encoded in legacy format.

### Format detection

Nix first tries parsing `builders` as TOML.
If TOML parsing fails and the input appears to be clearly TOML (for example,
it contains `"`), a TOML error is reported.
Otherwise, Nix retries using the legacy parser.

### Builder selection

Given machines like the above, `granite` will only build derivations that
require its mandatory features, e.g.

```nix
requiredSystemFeatures = [ "benchmark" ];
```

or

```nix
requiredSystemFeatures = [ "benchmark" "kvm" ];
```

`diorite` cannot do builds that require `kvm`, while `andesite` can. For
regular builds, `diorite` is preferred over `andesite` because it has a
higher speed factor.

[Nix instance]: @docroot@/glossary.md#gloss-nix-instance
