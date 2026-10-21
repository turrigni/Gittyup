//
//          Copyright (c) 2018, Scientific Toolworks, Inc.
//
// This software is licensed under the MIT License. The LICENSE.md file
// describes the conditions under which this software may be distributed.
//
// Author: Jason Haslam
//

#include "SideBar.h"
#include "Footer.h"
#include "MainWindow.h"
#include "ProgressIndicator.h"
#include "RepoView.h"
#include "TabWidget.h"
#include "app/Application.h"
#include "conf/Settings.h"
#include "conf/RecentRepositories.h"
#include "conf/RecentRepository.h"
#include "dialogs/AccountDialog.h"
#include "dialogs/CloneDialog.h"
#include "git/Repository.h"
#include "git/Submodule.h"
#include "host/Accounts.h"
#include <QAbstractItemModel>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHash>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSet>
#include <QSettings>
#include <QStyledItemDelegate>
#include <QTreeView>
#include <QVBoxLayout>

namespace {

const QString kRemoteExpandedGroup = "remote/expanded";
const QString kOpenExpandedGroup = "open/expanded";
const QString kRecentExpandedGroup = "recent/expanded";
const QString kGroupedViewKey = "sidebar/group-repositories";

const QString kStyleSheet = "QTreeView {"
#ifdef Q_OS_MAC
                            "  background: palette(midlight);"
#endif
                            "  border: none"
                            "}"
                            "Footer {"
                            "  border-left: none;"
                            "  border-right: none"
                            "}";

enum Role {
  PathRole = Qt::UserRole,
  TabRole,
  RecentRole,
  AccountRole,
  AccountKindRole,
  RepositoryRole,
  GroupPathRole,
  OpenGroupPathRole
};

class OpenGroup : public QObject {
  Q_OBJECT

public:
  OpenGroup(const QString &path, QObject *parent = nullptr)
      : QObject(parent), mPath(path) {}

  QString path() const { return mPath; }
  QList<RepoView *> children;

private:
  QString mPath;
};

class RecentGroup : public QObject {
  Q_OBJECT

public:
  RecentGroup(const QString &path, QObject *parent = nullptr)
      : QObject(parent), mPath(path) {}

  QString path() const { return mPath; }
  QList<RecentRepository *> children;

private:
  QString mPath;
};

class ProgressDelegate : public QStyledItemDelegate {
public:
  ProgressDelegate(QObject *parent = nullptr) : QStyledItemDelegate(parent) {}

  void paint(QPainter *painter, const QStyleOptionViewItem &option,
             const QModelIndex &index) const override {
    QStyleOptionViewItem opt = option;
    initStyleOption(&opt, index);

    // Draw background.
    QStyledItemDelegate::paint(painter, opt, index);

    // Draw busy indicator.
    QVariant progress = index.data(Qt::DecorationRole);
    if (!progress.canConvert<int>())
      return;

    QStyle *style = opt.widget ? opt.widget->style() : QApplication::style();
    QStyle::SubElement se = QStyle::SE_ItemViewItemDecoration;
    QRect rect = style->subElementRect(se, &opt, opt.widget);
    ProgressIndicator::paint(painter, rect, "#808080", progress.toInt());
  }

protected:
  void initStyleOption(QStyleOptionViewItem *option,
                       const QModelIndex &index) const override {
    QStyledItemDelegate::initStyleOption(option, index);
    if (index.data(Qt::DecorationRole).canConvert<int>()) {
      option->decorationSize = ProgressIndicator::size();
    } else if (index.data(RepositoryRole).isValid()) {
      option->features |= QStyleOptionViewItem::HasDecoration;
      option->decorationSize = QSize(20, 20);
    }
  }
};

class RepoModel : public QAbstractItemModel {
  Q_OBJECT

public:
  enum RootRow { Repo, Recent, Remote };

  RepoModel(TabWidget *tabs, QObject *parent = nullptr)
      : QAbstractItemModel(parent), mTabs(tabs), mCloudIcon(":/cloud.png"),
        mErrorIcon(tabs->style()->standardIcon(QStyle::SP_MessageBoxCritical)) {
    connect(tabs, &TabWidget::tabAboutToBeInserted, this,
            &RepoModel::beginResetModel);
    connect(tabs, &TabWidget::tabAboutToBeRemoved, this,
            &RepoModel::beginResetModel);
    connect(tabs, QOverload<>::of(&TabWidget::tabInserted), this,
            [this] {
              rebuildOpenTree();
              endResetModel();
            });
    connect(tabs, QOverload<>::of(&TabWidget::tabRemoved), this,
            [this] {
              rebuildOpenTree();
              endResetModel();
            });
    connect(tabs->tabBar(), &QTabBar::tabMoved, [this] {
      beginResetModel();
      rebuildOpenTree();
      endResetModel();
    });

    RecentRepositories *repos = RecentRepositories::instance();
    connect(repos, &RecentRepositories::repositoryAboutToBeAdded, this,
            &RepoModel::beginResetModel);
    connect(repos, &RecentRepositories::repositoryAboutToBeRemoved, this,
            &RepoModel::beginResetModel);
    connect(repos, &RecentRepositories::repositoryAdded, this, [this] {
      rebuildRecentTree();
      endResetModel();
    });
    connect(repos, &RecentRepositories::repositoryRemoved, this, [this] {
      rebuildRecentTree();
      endResetModel();
    });

    Accounts *accounts = Accounts::instance();
    connect(accounts, &Accounts::accountAboutToBeAdded, this,
            &RepoModel::beginResetModel);
    connect(accounts, &Accounts::accountAdded, this, &RepoModel::endResetModel);
    connect(accounts, &Accounts::accountAboutToBeRemoved, this,
            &RepoModel::beginResetModel);
    connect(accounts, &Accounts::accountRemoved, this,
            &RepoModel::endResetModel);

    connect(accounts, &Accounts::progress, this, [this](int accountIndex) {
      QModelIndex idx = index(0, 0, index(accountIndex, 0, index(Remote, 0)));
      emit dataChanged(idx, idx, {Qt::DisplayRole});
    });
    connect(accounts, &Accounts::started, this, [this](int accountIndex) {
      beginResetModel();
      endResetModel();
    });
    connect(accounts, &Accounts::finished, this, [this](int accountIndex) {
      beginResetModel();
      endResetModel();
    });

    connect(accounts, &Accounts::repositoryAboutToBeAdded, this,
            &RepoModel::beginResetModel);
    connect(accounts, &Accounts::repositoryAdded, this,
            &RepoModel::endResetModel);
    connect(accounts, &Accounts::repositoryPathChanged, this,
            [this](int accountIndex, int repoIndex) {
              QModelIndex idx =
                  index(repoIndex, 0, index(accountIndex, 0, index(Remote, 0)));
              emit dataChanged(idx, idx, {Qt::DisplayRole});
            });

    rebuildOpenTree();
    rebuildRecentTree();
  }

