//
//          Copyright (c) 2016, Scientific Toolworks, Inc.
//
// This software is licensed under the MIT License. The LICENSE.md file
// describes the conditions under which this software may be distributed.
//
// Author: Jason Haslam
//

#ifndef ADDSUBMODULEDIALOG_H
#define ADDSUBMODULEDIALOG_H

#include <QDialog>

class QCheckBox;
class QLineEdit;
class QPushButton;
class QSpinBox;

class AddSubmoduleDialog : public QDialog {
  Q_OBJECT

public:
  explicit AddSubmoduleDialog(QWidget *parent = nullptr);

  QString url() const;
  QString path() const;
  bool initialize() const;
  int depth() const;

private:
  void update();

  QLineEdit *mUrl;
  QLineEdit *mPath;
  QCheckBox *mInit;
  QSpinBox *mDepth;
  QPushButton *mAdd;
};

#endif