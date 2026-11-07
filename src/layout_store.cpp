// SPDX-License-Identifier: GPL-2.0-or-later

#include "layout_store.hpp"

#include <QDir>
#include <QDockWidget>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLayout>
#include <QMainWindow>
#include <QPair>
#include <QSaveFile>

#include <algorithm>

namespace docklayouts {

namespace {

const char *const kPluginId = "obs-dock-layouts";
const char *const kExtension = ".json";

QString normalizedName(const QString &name)
{
	return name.trimmed().normalized(QString::NormalizationForm_C);
}

bool isWindowsReservedName(const QString &name)
{
	const QString stem = name.section('.', 0, 0).trimmed().toUpper();
	static const QStringList reserved = {
		QStringLiteral("CON"),  QStringLiteral("PRN"),	QStringLiteral("AUX"),
		QStringLiteral("NUL"),  QStringLiteral("COM1"), QStringLiteral("COM2"),
		QStringLiteral("COM3"), QStringLiteral("COM4"), QStringLiteral("COM5"),
		QStringLiteral("COM6"), QStringLiteral("COM7"), QStringLiteral("COM8"),
		QStringLiteral("COM9"), QStringLiteral("LPT1"), QStringLiteral("LPT2"),
		QStringLiteral("LPT3"), QStringLiteral("LPT4"), QStringLiteral("LPT5"),
		QStringLiteral("LPT6"), QStringLiteral("LPT7"), QStringLiteral("LPT8"),
		QStringLiteral("LPT9"),
	};
	return reserved.contains(stem);
}

QJsonObject toJson(const Layout &layout)
{
	QJsonArray docks;
	for (const auto &dock : layout.docks)
		docks.append(QJsonObject{{QStringLiteral("id"), dock.id}, {QStringLiteral("title"), dock.title}});

	return {
		{QStringLiteral("format"), kFormatVersion},
		{QStringLiteral("plugin"), QString::fromLatin1(kPluginId)},
		{QStringLiteral("name"), layout.name},
		{QStringLiteral("saved_at"),
		 layout.savedAt.toUTC().toString(Qt::ISODateWithMs)},
		{QStringLiteral("obs_version"), layout.obsVersion},
		{QStringLiteral("qt_version"), layout.qtVersion},
		{QStringLiteral("dock_state"), QString::fromLatin1(layout.dockState.toBase64())},
		{QStringLiteral("window_geometry"), QString::fromLatin1(layout.windowGeometry.toBase64())},
		{QStringLiteral("docks"), docks},
	};
}

bool fromJson(const QJsonObject &object, Layout *layout)
{
	if (object.value(QStringLiteral("format")).toInt() != kFormatVersion ||
	    object.value(QStringLiteral("plugin")).toString() != QString::fromLatin1(kPluginId))
		return false;

	const QString name = object.value(QStringLiteral("name")).toString();
	if (!LayoutStore::validName(name))
		return false;

	const auto state = QByteArray::fromBase64Encoding(
		object.value(QStringLiteral("dock_state")).toString().toLatin1(),
		QByteArray::AbortOnBase64DecodingErrors);
	const auto geometry = QByteArray::fromBase64Encoding(
		object.value(QStringLiteral("window_geometry")).toString().toLatin1(),
		QByteArray::AbortOnBase64DecodingErrors);
	if (!state || state.decoded.isEmpty() || !geometry)
		return false;

	QVector<DockRef> docks;
	const auto dockArray = object.value(QStringLiteral("docks")).toArray();
	for (const auto &value : dockArray) {
		if (!value.isObject())
			return false;
		const auto dockObject = value.toObject();
		const QString id = dockObject.value(QStringLiteral("id")).toString();
		if (id.isEmpty())
			return false;
		docks.append({id, dockObject.value(QStringLiteral("title")).toString()});
	}

	Layout loaded;
	loaded.name = name;
	loaded.dockState = state.decoded;
	loaded.windowGeometry = geometry.decoded;
	loaded.docks = docks;
	loaded.obsVersion = object.value(QStringLiteral("obs_version")).toString();
	loaded.qtVersion = object.value(QStringLiteral("qt_version")).toString();
	loaded.savedAt = QDateTime::fromString(object.value(QStringLiteral("saved_at")).toString(),
					       Qt::ISODateWithMs);
	if (!loaded.savedAt.isValid())
		loaded.savedAt = QDateTime::currentDateTimeUtc();

	*layout = loaded;
	return true;
}

QString titleForDock(const QDockWidget *dock)
{
	QString title = dock->windowTitle();
	title.remove(QLatin1Char('&'));
	return title.isEmpty() ? dock->objectName() : title;
}

} // namespace

LayoutStore::LayoutStore(QString directory) : directory_(std::move(directory)) {}

bool LayoutStore::ensureDirectory(QString *error) const
{
	if (directory_.isEmpty()) {
		if (error)
			*error = QStringLiteral("The layout directory is not available.");
		return false;
	}
	if (QDir().mkpath(directory_))
		return true;
	if (error)
		*error = QStringLiteral("Could not create the layout directory:\n%1").arg(directory_);
	return false;
}

bool LayoutStore::validName(const QString &name)
{
	const QString trimmed = name.trimmed();
	if (trimmed.isEmpty() || trimmed.size() > 80 || trimmed != name)
		return false;
	if (trimmed == QStringLiteral(".") || trimmed == QStringLiteral("..") ||
	    trimmed.startsWith(QLatin1Char('.')) || trimmed.endsWith(QLatin1Char('.')))
		return false;
	if (trimmed.toUtf8().size() > 200)
		return false;
	if (isWindowsReservedName(trimmed))
		return false;

	for (const QChar character : trimmed) {
		if (character.unicode() < 32)
			return false;
		if (QStringLiteral("<>:\"/\\|?*").contains(character))
			return false;
	}
	return true;
}

QString LayoutStore::pathForName(const QString &name) const
{
	return QDir(directory_).filePath(normalizedName(name) + QString::fromLatin1(kExtension));
}

bool LayoutStore::read(const QString &path, Layout *layout) const
{
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly))
		return false;
	if (file.size() <= 0 || file.size() > kMaxLayoutFileSize)
		return false;

	QJsonParseError parseError;
	const auto document = QJsonDocument::fromJson(file.readAll(), &parseError);
	if (parseError.error != QJsonParseError::NoError || !document.isObject())
		return false;
	return fromJson(document.object(), layout);
}