  QModelIndex index(int row, int column,
                    const QModelIndex &parent = QModelIndex()) const override {
    if (!parent.isValid())
      return createIndex(row, column);

    QObject *ptr = static_cast<QObject *>(parent.internalPointer());
    if (OpenGroup *group = qobject_cast<OpenGroup *>(ptr)) {
      if (row >= 0 && row < group->children.size())
        return createIndex(row, column, group->children.at(row));
      return QModelIndex();
    }

    if (RecentGroup *group = qobject_cast<RecentGroup *>(ptr)) {
      if (row >= 0 && row < group->children.size())
        return createIndex(row, column, group->children.at(row));
      return QModelIndex();
    }

    if (RecentRepository *repo = qobject_cast<RecentRepository *>(ptr)) {
      const QList<RecentRepository *> children = mRecentChildren.value(repo);
      if (row >= 0 && row < children.size())
        return createIndex(row, column, children.at(row));
      return QModelIndex();
    }

    if (Account *account = qobject_cast<Account *>(ptr)) {
      // repository
      if (account->repositoryCount())
        return createIndex(row, column, account->repository(row));

      // error
      AccountError *error = account->error();
      if (error->isValid())
        return createIndex(row, column, error);

      // progress
      return createIndex(row, column, account->progress());
    }

    Accounts *accounts = Accounts::instance();
    RecentRepositories *recent = RecentRepositories::instance();
    switch (parent.row()) {
      case Repo:
        if (!mTabs->count())
          return createIndex(row, column, mTabs);
        if (mRecentTree)
          return createIndex(row, column, mOpenRoots.at(row));
        return createIndex(row, column, mTabs->widget(row));

      case Recent:
        if (!recent->count())
          return createIndex(row, column, recent);
        if (mRecentTree)
          return createIndex(row, column, mRecentRoots.at(row));
        return createIndex(row, column, recent->repository(row));

      case Remote:
        if (!accounts->count())
          return createIndex(row, column, accounts);
        return createIndex(row, column, accounts->account(row));
    }

    return QModelIndex();
  }

  QModelIndex parent(const QModelIndex &index) const override {
    QObject *ptr = static_cast<QObject *>(index.internalPointer());
    if (!ptr)
      return QModelIndex();

    if (qobject_cast<OpenGroup *>(ptr))
      return createIndex(Repo, 0);

    if (RepoView *view = qobject_cast<RepoView *>(ptr)) {
      if (mOpenGroups.contains(view)) {
        OpenGroup *group = mOpenGroups.value(view);
        return createIndex(mOpenRoots.indexOf(group), 0, group);
      }
      return createIndex(Repo, 0);
    }

    if (qobject_cast<RecentGroup *>(ptr))
      return createIndex(Recent, 0);

    if (RecentRepository *repo = qobject_cast<RecentRepository *>(ptr)) {
      if (mRecentParents.contains(repo))
        return recentIndex(mRecentParents.value(repo));
      if (mRecentGroups.contains(repo)) {
        RecentGroup *group = mRecentGroups.value(repo);
        return createIndex(mRecentRoots.indexOf(group), 0, group);
      }
      return createIndex(Recent, 0);
    }

    if (qobject_cast<TabWidget *>(ptr))
      return createIndex(Repo, 0);

    if (qobject_cast<RecentRepositories *>(ptr))
      return createIndex(Recent, 0);

    if (qobject_cast<Account *>(ptr) || qobject_cast<Accounts *>(ptr))
      return createIndex(Remote, 0);

    Account *account = nullptr;
    if (Repository *repo = qobject_cast<Repository *>(ptr)) {
      account = repo->account();
    } else if (AccountError *error = qobject_cast<AccountError *>(ptr)) {
      account = error->account();
    } else if (AccountProgress *ap = qobject_cast<AccountProgress *>(ptr)) {
      account = ap->account();
    }

    if (account)
      return createIndex(Accounts::instance()->indexOf(account), 0, account);

    return QModelIndex();
  }

  int rowCount(const QModelIndex &parent = QModelIndex()) const override {
    if (!parent.isValid())
      return 3;

    QObject *ptr = static_cast<QObject *>(parent.internalPointer());
    if (OpenGroup *group = qobject_cast<OpenGroup *>(ptr))
      return group->children.size();

    if (RecentGroup *group = qobject_cast<RecentGroup *>(ptr))
      return group->children.size();

    if (RecentRepository *repo = qobject_cast<RecentRepository *>(ptr))
      return mRecentChildren.value(repo).size();

    if (Account *account = qobject_cast<Account *>(ptr)) {
      int count = account->repositoryCount();
      if (count > 0)
        return count;

      AccountError *error = account->error();
      AccountProgress *progress = account->progress();
      return (error->isValid() || progress->isValid()) ? 1 : 0;
    }

    if (parent.parent().isValid())
      return 0;

    switch (parent.row()) {
      case Repo:
        if (!mTabs->count())
          return 1;
        return mRecentTree ? mOpenRoots.size() : mTabs->count();

      case Recent: {
        RecentRepositories *recent = RecentRepositories::instance();
        if (!recent->count())
          return 1;
        return mRecentTree ? mRecentRoots.size() : recent->count();
      }

      case Remote: {
        Accounts *accounts = Accounts::instance();
        return accounts->count() ? accounts->count() : Account::NUM_KINDS;
      }

      default:
        return 0;
    }
  }

