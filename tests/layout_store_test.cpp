// SPDX-License-Identifier: GPL-2.0-or-later

#include "layout_store.hpp"

#include <QApplication>
#include <QDir>
#include <QDockWidget>
#include <QFile>
#include <QMainWindow>
#include <QTemporaryDir>

#include <cstdio>

using namespace docklayouts;

namespace {

int failures = 0;

void check(bool condition, const char *message)
{
	if (condition)
		return;
	std::fprintf(stderr, "FAIL: %s\n", message);
	++failures;
}

QMainWindow *makeWindow()
{
	auto *window = new QMainWindow;
	window->resize(1000, 700);

	auto *scenes = new QDockWidget(QStringLiteral("Scenes"), window);
	scenes->setObjectName(QStringLiteral("scenesDock"));
	scenes->setWidget(new QWidget(scenes));
	window->addDockWidget(Qt::LeftDockWidgetArea, scenes);

	auto *mixer = new QDockWidget(QStringLiteral("Audio Mixer"), window);
	mixer->setObjectName(QStringLiteral("mixerDock"));
	mixer->setWidget(new QWidget(mixer));
	window->addDockWidget(Qt::BottomDockWidgetArea, mixer);

	window->show();
	QApplication::processEvents();
	return window;
}

void testNames()
{
	check(!LayoutStore::validName(QString()), "empty name rejected");
	check(!LayoutStore::validName(QStringLiteral("  name")), "untrimmed name rejected");
	check(!LayoutStore::validName(QStringLiteral("a/b")), "separator rejected");
	check(!LayoutStore::validName(QStringLiteral("CON")), "Windows device name rejected");
	check(!LayoutStore::validName(QStringLiteral("..")), "dot name rejected");
	check(LayoutStore::validName(QStringLiteral("Streaming")), "regular name accepted");
	check(LayoutStore::validName(QStringLiteral("Café 布局")), "Unicode name accepted");
}

void testStore()
{
	QTemporaryDir temporary;
	check(temporary.isValid(), "temporary directory");
	LayoutStore store(temporary.path());

	Layout layout;
	layout.name = QStringLiteral("Streaming");
	layout.dockState = QByteArrayLiteral("state");
	layout.windowGeometry = QByteArrayLiteral("geometry");
	layout.docks = {{QStringLiteral("scenesDock"), QStringLiteral("Scenes")}};
	layout.obsVersion = QStringLiteral("33.0.1");
	layout.qtVersion = QStringLiteral("6.12.0");
	layout.savedAt = QDateTime::currentDateTimeUtc();

	QString path;
	check(store.save(layout, &path) == StoreError::None, "save layout");
	check(QFileInfo::exists(path), "layout file exists");

	Layout loaded;
	check(store.read(path, &loaded), "read layout");
	check(loaded.name == layout.name, "name round trip");
	check(loaded.dockState == layout.dockState, "dock state round trip");
	check(loaded.windowGeometry == layout.windowGeometry, "geometry round trip");
	check(loaded.docks.size() == 1 && loaded.docks.first().id == QStringLiteral("scenesDock"),
	      "dock list round trip");

	check(store.findByName(QStringLiteral("streaming")) == path, "case-insensitive lookup");
	check(store.list().size() == 1, "list layouts");

	Layout duplicate = layout;
	duplicate.name = QStringLiteral("STREAMING");
	QString duplicatePath;
	check(store.save(duplicate, &duplicatePath) == StoreError::None, "overwrite by name");
	check(duplicatePath == path, "case variant uses the same file");
	check(store.list().size() == 1, "no duplicate file");

	QString renamedPath;
	check(store.rename(path, QStringLiteral("Editing"), &renamedPath) == StoreError::None,
	      "rename layout");
	check(renamedPath != path && QFileInfo::exists(renamedPath), "renamed file exists");
	check(!QFileInfo::exists(path), "old file removed");

	Layout renamed;
	check(store.read(renamedPath, &renamed), "read renamed layout");
	check(renamed.name == QStringLiteral("Editing"), "renamed name stored");

	QFile corrupt(QDir(temporary.path()).filePath(QStringLiteral("broken.json")));
	check(corrupt.open(QIODevice::WriteOnly), "create corrupt file");
	corrupt.write("{not json");
	corrupt.close();

	QStringList invalid;
	const auto summaries = store.list(&invalid);
	check(summaries.size() == 1, "corrupt file skipped");
	check(invalid.size() == 1, "corrupt file reported");

	check(!store.remove(QStringLiteral("/does/not/exist")), "missing file removal fails");
	check(store.remove(renamedPath), "remove layout");
	check(store.list().isEmpty(), "empty list after removal");
}

void testCaptureAndApply()
{
	QMainWindow *window = makeWindow();
	window->setObjectName(QStringLiteral("main"));

	auto *mixer = window->findChild<QDockWidget *>(QStringLiteral("mixerDock"));
	check(mixer != nullptr, "find mixer dock");

	Layout layout = captureLayout(window, QStringLiteral("Test"), QStringLiteral("33.0.1"));
	check(!layout.dockState.isEmpty(), "captured dock state");
	check(layout.docks.size() == 2, "captured all docks");
	check(missingDocks(window, layout).isEmpty(), "no missing docks");

	mixer->hide();
	mixer->setFeatures(QDockWidget::NoDockWidgetFeatures);
	QApplication::processEvents();
	check(!mixer->isVisible(), "mixer hidden before restore");

	QString error;
	check(applyLayout(window, layout, &error), "apply layout");
	QApplication::processEvents();
	check(mixer->isVisible(), "mixer visibility restored");
	check(mixer->features() == QDockWidget::NoDockWidgetFeatures,
	      "current dock lock state preserved");

	Layout stale = layout;
	stale.docks.append({QStringLiteral("missingDock"), QStringLiteral("Missing Dock")});
	const auto missing = missingDocks(window, stale);
	check(missing.size() == 1 && missing.first() == QStringLiteral("Missing Dock"),
	      "missing dock reported");

	Layout broken;
	broken.name = QStringLiteral("Broken");
	check(!applyLayout(window, broken, &error), "empty layout rejected");

	delete window;
}

} // namespace

int main(int argc, char **argv)
{
	QApplication application(argc, argv);

	testNames();
	testStore();
	testCaptureAndApply();

	if (failures == 0)
		std::printf("All layout store tests passed\n");
	return failures == 0 ? 0 : 1;
}