StoreError LayoutStore::write(const QString &path, const Layout &layout, QString *error)
{
	if (!ensureDirectory(error))
		return StoreError::WriteFailed;
	if (layout.dockState.isEmpty())
		return StoreError::InvalidLayout;

	const auto data = QJsonDocument(toJson(layout)).toJson(QJsonDocument::Indented);
	if (data.size() > kMaxLayoutFileSize) {
		if (error)
			*error = QStringLiteral("The layout is too large to save.");
		return StoreError::InvalidLayout;
	}

	QSaveFile file(path);
	if (!file.open(QIODevice::WriteOnly)) {
		if (error)
			*error = file.errorString();
		return StoreError::WriteFailed;
	}
	if (file.write(data) != data.size() || !file.commit()) {
		if (error)
			*error = file.errorString();
		return StoreError::WriteFailed;
	}
	return StoreError::None;
}

StoreError LayoutStore::save(const Layout &layout, QString *savedPath, QString *error)
{
	const QString name = normalizedName(layout.name);
	if (!validName(name))
		return StoreError::InvalidName;
	if (layout.dockState.isEmpty())
		return StoreError::InvalidLayout;

	Layout copy = layout;
	copy.name = name;

	QString path = findByName(name);
	if (path.isEmpty())
		path = pathForName(name);

	const auto result = write(path, copy, error);
	if (result == StoreError::None && savedPath)
		*savedPath = path;
	return result;
}

QString LayoutStore::findByName(const QString &name) const
{
	const QString key = normalizedName(name).toCaseFolded();
	if (key.isEmpty())
		return {};

	const auto files = QDir(directory_).entryInfoList(
		{QString::fromLatin1("*") + QString::fromLatin1(kExtension)}, QDir::Files | QDir::Readable,
		QDir::Name);
	for (const auto &file : files) {
		Layout layout;
		if (read(file.absoluteFilePath(), &layout) && layout.name.toCaseFolded() == key)
			return file.absoluteFilePath();
	}
	return {};
}

QVector<LayoutSummary> LayoutStore::list(QStringList *invalidFiles) const
{
	QVector<LayoutSummary> layouts;
	const auto files = QDir(directory_).entryInfoList(
		{QString::fromLatin1("*") + QString::fromLatin1(kExtension)}, QDir::Files | QDir::Readable,
		QDir::Name);

	for (const auto &file : files) {
		Layout layout;
		if (!read(file.absoluteFilePath(), &layout)) {
			if (invalidFiles)
				invalidFiles->append(file.absoluteFilePath());
			continue;
		}
		LayoutSummary summary;
		summary.path = file.absoluteFilePath();
		summary.name = layout.name;
		summary.savedAt = layout.savedAt.isValid() ? layout.savedAt : file.lastModified();
		layouts.append(summary);
	}

	std::sort(layouts.begin(), layouts.end(), [](const LayoutSummary &a, const LayoutSummary &b) {
		const int nameResult = QString::compare(a.name, b.name, Qt::CaseInsensitive);
		return nameResult != 0 ? nameResult < 0 : a.name < b.name;
	});
	return layouts;
}