  int columnCount(const QModelIndex &parent = QModelIndex()) const override {
    return 1;
  }

  QVariant data(const QModelIndex &index,
                int role = Qt::DisplayRole) const override {
    QObject *ptr = static_cast<QObject *>(index.internalPointer());
    if (OpenGroup *group = qobject_cast<OpenGroup *>(ptr)) {
      switch (role) {
        case Qt::DisplayRole:
          return mShowFullPath ? group->path()
                               : QFileInfo(group->path()).fileName();
        case Qt::ToolTipRole:
          return mShowFullPath ? QString() : group->path();
        case OpenGroupPathRole:
          return group->path();
        default:
          return QVariant();
      }
    }

    if (RepoView *view = qobject_cast<RepoView *>(ptr)) {
      const int tabIndex = mTabs->indexOf(view);
      const QString path = view->repo().workdir().path();
      switch (role) {
        case Qt::DisplayRole:
          return mShowFullPath ? path : mTabs->tabText(tabIndex);
        case PathRole:
          return path;
        case TabRole:
          return QVariant::fromValue(view);
        case Qt::ToolTipRole:
          return mShowFullPath ? QString() : path;
        default:
          return QVariant();
      }
    }

    if (RecentGroup *group = qobject_cast<RecentGroup *>(ptr)) {
      switch (role) {
        case Qt::DisplayRole:
          return mShowFullPath ? group->path()
                               : QFileInfo(group->path()).fileName();
        case Qt::ToolTipRole:
          return mShowFullPath ? QString() : group->path();
        case GroupPathRole:
          return group->path();
        default:
          return QVariant();
      }
    }

    if (RecentRepository *recentRepo = qobject_cast<RecentRepository *>(ptr)) {
      switch (role) {
        case Qt::DisplayRole:
          return mShowFullPath ? recentRepo->gitpath() : recentRepo->name();
        case PathRole:
          return recentRepo->gitpath();
        case Qt::ToolTipRole:
          return mShowFullPath ? QString() : recentRepo->gitpath();
        case RecentRole:
          return QVariant::fromValue(recentRepo);
        default:
          return QVariant();
      }
    }

    if (Repository *repo = qobject_cast<Repository *>(ptr)) {
      switch (role) {
        case Qt::DisplayRole:
          return mShowFullName ? repo->fullName() : repo->name();

        case Qt::DecorationRole: {
          Account *account = repo->account();
          QString path = account->repositoryPath(account->indexOf(repo));
          return path.isEmpty() ? mCloudIcon : QIcon();
        }

        case PathRole: {
          Account *account = repo->account();
          return account->repositoryPath(account->indexOf(repo));
        }

        case RepositoryRole:
          return QVariant::fromValue(repo);

        default:
          return QVariant();
      }
    }

    if (AccountError *error = qobject_cast<AccountError *>(ptr)) {
      switch (role) {
        case Qt::DisplayRole:
          return error->text();
        case Qt::DecorationRole:
          return mErrorIcon;
        case Qt::ToolTipRole:
          return error->detailedText();
        default:
          return QVariant();
      }
    }

    if (AccountProgress *progress = qobject_cast<AccountProgress *>(ptr)) {
      switch (role) {
        case Qt::DisplayRole:
          return tr("Connecting");

        case Qt::DecorationRole:
          return progress->value();

        case Qt::FontRole: {
          QFont font = static_cast<QWidget *>(QObject::parent())->font();
          font.setItalic(true);
          return font;
        }

        default:
          return QVariant();
      }
    }

    int row = index.row();
    QModelIndex parent = index.parent();
    RecentRepositories *repos = RecentRepositories::instance();
    switch (role) {
      case Qt::DisplayRole: {
        if (!parent.isValid()) {
          switch (row) {
            case Repo:
              return tr("open");
            case Recent:
              return tr("recent");
            case Remote:
              return tr("remote");
            default:
              return QVariant();
          }
        }

        switch (parent.row()) {
          case Repo:
            if (mTabs->count()) {
              if (!mShowFullPath)
                return mTabs->tabText(row);

              RepoView *view = static_cast<RepoView *>(mTabs->widget(row));
              return view->repo().dir(false).path();
            }

            return tr("none");

          case Recent: {
            RecentRepositories *recent = RecentRepositories::instance();
            if (recent->count()) {
              RecentRepository *repo = repos->repository(row);
              return mShowFullPath ? repo->gitpath() : repo->name();
            }

            return tr("none");
          }

          case Remote: {
            Accounts *accounts = Accounts::instance();
            if (accounts->count())
              return accounts->account(row)->username();
            return Account::name(static_cast<Account::Kind>(row));
          }

          default:
            return QVariant();
        }
      }

      case Qt::DecorationRole: {
        if (!parent.isValid())
          return QVariant();

        switch (parent.row()) {
          case Remote: {
            Accounts *accounts = Accounts::instance();
            if (accounts->count())
              return Account::icon(accounts->account(row)->kind());
            return Account::icon(static_cast<Account::Kind>(row));
          }

          default:
            return QVariant();
        }
      }

      case Qt::FontRole: {
        if (!parent.isValid()) {
          QFont font;
          font.setCapitalization(QFont::SmallCaps);
          font.setBold(true);
          return font;
        }

        switch (parent.row()) {
          case Repo: {
            if (mTabs->count())
              return QVariant();

            QFont font;
            font.setItalic(true);
            return font;
          }

          case Recent: {
            RecentRepositories *recent = RecentRepositories::instance();
            if (recent->count())
              return QVariant();

            QFont font;
            font.setItalic(true);
            return font;
          }

          default:
            return QVariant();
        }
      }

      case Qt::ForegroundRole:
        return QPalette().brush(!parent.isValid() ? QPalette::BrightText
                                                  : QPalette::Text);

      case Qt::SizeHintRole:
        return QSize(0, QFontMetrics(QFont()).lineSpacing() + 8);

      case PathRole: {
        if (!parent.isValid())
          return QVariant();

        switch (parent.row()) {
          case Repo: {
            if (!mTabs->count())
              return QVariant();
            QWidget *widget = mTabs->widget(row);
            RepoView *view = static_cast<RepoView *>(widget);
            return view->repo().dir(false).path();
          }

          case Recent:
            if (!repos->count())
              return QVariant();
            return repos->repository(row)->gitpath();

          default:
            return QVariant();
        }
      }

      case TabRole:
        return QVariant::fromValue(qobject_cast<RepoView *>(ptr));

      case RecentRole:
        return QVariant::fromValue(qobject_cast<RecentRepository *>(ptr));

      case AccountRole:
        return QVariant::fromValue(qobject_cast<Account *>(ptr));

      case AccountKindRole: {
        if (!parent.isValid())
          return QVariant();

        switch (parent.row()) {
          case Remote:
            if (Account *account = qobject_cast<Account *>(ptr))
              return account->kind();
            return static_cast<Account::Kind>(row);

          default:
            return QVariant();
        }
      }

      case Qt::ToolTipRole: {
        if (!parent.isValid() ||
            mShowFullPath) // makes no sense to show tooltip when path is
                           // already shown completely
          return "";

        switch (parent.row()) {
          case Repo:
            if (mTabs->count()) {
              RepoView *view = static_cast<RepoView *>(mTabs->widget(row));
              return view->repo().dir(false).path();
            }

            return "";

          case Recent: {
            RecentRepositories *recent = RecentRepositories::instance();
            if (recent->count()) {
              RecentRepository *repo = repos->repository(row);
              return repo->gitpath();
            }

            return "";
          }

          default:
            return QVariant();
        }
      }
      default:
        return QVariant();
    }
  }

