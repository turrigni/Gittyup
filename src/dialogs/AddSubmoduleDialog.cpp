//
//          Copyright (c) 2016, Scientific Toolworks, Inc.
//
// This software is licensed under the MIT License. The LICENSE.md file
// describes the conditions under which this software may be distributed.
//
// Author: Jason Haslam
//

#include "AddSubmoduleDialog.h"
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <limits>

AddSubmoduleDialog::AddSubmoduleDialog(QWidget *parent) : QDialog(parent) {
  setAttribute(Qt::WA_DeleteOnClose);
  setWindowTitle(tr("Add Submodule"));

  mUrl = new QLineEdit(this);
  mPath = new QLineEdit(this);
  mInit = new QCheckBox(tr("Init"), this);
  mInit->setChecked(true);
  mDepth = new QSpinBox(this);
  mDepth->setRange(0, std::numeric_limits<int>::max());
  mDepth->setSpecialValueText(tr("Full history"));

  connect(mUrl, &QLineEdit::textChanged, this, [this] { update(); });
  connect(mPath, &QLineEdit::textChanged, this, [this] { update(); });

  QDialogButtonBox *buttons = new QDialogButtonBox(this);
  buttons->addButton(QDialogButtonBox::Cancel);
  mAdd = buttons->addButton(tr("Add"), QDialogButtonBox::AcceptRole);
  connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

  QFormLayout *layout = new QFormLayout(this);
  layout->addRow(tr("URL:"), mUrl);
  layout->addRow(tr("Path:"), mPath);
  layout->addRow(tr("Depth:"), mDepth);
  layout->addRow(mInit);
  layout->addRow(buttons);

  update();
}

QString AddSubmoduleDialog::url() const { return mUrl->text(); }

QString AddSubmoduleDialog::path() const { return mPath->text(); }

bool AddSubmoduleDialog::initialize() const { return mInit->isChecked(); }

int AddSubmoduleDialog::depth() const { return mDepth->value(); }

void AddSubmoduleDialog::update() {
  mAdd->setEnabled(!url().isEmpty() && !path().isEmpty());
}