StoreError LayoutStore::rename(const QString &path, const QString &newName, QString *newPath,
			       QString *error)
{
	Layout layout;
	if (!QFileInfo::exists(path))
		return StoreError::NotFound;
	if (!read(path, &layout))
		return StoreError::ReadFailed;

	const QString name = normalizedName(newName);
	if (!validName(name))
		return StoreError::InvalidName;

	const QString existing = findByName(name);
	if (!existing.isEmpty() && QFileInfo(existing) != QFileInfo(path))
		return StoreError::NameExists;

	layout.name = name;
	layout.savedAt = QDateTime::currentDateTimeUtc();

	QString target = pathForName(name);
	if (!existing.isEmpty())
		target = existing;
	if (QFileInfo(target) == QFileInfo(path)) {
		const auto result = write(target, layout, error);
		if (result == StoreError::None && newPath)
			*newPath = target;
		return result;
	}

	const auto result = write(target, layout, error);
	if (result != StoreError::None)
		return result;
	if (!QFile::remove(path)) {
		QFile::remove(target);
		if (error)
			*error = QStringLiteral("Could not remove the old layout file.");
		return StoreError::WriteFailed;
	}
	if (newPath)
		*newPath = target;
	return StoreError::None;
}

bool LayoutStore::remove(const QString &path)
{
	return QFile::remove(path);
}

QStringList dockIds(QMainWindow *window)
{
	QStringList ids;
	if (!window)
		return ids;
	for (const auto *dock : window->findChildren<QDockWidget *>()) {
		const QString id = dock->objectName();
		if (!id.isEmpty())
			ids.append(id);
	}
	ids.removeDuplicates();
	ids.sort();
	return ids;
}

Layout captureLayout(QMainWindow *window, const QString &name, const QString &obsVersion)
{
	Layout layout;
	if (!window)
		return layout;

	layout.name = normalizedName(name);
	layout.dockState = window->saveState();
	layout.windowGeometry = window->saveGeometry();
	layout.obsVersion = obsVersion;
	layout.qtVersion = QString::fromLatin1(qVersion());
	layout.savedAt = QDateTime::currentDateTimeUtc();

	for (const auto *dock : window->findChildren<QDockWidget *>()) {
		const QString id = dock->objectName();
		if (!id.isEmpty())
			layout.docks.append({id, titleForDock(dock)});
	}
	std::sort(layout.docks.begin(), layout.docks.end(),
		  [](const DockRef &a, const DockRef &b) { return a.id < b.id; });
	layout.docks.erase(std::unique(layout.docks.begin(), layout.docks.end(),
				       [](const DockRef &a, const DockRef &b) { return a.id == b.id; }),
			   layout.docks.end());
	return layout;
}

QStringList missingDocks(QMainWindow *window, const Layout &layout)
{
	const QStringList present = dockIds(window);
	QStringList missing;
	for (const auto &dock : layout.docks) {
		if (present.contains(dock.id))
			continue;
		const QString title = dock.title.isEmpty() ? dock.id : dock.title;
		if (!missing.contains(title))
			missing.append(title);
	}
	missing.sort(Qt::CaseInsensitive);
	return missing;
}

bool applyLayout(QMainWindow *window, const Layout &layout, QString *error)
{
	if (!window) {
		if (error)
			*error = QStringLiteral("The OBS main window is not available.");
		return false;
	}
	if (layout.dockState.isEmpty()) {
		if (error)
			*error = QStringLiteral("The layout does not contain dock data.");
		return false;
	}

	const auto previousState = window->saveState();
	const auto previousGeometry = window->saveGeometry();
	const bool restoreGeometry = !layout.windowGeometry.isEmpty() && !window->isFullScreen();

	QVector<QPair<QDockWidget *, QDockWidget::DockWidgetFeatures>> dockFeatures;
	for (auto *dock : window->findChildren<QDockWidget *>())
		dockFeatures.append({dock, dock->features()});

	if (restoreGeometry && !window->restoreGeometry(layout.windowGeometry))
		window->restoreGeometry(previousGeometry);

	if (!window->restoreState(layout.dockState)) {
		window->restoreState(previousState);
		window->restoreGeometry(previousGeometry);
		for (const auto &entry : dockFeatures)
			entry.first->setFeatures(entry.second);
		if (error)
			*error = QStringLiteral("The saved dock layout could not be restored.");
		return false;
	}

	// OBS owns the dock lock setting, so do not let a saved layout change it.
	for (const auto &entry : dockFeatures)
		entry.first->setFeatures(entry.second);

	if (auto *mainLayout = window->layout())
		mainLayout->activate();

	return true;
}

} // namespace docklayouts