  Qt::ItemFlags flags(const QModelIndex &index) const override {
    Qt::ItemFlags flags = QAbstractItemModel::flags(index);
    QObject *ptr = static_cast<QObject *>(index.internalPointer());
    if (!index.parent().isValid() || qobject_cast<OpenGroup *>(ptr) ||
        qobject_cast<RecentGroup *>(
        ptr))
      flags &= ~Qt::ItemIsSelectable;
    return flags;
  }

  QModelIndex currentIndex() const {
    if (mRecentTree && mTabs->currentIndex() >= 0)
      return openIndex(qobject_cast<RepoView *>(mTabs->currentWidget()));
    return index(mTabs->currentIndex(), 0, index(Repo, 0));
  }

  void setShowFullPath(bool enabled) {
    beginResetModel();
    mShowFullPath = enabled;
    endResetModel();
  }

  void setShowFullName(bool enabled) {
    beginResetModel();
    mShowFullName = enabled;
    endResetModel();
  }

  void setRecentTree(bool enabled) {
    if (mRecentTree == enabled)
      return;

    beginResetModel();
    mRecentTree = enabled;
    rebuildOpenTree();
    rebuildRecentTree();
    endResetModel();
  }

  bool isOpenExpandable(const QModelIndex &index) const {
    QObject *ptr = static_cast<QObject *>(index.internalPointer());
    if (OpenGroup *group = qobject_cast<OpenGroup *>(ptr))
      return !group->children.isEmpty();
    return false;
  }

  bool isRecentExpandable(const QModelIndex &index) const {
    QObject *ptr = static_cast<QObject *>(index.internalPointer());
    if (RecentGroup *group = qobject_cast<RecentGroup *>(ptr))
      return !group->children.isEmpty();
    if (RecentRepository *repo = qobject_cast<RecentRepository *>(ptr))
      return !mRecentChildren.value(repo).isEmpty();
    return false;
  }

  void removeRecent(const QString &path) {
    RecentRepositories *repos = RecentRepositories::instance();
    for (int i = 0; i < repos->count(); ++i) {
      if (repos->repository(i)->gitpath() == path) {
        repos->remove(i);
        return;
      }
    }
  }

private:
  static QString canonicalPath(const QString &path) {
    QFileInfo info(path);
    QString canonical = info.canonicalFilePath();
    if (canonical.isEmpty())
      canonical = info.absoluteFilePath();
    canonical = QDir::cleanPath(canonical);
#ifdef Q_OS_WIN
    canonical = canonical.toCaseFolded();
#endif
    return canonical;
  }

  void rebuildOpenTree() {
    qDeleteAll(mOpenGroupObjects);
    mOpenGroupObjects.clear();
    mOpenRoots.clear();
    mOpenGroups.clear();

    if (!mRecentTree) {
      for (int i = 0; i < mTabs->count(); ++i)
        mOpenRoots.append(mTabs->widget(i));
      return;
    }

    QHash<QString, QList<RepoView *>> directoryGroups;
    QHash<RepoView *, QString> repositoryDirectories;
    QHash<QString, QString> directoryPaths;
    for (int i = 0; i < mTabs->count(); ++i) {
      RepoView *view = qobject_cast<RepoView *>(mTabs->widget(i));
      if (!view)
        continue;

      QString workdir = view->repo().workdir().path();
      if (workdir.isEmpty())
        workdir = view->repo().dir(false).path();
      const QString originalDirectory = QFileInfo(workdir).absolutePath();
      const QString directory = canonicalPath(originalDirectory);
      repositoryDirectories.insert(view, directory);
      directoryGroups[directory].append(view);
      if (!directoryPaths.contains(directory))
        directoryPaths.insert(directory, originalDirectory);
    }

    QSet<QString> addedGroups;
    for (int i = 0; i < mTabs->count(); ++i) {
      RepoView *view = qobject_cast<RepoView *>(mTabs->widget(i));
      if (!view)
        continue;

      const QString directory = repositoryDirectories.value(view);
      const QList<RepoView *> groupViews = directoryGroups.value(directory);
      if (groupViews.size() < 2) {
        mOpenRoots.append(view);
        continue;
      }

      if (addedGroups.contains(directory))
        continue;

      OpenGroup *group = new OpenGroup(directoryPaths.value(directory), this);
      group->children = groupViews;
      mOpenGroupObjects.append(group);
      mOpenRoots.append(group);
      addedGroups.insert(directory);
      for (RepoView *groupView : groupViews)
        mOpenGroups.insert(groupView, group);
    }
  }

