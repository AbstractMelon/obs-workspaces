// SPDX-License-Identifier: GPL-2.0-or-later

#include "layout_dialog.hpp"

#include <obs-module.h>

#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

namespace docklayouts {

namespace {

QString text(const char *key)
{
	return QString::fromUtf8(obs_module_text(key));
}

} // namespace

LayoutDialog::LayoutDialog(const QString &directory, QWidget *parent)
	: QDialog(parent), list_(new QListWidget(this))
{
	setWindowTitle(text("Dialog.Title"));
	setMinimumSize(560, 360);
	resize(600, 400);

	list_->setAlternatingRowColors(true);
	list_->setSelectionMode(QAbstractItemView::SingleSelection);
	list_->setUniformItemSizes(true);

	auto *saveButton = new QPushButton(text("Dialog.SaveCurrent"), this);
	loadButton_ = new QPushButton(text("Dialog.Load"), this);
	renameButton_ = new QPushButton(text("Dialog.Rename"), this);
	deleteButton_ = new QPushButton(text("Dialog.Delete"), this);
	auto *openButton = new QPushButton(text("Dialog.OpenFolder"), this);
	auto *closeButton = new QPushButton(text("Dialog.Close"), this);

	auto *buttons = new QVBoxLayout;
	buttons->addWidget(saveButton);
	buttons->addSpacing(8);
	buttons->addWidget(loadButton_);
	buttons->addWidget(renameButton_);
	buttons->addWidget(deleteButton_);
	buttons->addStretch();
	buttons->addWidget(openButton);
	buttons->addWidget(closeButton);

	auto *content = new QHBoxLayout;
	content->addWidget(list_, 1);
	content->addLayout(buttons);

	auto *location = new QLabel(text("Dialog.Location").arg(QDir::toNativeSeparators(directory)), this);
	location->setTextInteractionFlags(Qt::TextSelectableByMouse);
	location->setWordWrap(true);
	location->setToolTip(QDir::toNativeSeparators(directory));

	auto *layout = new QVBoxLayout(this);
	layout->addLayout(content, 1);
	layout->addWidget(location);

	connect(saveButton, &QPushButton::clicked, this, &LayoutDialog::saveRequested);
	connect(loadButton_, &QPushButton::clicked, this, &LayoutDialog::activateSelected);
	connect(renameButton_, &QPushButton::clicked, this, [this] {
		const QString path = selectedPath();
		if (!path.isEmpty())
			emit renameRequested(path);
	});
	connect(deleteButton_, &QPushButton::clicked, this, [this] {
		const QString path = selectedPath();
		if (!path.isEmpty())
			emit deleteRequested(path);
	});
	connect(openButton, &QPushButton::clicked, this, &LayoutDialog::openFolderRequested);
	connect(closeButton, &QPushButton::clicked, this, &QDialog::close);
	connect(list_, &QListWidget::itemSelectionChanged, this, &LayoutDialog::updateButtons);
	connect(list_, &QListWidget::itemActivated, this, &LayoutDialog::activateSelected);

	updateButtons();
}

void LayoutDialog::refresh(const QVector<LayoutSummary> &layouts, const QString &selectPath)
{
	const QString wanted = selectPath.isEmpty() ? selectedPath() : selectPath;
	QListWidgetItem *selected = nullptr;

	list_->clear();
	for (const auto &layout : layouts) {
		auto *item = new QListWidgetItem(layout.name, list_);
		item->setData(Qt::UserRole, layout.path);
		item->setToolTip(QStringLiteral("%1\n%2\n%3")
					 .arg(layout.name,
					      layout.savedAt.toLocalTime().toString(
						      QStringLiteral("yyyy-MM-dd HH:mm")),
					      QDir::toNativeSeparators(layout.path)));
		if (QFileInfo(layout.path) == QFileInfo(wanted))
			selected = item;
	}

	if (selected)
		list_->setCurrentItem(selected);
	else if (list_->count() > 0)
		list_->setCurrentRow(0);

	updateButtons();
}

QString LayoutDialog::selectedPath() const
{
	const auto *item = list_->currentItem();
	return item ? item->data(Qt::UserRole).toString() : QString();
}

void LayoutDialog::activateSelected()
{
	const QString path = selectedPath();
	if (!path.isEmpty())
		emit loadRequested(path);
}

void LayoutDialog::updateButtons()
{
	const bool hasSelection = !selectedPath().isEmpty();
	loadButton_->setEnabled(hasSelection);
	renameButton_->setEnabled(hasSelection);
	deleteButton_->setEnabled(hasSelection);
}

} // namespace docklayouts
