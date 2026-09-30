#pragma once

#include <QString>
#include <optional>

/// The desktop keyring (Secret Service: GNOME Keyring, KWallet, KeePassXC …) through libsecret.
///
/// Built without libsecret, or run where no Secret Service answers (a bare window manager, a
/// container, CI), every call reports failure and the caller decides what to do instead — the
/// keyring is where secrets should live, not a precondition for the app to work.
///
/// The calls are synchronous D-Bus round trips. They are made at sign-in, at launch and when a
/// token actually changes, never per request.
namespace SecretStore {

/// Whether this build can talk to a keyring at all (compiled with libsecret).
bool compiledIn();

/// Store `value` under `key`, replacing what was there. False when no keyring accepted it.
bool store(const QString &key, const QString &value);

/// The stored value; an empty string when the keyring answered but holds nothing under `key`;
/// nullopt when there is no keyring to ask.
std::optional<QString> lookup(const QString &key);

/// Remove `key`. True when the keyring answered (whether or not anything was stored).
bool remove(const QString &key);

}  // namespace SecretStore
