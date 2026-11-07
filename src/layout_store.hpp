// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QString>
#include <QStringList>
#include <QVector>

class QMainWindow;

namespace docklayouts {

inline constexpr int kFormatVersion = 1;
inline constexpr qint64 kMaxLayoutFileSize = 4 * 1024 * 1024;

struct DockRef {
	QString id;
	QString title;
};

struct Layout {
	QString name;
	QByteArray dockState;
	QByteArray windowGeometry;
	QVector<DockRef> docks;
	QString obsVersion;
	QString qtVersion;
	QDateTime savedAt;
};

struct LayoutSummary {
	QString path;
	QString name;
	QDateTime savedAt;
};

enum class StoreError {
	None,
	InvalidName,
	NameExists,
	InvalidLayout,
	NotFound,
	ReadFailed,
	InvalidFile,
	WriteFailed,
};

class LayoutStore {
public:
	explicit LayoutStore(QString directory);

	const QString &directory() const { return directory_; }
	bool ensureDirectory(QString *error = nullptr) const;

	QVector<LayoutSummary> list(QStringList *invalidFiles = nullptr) const;
	QString findByName(const QString &name) const;
	QString pathForName(const QString &name) const;

	bool read(const QString &path, Layout *layout) const;
	StoreError save(const Layout &layout, QString *savedPath = nullptr, QString *error = nullptr);
	StoreError rename(const QString &path, const QString &newName, QString *newPath = nullptr,
			  QString *error = nullptr);
	bool remove(const QString &path);

	static bool validName(const QString &name);

private:
	StoreError write(const QString &path, const Layout &layout, QString *error = nullptr);
	QString directory_;
};

Layout captureLayout(QMainWindow *window, const QString &name, const QString &obsVersion = {});
bool applyLayout(QMainWindow *window, const Layout &layout, QString *error = nullptr);
QStringList missingDocks(QMainWindow *window, const Layout &layout);
QStringList dockIds(QMainWindow *window);

} // namespace docklayouts