  QModelIndex openIndex(RepoView *view) const {
    if (!view)
      return QModelIndex();
    if (mOpenGroups.contains(view)) {
      OpenGroup *group = mOpenGroups.value(view);
      return createIndex(group->children.indexOf(view), 0, view);
    }
    return createIndex(mOpenRoots.indexOf(view), 0, view);
  }

  bool createsRecentCycle(RecentRepository *parent,
                          RecentRepository *child) const {
    RecentRepository *ancestor = parent;
    while (ancestor) {
      if (ancestor == child)
        return true;
      ancestor = mRecentParents.value(ancestor, nullptr);
    }
    return false;
  }

  void rebuildRecentTree() {
    qDeleteAll(mRecentGroupObjects);
    mRecentGroupObjects.clear();
    mRecentRoots.clear();
    mRecentGroups.clear();
    mRecentParents.clear();
    mRecentChildren.clear();

    RecentRepositories *repos = RecentRepositories::instance();
    if (!mRecentTree) {
      for (int i = 0; i < repos->count(); ++i)
        mRecentRoots.append(repos->repository(i));
      return;
    }

    QHash<QString, RecentRepository *> repositoriesByPath;
    QHash<RecentRepository *, QString> repositoryWorkdirs;
    for (int i = 0; i < repos->count(); ++i) {
      RecentRepository *recentRepo = repos->repository(i);
      repositoriesByPath.insert(canonicalPath(recentRepo->gitpath()),
                                recentRepo);

      git::Repository repository = git::Repository::open(recentRepo->gitpath());
      QString workdir = repository.isValid() ? repository.workdir().path()
                                             : QString();
      if (workdir.isEmpty())
        workdir = recentRepo->gitpath();
      repositoryWorkdirs.insert(recentRepo, workdir);
      if (repository.isValid())
        repositoriesByPath.insert(canonicalPath(workdir), recentRepo);
    }

    for (int i = 0; i < repos->count(); ++i) {
      RecentRepository *recentParent = repos->repository(i);
      git::Repository repository =
          git::Repository::open(recentParent->gitpath());
      if (!repository.isValid())
        continue;

      for (const git::Submodule &submodule : repository.submodules()) {
        if (!submodule.isInitialized())
          continue;

        git::Repository childRepository = submodule.open();
        if (!childRepository.isValid())
          continue;

        const QString childPath = canonicalPath(childRepository.workdir().path());
        RecentRepository *recentChild = repositoriesByPath.value(childPath);
        if (!recentChild || mRecentParents.contains(recentChild) ||
            createsRecentCycle(recentParent, recentChild))
          continue;

        mRecentParents.insert(recentChild, recentParent);
        mRecentChildren[recentParent].append(recentChild);
      }
    }

    QHash<QString, QList<RecentRepository *>> directoryGroups;
    QHash<QString, QString> directoryPaths;
    for (int i = 0; i < repos->count(); ++i) {
      RecentRepository *recentRepo = repos->repository(i);
      if (mRecentParents.contains(recentRepo)) {
        continue;
      }

      const QString originalDirectory =
          QFileInfo(repositoryWorkdirs.value(recentRepo)).absolutePath();
      const QString directory = canonicalPath(originalDirectory);
      directoryGroups[directory].append(recentRepo);
      if (!directoryPaths.contains(directory))
        directoryPaths.insert(directory, originalDirectory);
    }

    QSet<QString> addedGroups;
    for (int i = 0; i < repos->count(); ++i) {
      RecentRepository *recentRepo = repos->repository(i);
      if (mRecentParents.contains(recentRepo)) {
        continue;
      }

      const QString directory = canonicalPath(
          QFileInfo(repositoryWorkdirs.value(recentRepo)).absolutePath());
      const QList<RecentRepository *> groupRepos = directoryGroups.value(directory);
      if (groupRepos.size() < 2) {
        mRecentRoots.append(recentRepo);
        continue;
      }

      if (addedGroups.contains(directory))
        continue;

      RecentGroup *group = new RecentGroup(directoryPaths.value(directory), this);
      group->children = groupRepos;
      mRecentGroupObjects.append(group);
      mRecentRoots.append(group);
      addedGroups.insert(directory);
      for (RecentRepository *groupRepo : groupRepos)
        mRecentGroups.insert(groupRepo, group);
    }
  }

  QModelIndex recentIndex(RecentRepository *repo) const {
    if (mRecentParents.contains(repo)) {
      RecentRepository *parent = mRecentParents.value(repo);
      return createIndex(mRecentChildren.value(parent).indexOf(repo), 0, repo);
    }
    if (mRecentGroups.contains(repo)) {
      RecentGroup *group = mRecentGroups.value(repo);
      return createIndex(group->children.indexOf(repo), 0, repo);
    }
    return createIndex(mRecentRoots.indexOf(repo), 0, repo);
  }

  TabWidget *mTabs;
  bool mShowFullPath = false;
  bool mShowFullName = false;
  bool mRecentTree = false;
  QList<QObject *> mOpenRoots;
  QList<OpenGroup *> mOpenGroupObjects;
  QHash<RepoView *, OpenGroup *> mOpenGroups;
  QList<QObject *> mRecentRoots;
  QList<RecentGroup *> mRecentGroupObjects;
  QHash<RecentRepository *, RecentGroup *> mRecentGroups;
  QHash<RecentRepository *, RecentRepository *> mRecentParents;
  QHash<RecentRepository *, QList<RecentRepository *>> mRecentChildren;

