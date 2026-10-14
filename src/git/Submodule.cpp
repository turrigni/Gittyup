//
//          Copyright (c) 2016, Scientific Toolworks, Inc.
//
// This software is licensed under the MIT License. The LICENSE.md file
// describes the conditions under which this software may be distributed.
//
// Author: Jason Haslam
//

#include "Submodule.h"
#include "Config.h"
#include "Id.h"
#include "Repository.h"
#include <QFile>
#include <QFileInfo>

namespace git {

namespace {

QStringList submoduleConfigKeys(const Config &config, const QString &name) {
  QStringList keys;
  Config::Iterator iterator = config.glob("submodule.*");
  QString prefix = QString("submodule.%1.").arg(name);
  while (Config::Entry entry = iterator.next()) {
    if (entry.name().startsWith(prefix))
      keys.append(entry.name());
  }
  return keys;
}

void removeSubmoduleConfig(Config &config, const QString &name) {
  for (const QString &key : submoduleConfigKeys(config, name))
    config.remove(key);
}

bool hasSubmoduleConfig(const Config &config) {
  return config.glob("submodule.*").next().isValid();
}

} // namespace

const QString kUrl = "https://github.com/Murmele/Gittyup";

Submodule::Submodule() {}

Submodule::Submodule(git_submodule *submodule)
    : d(submodule, git_submodule_free) {}

Submodule::operator git_submodule *() const { return d.data(); }

bool Submodule::isInitialized() const {
  Repository repo(git_submodule_owner(d.data()));
  QString key = QString("submodule.%1.url").arg(name());
  return !repo.gitConfig().value<QString>(key).isEmpty();
}

void Submodule::initialize() const { git_submodule_init(d.data(), false); }

void Submodule::deinitialize() const {
  // Remove git config entry.
  Repository repo(git_submodule_owner(d.data()));
  Config config = repo.gitConfig();
  QString regex = QString("submodule\\.%1\\..*").arg(name());
  Config::Iterator it = config.glob(regex);
  while (Config::Entry entry = it.next())
    config.remove(entry.name());

  // Remove submodule workdir.
  QDir dir = repo.workdir();
  if (dir.cd(path()) && dir.removeRecursively())
    dir.mkpath(".");
}

QString Submodule::name() const { return git_submodule_name(d.data()); }

QString Submodule::path() const { return git_submodule_path(d.data()); }

QString Submodule::url() const { return git_submodule_url(d.data()); }

void Submodule::setUrl(const QString &url) {
  if (url == this->url())
    return;

  QByteArray buffer = url.toUtf8();
  const char *data = !buffer.isEmpty() ? buffer.constData() : nullptr;
  git_repository *repo = git_submodule_owner(d.data());
  git_submodule_set_url(repo, git_submodule_name(d.data()), data);
}

QString Submodule::branch() const { return git_submodule_branch(d.data()); }

void Submodule::setBranch(const QString &branch) {
  if (branch == this->branch())
    return;

  QByteArray buffer = branch.toUtf8();
  const char *data = !buffer.isEmpty() ? buffer.constData() : nullptr;
  git_repository *repo = git_submodule_owner(d.data());
  git_submodule_set_branch(repo, git_submodule_name(d.data()), data);
}

Id Submodule::headId() const { return git_submodule_head_id(d.data()); }

Id Submodule::indexId() const { return git_submodule_index_id(d.data()); }

Id Submodule::workdirId() const { return git_submodule_wd_id(d.data()); }

Result Submodule::update(Remote::Callbacks *callbacks, bool init,
                         bool checkout_force) {
  git_submodule_update_options opts = GIT_SUBMODULE_UPDATE_OPTIONS_INIT;
#ifndef USE_SYSTEM_LIBGIT2
  opts.fetch_opts.callbacks.connected = &Remote::Callbacks::connected;
  opts.fetch_opts.callbacks.about_to_disconnect =
      &Remote::Callbacks::about_to_disconnect;
#endif
  opts.fetch_opts.callbacks.sideband_progress = &Remote::Callbacks::sideband;
  opts.fetch_opts.callbacks.credentials = &Remote::Callbacks::credentials;
  opts.fetch_opts.callbacks.certificate_check = &Remote::Callbacks::certificate;
  opts.fetch_opts.callbacks.transfer_progress = &Remote::Callbacks::transfer;
  opts.fetch_opts.callbacks.update_tips = &Remote::Callbacks::update;
  opts.fetch_opts.callbacks.remote_ready = &Remote::Callbacks::remoteReady;
  opts.fetch_opts.callbacks.payload = callbacks;

  if (checkout_force)
    opts.checkout_opts.checkout_strategy |= GIT_CHECKOUT_FORCE;
  // Use a fake URL. Submodule update doesn't have a way to
  // query a different proxy for each submodule remote.
  QByteArray proxy = Remote::proxyUrl(kUrl, opts.fetch_opts.proxy_opts.type);
  opts.fetch_opts.proxy_opts.url = proxy;

  return git_submodule_update(d.data(), init, &opts);
}

Result Submodule::add(const Repository &repo, const QString &url,
                      const QString &path, bool initialize, int depth,
                      Remote::Callbacks *callbacks) {
  git_submodule *raw = nullptr;
  QByteArray urlBuffer = url.toUtf8();
  QByteArray pathBuffer = path.toUtf8();
  int error = git_submodule_add_setup(&raw, repo, urlBuffer.constData(),
                                      pathBuffer.constData(), true);
  if (error < 0)
    return Result(error);

  Submodule submodule(raw);
  if (!initialize)
    return Result(0);

  git_submodule_update_options opts = GIT_SUBMODULE_UPDATE_OPTIONS_INIT;
#ifndef USE_SYSTEM_LIBGIT2
  opts.fetch_opts.callbacks.connected = &Remote::Callbacks::connected;
  opts.fetch_opts.callbacks.about_to_disconnect =
      &Remote::Callbacks::about_to_disconnect;
#endif
  opts.fetch_opts.callbacks.sideband_progress = &Remote::Callbacks::sideband;
  opts.fetch_opts.callbacks.credentials = &Remote::Callbacks::credentials;
  opts.fetch_opts.callbacks.certificate_check = &Remote::Callbacks::certificate;
  opts.fetch_opts.callbacks.transfer_progress = &Remote::Callbacks::transfer;
  opts.fetch_opts.callbacks.update_tips = &Remote::Callbacks::update;
  opts.fetch_opts.callbacks.remote_ready = &Remote::Callbacks::remoteReady;
  opts.fetch_opts.callbacks.payload = callbacks;
  opts.fetch_opts.depth = depth;

  git_proxy_t proxyType = GIT_PROXY_AUTO;
  QByteArray proxy = Remote::proxyUrl(url, proxyType);
  opts.fetch_opts.proxy_opts.type = proxyType;
  opts.fetch_opts.proxy_opts.url = proxy;

  error = git_submodule_clone(nullptr, submodule.d.data(), &opts);
  if (error < 0)
    return Result(error);

  return Result(git_submodule_add_finalize(submodule.d.data()));
}

Result Submodule::remove() const {
  Repository repo(git_submodule_owner(d.data()));
  QString submoduleName = name();
  QString submodulePath = path();
  QString modulesFilePath = repo.workdir().filePath(".gitmodules");

  {
    Config config = repo.gitConfig();
    removeSubmoduleConfig(config, submoduleName);
  }

  bool modulesFileExists = QFileInfo::exists(modulesFilePath);
  bool hasOtherSubmodules = false;
  if (modulesFileExists) {
    Config modulesConfig = Config::open(modulesFilePath);
    if (!modulesConfig.isValid())
      return Result(-1, "Unable to open .gitmodules.");

    removeSubmoduleConfig(modulesConfig, submoduleName);
    hasOtherSubmodules = hasSubmoduleConfig(modulesConfig);
    if (!hasOtherSubmodules && !QFile::remove(modulesFilePath))
      return Result(-1, "Unable to remove .gitmodules.");
  }

  git_index *rawIndex = nullptr;
  int error = git_repository_index(&rawIndex, repo);
  if (error < 0)
    return Result(error);

  QSharedPointer<git_index> index(rawIndex, git_index_free);
  QByteArray pathBuffer = submodulePath.toUtf8();
  if (git_index_get_bypath(index.data(), pathBuffer.constData(), 0) &&
      (error = git_index_remove_bypath(index.data(), pathBuffer.constData())) <
          0)
    return Result(error);

  QByteArray modulesFileBuffer = QByteArrayLiteral(".gitmodules");
  if (hasOtherSubmodules) {
    error = git_index_add_bypath(index.data(), modulesFileBuffer.constData());
    if (error < 0)
      return Result(error);
  } else if (git_index_get_bypath(index.data(), modulesFileBuffer.constData(),
                                  0) &&
             (error = git_index_remove_bypath(
                  index.data(), modulesFileBuffer.constData())) < 0) {
    return Result(error);
  }

  error = git_index_write(index.data());
  if (error < 0)
    return Result(error);

  Repository submoduleRepo = open();
  QString submoduleGitDir =
      submoduleRepo.isValid() ? submoduleRepo.dir().absolutePath() : QString();
  submoduleRepo = Repository();

  if (!submoduleGitDir.isEmpty()) {
    QDir gitDir(submoduleGitDir);
    if (gitDir.exists() && !gitDir.removeRecursively())
      return Result(-1, QString("Unable to remove submodule data at '%1'.")
                             .arg(submoduleGitDir));
  }

  QDir workdir(repo.workdir().filePath(submodulePath));
  if (workdir.exists() && !workdir.removeRecursively())
    return Result(-1, QString("Unable to remove submodule files at '%1'.")
                           .arg(workdir.absolutePath()));

  return Result(0);
}

Repository Submodule::open() const {
  git_repository *repo = nullptr;
  git_submodule_open(&repo, d.data());
  return Repository(repo);
}

} // namespace git
