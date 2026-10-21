//
//          Copyright (c) 2016, Scientific Toolworks, Inc.
//
// This software is licensed under the MIT License. The LICENSE.md file
// describes the conditions under which this software may be distributed.
//
// Author: Jason Haslam
//

#include "Test.h"
#include "conf/RecentRepository.h"
#include "conf/RecentRepositories.h"
#include "git/Submodule.h"
#include "ui/MainWindow.h"
#include "ui/RepoView.h"
#include "ui/SideBar.h"
#include <QAction>
#include <QDir>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTreeView>

using namespace Test;
using namespace QTest;

class TestMainWindow : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();
  void recentTree();
  void show();
  void cleanupTestCase();

private:
  ScratchRepository mRepo;
  QTemporaryDir mRecentRepoDirectory;
  QStringList mTestRecentPaths;
  QAction *mTreeModeAction = nullptr;
  bool mOriginalTreeMode = false;
  MainWindow *mWindow = nullptr;
};

void TestMainWindow::initTestCase() { mWindow = new MainWindow(mRepo); }

void TestMainWindow::recentTree() {
  QVERIFY(mRecentRepoDirectory.isValid());

  QVERIFY(QDir().mkpath(mRecentRepoDirectory.filePath("d1")));
  QVERIFY(QDir().mkpath(mRecentRepoDirectory.filePath("d2")));
  QVERIFY(QDir().mkpath(mRecentRepoDirectory.filePath("sources")));

  const QString r1Path = mRecentRepoDirectory.filePath("d1/r1");
  const QString r2Path = mRecentRepoDirectory.filePath("d1/r2");
  const QString r3Path = mRecentRepoDirectory.filePath("d1/r3");
  const QString r4Path = mRecentRepoDirectory.filePath("d2/r4");
  const QString s1SourcePath = mRecentRepoDirectory.filePath("sources/s1");
  const QString s2SourcePath = mRecentRepoDirectory.filePath("sources/s2");
  const QString s3SourcePath = mRecentRepoDirectory.filePath("sources/s3");

  for (const QString &path : {s1SourcePath, s2SourcePath, s3SourcePath}) {
    git::Repository source = git::Repository::init(path);
    QVERIFY(source.isValid());
    Test::initRepo(source);
    QVERIFY(source.commit("source commit").isValid());
  }

  git::Remote::Callbacks callbacks{s1SourcePath};
  git::Repository r1 = git::Repository::init(r1Path);
  git::Repository r2 = git::Repository::init(r2Path);
  git::Repository r3 = git::Repository::init(r3Path);
  git::Repository r4 = git::Repository::init(r4Path);
  QVERIFY(r1.isValid());
  QVERIFY(r2.isValid());
  QVERIFY(r3.isValid());
  QVERIFY(r4.isValid());

  Test::initRepo(r2);
  QVERIFY(git::Submodule::add(r2, s1SourcePath, "s1", true, 0, &callbacks));
  QVERIFY(git::Submodule::add(r2, s2SourcePath, "s2", true, 0, &callbacks));
  QVERIFY(r2.commit("r2 with two submodules").isValid());

  Test::initRepo(r4);
  QVERIFY(git::Submodule::add(r4, s3SourcePath, "s3", true, 0, &callbacks));
  QVERIFY(r4.commit("r4 with one submodule").isValid());

  QCOMPARE(r2.submodules().size(), 2);
  QCOMPARE(r4.submodules().size(), 1);
  for (const git::Submodule &submodule : r2.submodules()) {
    QVERIFY(submodule.isInitialized());
    QVERIFY(submodule.open().isValid());
  }
  QVERIFY(r4.submodules().first().isInitialized());
  QVERIFY(r4.submodules().first().open().isValid());

  const QString s1Path = r2.lookupSubmodule("s1").open().dir(false).path();
  const QString s2Path = r2.lookupSubmodule("s2").open().dir(false).path();
  const QString s3Path = r4.lookupSubmodule("s3").open().dir(false).path();
  mTestRecentPaths = {r1Path, r2Path, r3Path, r4Path, s1Path, s2Path, s3Path};
  RecentRepositories *recent = RecentRepositories::instance();
  for (const QString &path : mTestRecentPaths)
    recent->add(path);

  QVERIFY(mWindow->addTab(r1Path));
  QVERIFY(mWindow->addTab(r2Path));
  QVERIFY(mWindow->addTab(r3Path));
  QVERIFY(mWindow->addTab(r4Path));

  SideBar *sideBar = mWindow->findChild<SideBar *>();
  QVERIFY(sideBar);
  QTreeView *recentView = sideBar->findChild<QTreeView *>();
  QVERIFY(recentView);

  mTreeModeAction = sideBar->findChild<QAction *>("repositoryTreeMode");
  QVERIFY(mTreeModeAction);

  mOriginalTreeMode = mTreeModeAction->isChecked();
  if (!mOriginalTreeMode)
    mTreeModeAction->trigger();
  QVERIFY(mTreeModeAction->isChecked());

  QAbstractItemModel *model = recentView->model();
  QModelIndex openIndex = model->index(0, 0);
  QModelIndex openGroupIndex;
  QModelIndex openR2Index;
  QModelIndex openR4Index;
  for (int i = 0; i < model->rowCount(openIndex); ++i) {
    QModelIndex candidate = model->index(i, 0, openIndex);
    if (candidate.data(Qt::DisplayRole).toString() == "d1")
      openGroupIndex = candidate;
    if (candidate.data(Qt::DisplayRole).toString() == "r4")
      openR4Index = candidate;
  }
  QVERIFY(openGroupIndex.isValid());
  QVERIFY(openR4Index.isValid());
  QCOMPARE(model->rowCount(openGroupIndex), 3);
  QCOMPARE(model->rowCount(openR4Index), 0);
  for (int i = 0; i < model->rowCount(openGroupIndex); ++i) {
    QModelIndex candidate = model->index(i, 0, openGroupIndex);
    if (candidate.data(Qt::DisplayRole).toString() == "r2")
      openR2Index = candidate;
  }
  QVERIFY(openR2Index.isValid());
  QVERIFY(!recentView->isExpanded(openGroupIndex));
  QVERIFY(QMetaObject::invokeMethod(recentView, "clicked", Qt::DirectConnection,
                                    Q_ARG(QModelIndex, openR2Index)));
  QCOMPARE(mWindow->currentView()->repo().workdir().path(), r2Path);

  QModelIndex recentIndex = model->index(1, 0);
  QModelIndex groupIndex;
  for (int i = 0; i < model->rowCount(recentIndex); ++i) {
    QModelIndex candidate = model->index(i, 0, recentIndex);
    if (model->rowCount(candidate) != 3)
      continue;

    QStringList names;
    for (int j = 0; j < model->rowCount(candidate); ++j) {
      names.append(QFileInfo(model->index(j, 0, candidate)
                                 .data(Qt::DisplayRole)
                                 .toString())
                       .fileName());
    }
    if (names.contains("r1") && names.contains("r2") &&
        names.contains("r3")) {
      groupIndex = candidate;
      break;
    }
  }
  QVERIFY(groupIndex.isValid());
  QCOMPARE(groupIndex.data(Qt::DisplayRole).toString(), QString("d1"));
  QStringList rootNames;
  for (int i = 0; i < model->rowCount(recentIndex); ++i)
    rootNames.append(model->index(i, 0, recentIndex).data().toString());
  QVERIFY(rootNames.contains("d1"));
  QVERIFY(rootNames.contains("r4"));

  QModelIndex firstChild = model->index(0, 0, groupIndex);
  QCOMPARE(model->parent(firstChild), groupIndex);

  QModelIndex r2Index;
  QModelIndex r4Index;
  for (int i = 0; i < model->rowCount(groupIndex); ++i) {
    QModelIndex candidate = model->index(i, 0, groupIndex);
    if (candidate.data(Qt::DisplayRole).toString() == "r2")
      r2Index = candidate;
  }
  for (int i = 0; i < model->rowCount(recentIndex); ++i) {
    QModelIndex candidate = model->index(i, 0, recentIndex);
    if (candidate.data(Qt::DisplayRole).toString() == "r4")
      r4Index = candidate;
  }
  QVERIFY(r2Index.isValid());
  QVERIFY(r4Index.isValid());
  QCOMPARE(model->rowCount(r2Index), 2);
  QCOMPARE(model->rowCount(r4Index), 1);

  QStringList r2Submodules;
  for (int i = 0; i < model->rowCount(r2Index); ++i)
    r2Submodules.append(model->index(i, 0, r2Index).data().toString());
  QCOMPARE(r2Submodules, QStringList({"s1", "s2"}));
  QCOMPARE(model->index(0, 0, r4Index).data().toString(), QString("s3"));

  QVERIFY(!recentView->isExpanded(groupIndex));
  QVERIFY(!recentView->isExpanded(r4Index));
  recentView->expand(groupIndex);
  QVERIFY(recentView->isExpanded(groupIndex));
  recentView->collapse(groupIndex);
  QVERIFY(!recentView->isExpanded(groupIndex));
  recentView->expand(r4Index);
  QVERIFY(recentView->isExpanded(r4Index));
  recentView->collapse(r4Index);
  QVERIFY(!recentView->isExpanded(r4Index));

  recentView->expand(groupIndex);
  QVERIFY(!recentView->isExpanded(r2Index));
  recentView->expand(r2Index);
  QVERIFY(recentView->isExpanded(r2Index));
  recentView->collapse(r2Index);
  QVERIFY(!recentView->isExpanded(r2Index));
  recentView->collapse(groupIndex);

  const int treeRows = model->rowCount(recentIndex);
  mTreeModeAction->trigger();
  QVERIFY(!mTreeModeAction->isChecked());
  recentIndex = model->index(1, 0);
  const int listRows = model->rowCount(recentIndex);
  QVERIFY(listRows > treeRows);

  bool foundFirst = false;
  bool foundThird = false;
  for (int i = 0; i < listRows; ++i) {
    const QString name =
        QFileInfo(model->index(i, 0, recentIndex)
                      .data(Qt::DisplayRole)
                      .toString())
            .fileName();
    foundFirst = foundFirst || name == "r1";
    foundThird = foundThird || name == "r3";
  }
  QVERIFY(foundFirst);
  QVERIFY(foundThird);
}

void TestMainWindow::show() {
  mWindow->show();
  QVERIFY(qWaitForWindowActive(mWindow));
}

void TestMainWindow::cleanupTestCase() {
  if (mTreeModeAction &&
      mTreeModeAction->isChecked() != mOriginalTreeMode)
    mTreeModeAction->trigger();

  RecentRepositories *recent = RecentRepositories::instance();
  for (int i = recent->count() - 1; i >= 0; --i) {
    if (mTestRecentPaths.contains(recent->repository(i)->gitpath()))
      recent->remove(i);
  }

  mWindow->close();
}

TEST_MAIN(TestMainWindow)

#include "main_window.moc"
