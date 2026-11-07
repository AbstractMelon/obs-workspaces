// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "layout_store.hpp"

#include <QDialog>

class QListWidget;
class QPushButton;

namespace docklayouts {

class LayoutDialog : public QDialog {
	Q_OBJECT

public:
	explicit LayoutDialog(const QString &directory, QWidget *parent = nullptr);

	void refresh(const QVector<LayoutSummary> &layouts, const QString &selectPath = {});

signals:
	void saveRequested();
	void loadRequested(const QString &path);
	void renameRequested(const QString &path);
	void deleteRequested(const QString &path);
	void openFolderRequested();

private:
	QString selectedPath() const;
	void updateButtons();
	void activateSelected();

	QListWidget *list_ = nullptr;
	QPushButton *loadButton_ = nullptr;
	QPushButton *renameButton_ = nullptr;
	QPushButton *deleteButton_ = nullptr;
};

} // namespace docklayouts