  QIcon mCloudIcon;
  QIcon mErrorIcon;
};

// Does this index correspond to one of the open repositories?
bool isRepoIndex(const QModelIndex &index) {
  return index.data(TabRole).value<RepoView *>() != nullptr;
}

bool isRemoteIndex(const QModelIndex &index) {
  QModelIndex parent = index.parent();
  return (parent.isValid() && !parent.parent().isValid() &&
          parent.row() == RepoModel::Remote);
}

void storeRecentExpansion(QTreeView *view, RepoModel *model,
                          const QModelIndex &index, QSettings &settings);
void restoreRecentExpansion(QTreeView *view, RepoModel *model,
                            const QModelIndex &index, QSettings &settings);
void storeOpenExpansion(QTreeView *view, RepoModel *model,
                        const QModelIndex &index, QSettings &settings);
void restoreOpenExpansion(QTreeView *view, RepoModel *model,
                          const QModelIndex &index, QSettings &settings);

QString treeExpansionKey(const QModelIndex &index) {
  const QString openGroupPath = index.data(OpenGroupPathRole).toString();
  if (!openGroupPath.isEmpty()) {
    const QByteArray encoded = openGroupPath.toUtf8().toBase64(
        QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
    return "open-group-" + QString::fromLatin1(encoded);
  }

  const bool isGroup = index.data(GroupPathRole).isValid();
  const QString path = isGroup ? index.data(GroupPathRole).toString()
                               : index.data(PathRole).toString();
  if (path.isEmpty())
    return QString();

  const QByteArray encoded = path.toUtf8().toBase64(
      QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
  return (isGroup ? "group-" : "repo-") + QString::fromLatin1(encoded);
}

void storeExpansionState(QTreeView *view) {
  QSettings settings;
  settings.beginGroup(kRemoteExpandedGroup);

  QAbstractItemModel *model = view->model();
  QModelIndex remote = model->index(RepoModel::Remote, 0);
  for (int i = 0; i < model->rowCount(remote); ++i) {
    QModelIndex index = model->index(i, 0, remote);
    QString key = index.data(Qt::DisplayRole).toString();
    settings.setValue(key, view->isExpanded(index));
  }

  settings.endGroup();

  RepoModel *repoModel = qobject_cast<RepoModel *>(view->model());
  if (!repoModel)
    return;

  settings.beginGroup(kOpenExpandedGroup);
  QModelIndex open = repoModel->index(RepoModel::Repo, 0);
  for (int i = 0; i < repoModel->rowCount(open); ++i) {
    QModelIndex index = repoModel->index(i, 0, open);
    storeOpenExpansion(view, repoModel, index, settings);
  }
  settings.endGroup();

  settings.beginGroup(kRecentExpandedGroup);
  QModelIndex recent = repoModel->index(RepoModel::Recent, 0);
  for (int i = 0; i < repoModel->rowCount(recent); ++i) {
    QModelIndex index = repoModel->index(i, 0, recent);
    storeRecentExpansion(view, repoModel, index, settings);
  }
  settings.endGroup();
}

void storeOpenExpansion(QTreeView *view, RepoModel *model,
                        const QModelIndex &index, QSettings &settings) {
  if (!model->isOpenExpandable(index))
    return;

  const QString key = treeExpansionKey(index);
  if (!key.isEmpty())
    settings.setValue(key, view->isExpanded(index));
}

void storeRecentExpansion(QTreeView *view, RepoModel *model,
                          const QModelIndex &index, QSettings &settings) {
  if (!model->isRecentExpandable(index))
    return;

  const QString key = treeExpansionKey(index);
  if (key.isEmpty())
    return;

  settings.setValue(key, view->isExpanded(index));
  for (int i = 0; i < model->rowCount(index); ++i)
    storeRecentExpansion(view, model, model->index(i, 0, index), settings);
}

void restoreExpansionState(QTreeView *view) {
  QAbstractItemModel *model = view->model();
  for (int i = 0; i < model->rowCount(); ++i)
    view->expand(model->index(i, 0));

  QSettings settings;
  settings.beginGroup(kRemoteExpandedGroup);

  QModelIndex remote = model->index(RepoModel::Remote, 0);
  for (int i = 0; i < model->rowCount(remote); ++i) {
    QModelIndex index = model->index(i, 0, remote);
    QString key = index.data(Qt::DisplayRole).toString();
    view->setExpanded(index, settings.value(key, true).toBool());
  }

  settings.endGroup();

  RepoModel *repoModel = qobject_cast<RepoModel *>(view->model());
  if (!repoModel)
    return;

  settings.beginGroup(kOpenExpandedGroup);
  QModelIndex open = repoModel->index(RepoModel::Repo, 0);
  for (int i = 0; i < repoModel->rowCount(open); ++i) {
    QModelIndex index = repoModel->index(i, 0, open);
    restoreOpenExpansion(view, repoModel, index, settings);
  }
  settings.endGroup();

  settings.beginGroup(kRecentExpandedGroup);
  QModelIndex recent = repoModel->index(RepoModel::Recent, 0);
  for (int i = 0; i < repoModel->rowCount(recent); ++i) {
    QModelIndex index = repoModel->index(i, 0, recent);
    restoreRecentExpansion(view, repoModel, index, settings);
  }
  settings.endGroup();
}

void restoreOpenExpansion(QTreeView *view, RepoModel *model,
                          const QModelIndex &index, QSettings &settings) {
  if (!model->isOpenExpandable(index))
    return;

  const QString key = treeExpansionKey(index);
  if (!key.isEmpty())
    view->setExpanded(index, settings.value(key, false).toBool());
}

void restoreRecentExpansion(QTreeView *view, RepoModel *model,
                            const QModelIndex &index, QSettings &settings) {
  if (!model->isRecentExpandable(index))
    return;

  const QString key = treeExpansionKey(index);
  if (key.isEmpty())
    return;

  view->setExpanded(index, settings.value(key, false).toBool());
  if (view->isExpanded(index)) {
    for (int i = 0; i < model->rowCount(index); ++i)
      restoreRecentExpansion(view, model, model->index(i, 0, index), settings);
  }
}

} // namespace

SideBar::SideBar(TabWidget *tabs, MainWindow *mainWindow, QWidget *parent)
    : QWidget(parent) {
  setStyleSheet(kStyleSheet);

  QTreeView *view = new QTreeView(this);
  view->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  view->setContextMenuPolicy(Qt::CustomContextMenu);
  view->setFocusPolicy(Qt::NoFocus);
  view->setIconSize(QSize(20, 20));
  view->setRootIsDecorated(false);
  view->setHeaderHidden(true);

  view->setItemDelegate(new ProgressDelegate(view));

  RepoModel *model = new RepoModel(tabs, view);
  view->setModel(model);

  // Restore selection and expansion state after model reset.
  connect(model, &RepoModel::modelReset, view, [view, model] {
    view->setCurrentIndex(model->currentIndex());
    restoreExpansionState(view);
  });

  connect(tabs, &TabWidget::currentChanged, view,
          [view, model] { view->setCurrentIndex(model->currentIndex()); });

  // Store expansion state when it changes.
  connect(view, &QTreeView::collapsed, [view](const QModelIndex &index) {
    if (isRemoteIndex(index) || index.data(PathRole).isValid() ||
        index.data(GroupPathRole).isValid() ||
        index.data(OpenGroupPathRole).isValid())
      storeExpansionState(view);
  });
  connect(view, &QTreeView::expanded, [view](const QModelIndex &index) {
    if (isRemoteIndex(index) || index.data(PathRole).isValid() ||
        index.data(GroupPathRole).isValid() ||
        index.data(OpenGroupPathRole).isValid())
      storeExpansionState(view);
  });

  connect(view, &QTreeView::clicked, [tabs](const QModelIndex &index) {
    if (isRepoIndex(index))
      tabs->setCurrentIndex(
          tabs->indexOf(index.data(TabRole).value<RepoView *>()));
  });

  connect(
      view, &QTreeView::doubleClicked,
      [tabs, this, mainWindow](const QModelIndex &index) {
        if (isRepoIndex(index)) {
          tabs->setCurrentIndex(
              tabs->indexOf(index.data(TabRole).value<RepoView *>()));
          if (Settings::instance()
                  ->value(Setting::Id::AutoHideRepoSiderbar, true)
                  .toBool()) {
            mainWindow->setSideBarVisible(false);
          }
          return;
        }

        // Open existing path.
        QString path = index.data(PathRole).toString();
        if (!path.isEmpty()) {
          MainWindow::open(path);
          if (Settings::instance()
                  ->value(Setting::Id::AutoHideRepoSiderbar, true)
                  .toBool()) {
            mainWindow->setSideBarVisible(false);
          }
          return;
        }

        // Add remote account.
        QVariant accountKindVariant = index.data(AccountKindRole);
        if (accountKindVariant.isValid()) {
          Account *account = index.data(AccountRole).value<Account *>();
          AccountDialog *dialog = new AccountDialog(account, this);
          dialog->setKind(accountKindVariant.value<Account::Kind>());
          dialog->open();
          return;
        }

        // Clone remote repository.
        QVariant repoVariant = index.data(RepositoryRole);
        if (repoVariant.isValid()) {
          Repository *repo = repoVariant.value<Repository *>();
          CloneDialog *dialog = new CloneDialog(CloneDialog::Clone, this, repo);
          connect(dialog, &CloneDialog::accepted, [repo, dialog, mainWindow] {
            // Set local path.
            Account *account = repo->account();
            account->setRepositoryPath(account->indexOf(repo), dialog->path());

            // Open the repo.
            if (Settings::instance()
                    ->value(Setting::Id::AutoHideRepoSiderbar, true)
                    .toBool()) {
              mainWindow->setSideBarVisible(false);
            }
            MainWindow::open(dialog->path());
          });

          dialog->open();
        }
      });

  connect(view, &QTreeView::customContextMenuRequested,
          [this, view](const QPoint &point) {
            QMenu menu;
            QModelIndex index = view->indexAt(point);
            if (RepoView *view = index.data(TabRole).value<RepoView *>()) {
              menu.addAction(tr("Close"), view, &RepoView::close);
            } else if (Account *account =
                           index.data(AccountRole).value<Account *>()) {
              menu.addAction(tr("Remove"), [this, account] {
                promptToRemoveAccount(account);
              });

              if (account->isAuthorizeSupported())
                menu.addAction(tr("Authorize"), account, &Account::authorize);
            }

            if (!menu.isEmpty())
              menu.exec(view->mapToGlobal(point));
          });

  // footer
  Footer *footer = new Footer(this);

  // plus button
  QMenu *plusMenu = new QMenu(this);
  footer->setPlusMenu(plusMenu);

  QAction *clone = plusMenu->addAction(tr("Clone Repository"));
  connect(clone, &QAction::triggered, [this] {
    CloneDialog *dialog = new CloneDialog(CloneDialog::Clone, this);
    connect(dialog, &CloneDialog::accepted, [dialog] {
      if (MainWindow *window = MainWindow::open(dialog->path()))
        window->currentView()->addLogEntry(dialog->message(),
                                           dialog->messageTitle());
    });
    dialog->open();
  });

  QAction *open = plusMenu->addAction(tr("Open Existing Repository"));
  connect(open, &QAction::triggered, [this] {
    // FIXME: Filter out non-git dirs.
    QFileDialog *dialog =
        new QFileDialog(this, tr("Open Repository"), QDir::homePath());
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setFileMode(QFileDialog::Directory);
    dialog->setOption(QFileDialog::ShowDirsOnly);
    connect(dialog, &QFileDialog::fileSelected,
            [](const QString &path) { MainWindow::open(path); });
    dialog->open();
  });

  QAction *init = plusMenu->addAction(tr("Initialize New Repository"));
  connect(init, &QAction::triggered, [this] {
    CloneDialog *dialog = new CloneDialog(CloneDialog::Init, this);
    connect(dialog, &CloneDialog::accepted, [dialog] {
      if (MainWindow *window = MainWindow::open(dialog->path()))
        window->currentView()->addLogEntry(dialog->message(),
                                           dialog->messageTitle());
    });
    dialog->open();
  });

  plusMenu->addSeparator();

  for (int i = 0; i < Account::NUM_KINDS; ++i) {
    Account::Kind kind = static_cast<Account::Kind>(i);
    QString text = tr("Add %1 Account").arg(Account::name(kind));
    QAction *add = plusMenu->addAction(text);
    connect(add, &QAction::triggered, [this, kind] {
      AccountDialog *dialog = new AccountDialog(nullptr, this);
      dialog->setKind(kind);
      dialog->open();
    });
  }

  // minus button
  QItemSelectionModel *sel = view->selectionModel();
  connect(sel, &QItemSelectionModel::selectionChanged, [footer, sel] {
    QModelIndexList indexes = sel->selectedIndexes();
    if (indexes.isEmpty())
      return;

    QModelIndex index = indexes.first();
    QString path = index.data(PathRole).toString();
    Account *account = index.data(AccountRole).value<Account *>();
    footer->setMinusEnabled(!path.isEmpty() || account);
  });

  connect(footer, &Footer::minusClicked, [this, view, model, sel] {
    for (const QModelIndex &index : sel->selectedIndexes()) {
      if (RepoView *tab = index.data(TabRole).value<RepoView *>()) {
        tab->close();

      } else if (index.data(RecentRole).value<RecentRepository *>()) {
        model->removeRecent(index.data(PathRole).toString());

      } else if (auto account = index.data(AccountRole).value<Account *>()) {
        promptToRemoveAccount(account);

      } else if (auto repo = index.data(RepositoryRole).value<Repository *>()) {
        QString fmt =
            tr("<p>Are you sure you want to remove the remote repository "
               "association for %1?</p><p>The local clone itself will not "
               "be affected.</p>");

        QMessageBox *dialog = new QMessageBox(
            QMessageBox::Warning, tr("Remove Repository Association?"),
            fmt.arg(repo->fullName()), QMessageBox::Cancel, this);
        dialog->setAttribute(Qt::WA_DeleteOnClose);

        QPushButton *remove =
            dialog->addButton(tr("Remove"), QMessageBox::AcceptRole);
        remove->setFocus();
        connect(remove, &QPushButton::clicked, [index, repo] {
          repo->account()->setRepositoryPath(index.row(), QString());
        });

        dialog->open();
      }
    }

    // Reset selection to current tab.
    view->setCurrentIndex(model->currentIndex());
  });

  // context menu
  QSettings settings;
  QMenu *contextMenu = new QMenu(this);
  footer->setContextMenu(contextMenu);

  QAction *clear = contextMenu->addAction(tr("Clear All Recent"));
  connect(clear, &QAction::triggered,
          [] { RecentRepositories::instance()->clear(); });

    QAction *treeView =
      contextMenu->addAction(tr("Group Repositories by Directory"));
    treeView->setObjectName("repositoryTreeMode");
  treeView->setCheckable(true);
    treeView->setChecked(settings.value(kGroupedViewKey, false).toBool());
  model->setRecentTree(treeView->isChecked());
  connect(treeView, &QAction::toggled, [model](bool checked) {
    QSettings().setValue(kGroupedViewKey, checked);
    model->setRecentTree(checked);
  });

  QAction *showFullPath = contextMenu->addAction(tr("Show Full Path"));
  bool recentChecked = settings.value("start/recent/fullpath").toBool();
  showFullPath->setCheckable(true);
  showFullPath->setChecked(recentChecked);
  model->setShowFullPath(recentChecked);
  connect(showFullPath, &QAction::triggered, [model](bool checked) {
    QSettings().setValue("start/recent/fullpath", checked);
    model->setShowFullPath(checked);
  });

  QAction *filter = contextMenu->addAction(tr("Filter Non-existent Paths"));
  filter->setCheckable(true);
  filter->setChecked(settings.value("recent/filter", true).toBool());
  connect(filter, &QAction::triggered,
          [](bool checked) { QSettings().setValue("recent/filter", checked); });

  contextMenu->addSeparator();

  QAction *refresh = contextMenu->addAction(tr("Refresh Remote Accounts"));
  connect(refresh, &QAction::triggered, [] {
    Accounts *accounts = Accounts::instance();
    for (int i = 0; i < accounts->count(); ++i)
      accounts->account(i)->connect();
  });

  QAction *showFullName = contextMenu->addAction(tr("Show Full Name"));
  bool remoteChecked = settings.value("start/remote/fullname").toBool();
  showFullName->setCheckable(true);
  showFullName->setChecked(remoteChecked);
  model->setShowFullName(remoteChecked);
  connect(showFullName, &QAction::triggered, [model](bool checked) {
    QSettings().setValue("start/remote/fullname", checked);
    model->setShowFullName(checked);
  });

  // Create layout.
  QVBoxLayout *layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);
  layout->addWidget(view);
  layout->addWidget(footer);

  // Disable footer resize.
  footer->setMinimumWidth(footer->sizeHint().width());
}

QSize SideBar::sizeHint() const { return QSize(192, 0); }

QSize SideBar::minimumSizeHint() const { return QSize(0, 0); }

void SideBar::promptToRemoveAccount(Account *account) {
  QString fmt =
      tr("<p>Are you sure you want to remove the %1 account for '%2'?</p>"
         "<p>Only the account association will be removed. Remote "
         "configurations and local clones will not be affected.</p>");

  QMessageBox *dialog = new QMessageBox(
      QMessageBox::Warning, tr("Remove Account?"),
      fmt.arg(Account::name(account->kind()), account->username()),
      QMessageBox::Cancel, this);
  dialog->setAttribute(Qt::WA_DeleteOnClose);

  QPushButton *remove =
      dialog->addButton(tr("Remove"), QMessageBox::AcceptRole);
  remove->setFocus();
  connect(remove, &QPushButton::clicked,
          [account] { Accounts::instance()->removeAccount(account); });

  dialog->open();
}

#include "SideBar.moc"
